// Copyright The Mumble-Funk Contributors. BSD-style license: see LICENSE.
#include "AudioFeedback.h"
#include "AudioSignal.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEndian>
#include <QtTest/QtTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <opus.h>
#include <stdexcept>

static QString asset(const char *name="busy-tone.wav") { return QStringLiteral(BUSY_ASSET_DIR)+"/"+name; }
struct Decoded { std::vector<float> pcm; double peak=0, rms=0; bool valid=true; };
static Decoded decode(const std::vector<QByteArray> &packets) {
    Decoded out; int error=0;
    std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
    if(!decoder || error) { out.valid=false; return out; }
    for(const auto &packet:packets) {
        std::array<float,960> frame{};
        if(opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char*>(packet.data()),packet.size(),frame.data(),960,0)!=960) { out.valid=false; return out; }
        for(float s:frame) { out.valid=out.valid && std::isfinite(s) && std::abs(s)<1.f; out.peak=std::max(out.peak,double(std::abs(s))); out.rms+=double(s)*s; }
        out.pcm.insert(out.pcm.end(),frame.begin(),frame.end());
    }
    out.rms=std::sqrt(out.rms/static_cast<double>(out.pcm.size())); return out;
}
static QString wav(QTemporaryDir &dir,unsigned rate,unsigned frames) {
    QByteArray bytes(44+int(frames*2),'\0'); std::memcpy(bytes.data(),"RIFF",4);
    qToLittleEndian<quint32>(quint32(bytes.size()-8),bytes.data()+4);
    std::memcpy(bytes.data()+8,"WAVEfmt ",8); qToLittleEndian<quint32>(16,bytes.data()+16);
    qToLittleEndian<quint16>(1,bytes.data()+20); qToLittleEndian<quint16>(1,bytes.data()+22);
    qToLittleEndian<quint32>(rate,bytes.data()+24); qToLittleEndian<quint32>(rate*2,bytes.data()+28);
    qToLittleEndian<quint16>(2,bytes.data()+32); qToLittleEndian<quint16>(16,bytes.data()+34);
    std::memcpy(bytes.data()+36,"data",4); qToLittleEndian<quint32>(frames*2,bytes.data()+40);
    // Mathematical test fixture only; never a replacement for the original asset.
    for(unsigned i=0;i<frames;++i) qToLittleEndian<qint16>(qint16(15000*std::sin(2*3.141592653589793*300*i/rate)),bytes.data()+44+i*2);
    const auto path=dir.filePath("boundary.wav"); QFile f(path);
    if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size()) return {};
    return path;
}
class TestBusyToneAsset:public QObject {
    Q_OBJECT
private slots:
    void original(); void limits(); void levels(); void scheduling(); void priority();
};
void TestBusyToneAsset::original() {
    QFile file(asset()); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll();
    QCOMPARE(bytes.size(),115016);
    QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),QByteArray("c24e7af67bc9aac5b040fad1118f006d79540a34b427d064e9a5596d0f11d107"));
    // Only an optional metadata chunk was removed. Pin the raw PCM bytes shared
    // with the privately retained original, independently of RIFF/header bytes.
    QByteArray rawPCM;
    for(int pos=12;pos<bytes.size();) {
        QVERIFY(pos+8<=bytes.size());
        const auto length=qFromLittleEndian<quint32>(bytes.constData()+pos+4);
        QVERIFY(length<=quint32(bytes.size()-pos-8));
        const auto name=bytes.mid(pos,4); QVERIFY(name!="bext");
        if(name=="data") rawPCM=bytes.mid(pos+8,int(length));
        pos+=8+int(length)+int(length&1);
    }
    QCOMPARE(rawPCM.size(),114936);
    QCOMPARE(QCryptographicHash::hash(rawPCM,QCryptographicHash::Sha256).toHex(),QByteArray("ed0a899bf86c0d9772d49e2a88b9c04066ad9860094c518e16b3d2f583a1eae3"));
    const auto pcm=AudioSignal::loadWav(asset()); QCOMPARE(pcm.size(),std::size_t(57468+960));
    QVERIFY(std::all_of(pcm.begin()+57468,pcm.end(),[](float s){return s==0;}));
    QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(asset(),AudioSignal::LengthPolicy::ShortSignal),std::runtime_error);
    const auto packets=AudioSignal::encode(pcm,100); QCOMPARE(packets.size(),std::size_t(61));
    const auto decoded=decode(packets); QVERIFY(decoded.valid); QCOMPARE(decoded.pcm.size(),std::size_t(58560));
    int error=0,lookahead=0;
    std::unique_ptr<OpusEncoder,decltype(&opus_encoder_destroy)> encoder(opus_encoder_create(48000,1,OPUS_APPLICATION_AUDIO,&error),opus_encoder_destroy);
    QVERIFY(encoder); QCOMPARE(opus_encoder_ctl(encoder.get(),OPUS_GET_LOOKAHEAD(&lookahead)),OPUS_OK);
    QVERIFY(decoded.pcm.size()>=std::size_t(57468+lookahead)); // Entire original plus codec delay fits.
    double sourceTail=0,decodedTail=0;
    for(std::size_t i=55000;i<57468;++i) { sourceTail+=double(pcm[i])*pcm[i]; decodedTail+=double(decoded.pcm[i+std::size_t(lookahead)])*decoded.pcm[i+std::size_t(lookahead)]; }
    QVERIFY(sourceTail>0); QVERIFY(decodedTail>0); // No truncation of a nonempty recording tail.
}
void TestBusyToneAsset::limits() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    for(unsigned rate:{8000u,16000u,24000u,48000u}) {
        for(unsigned frames:{rate/50,rate,rate+1,rate*5/4}) {
            const auto path=wav(dir,rate,frames); QVERIFY(!path.isEmpty());
            const auto pcm=AudioSignal::loadWav(path);
            QCOMPARE(pcm.size(),std::size_t(frames)*48000/rate+960);
            QVERIFY(decode(AudioSignal::encode(pcm,100,12)).valid);
            if(frames>rate) QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(path,AudioSignal::LengthPolicy::ShortSignal),std::runtime_error);
        }
        const auto longFile=wav(dir,rate,rate*5/4+1);
        QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(longFile),std::runtime_error);
        const auto shortFile=wav(dir,rate,rate/50-1);
        QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(shortFile),std::runtime_error);
    }
    QCOMPARE(AudioSignal::encode(std::vector<float>(60960),100).size(),std::size_t(64));
    QVERIFY_EXCEPTION_THROWN(AudioSignal::encode(std::vector<float>(60961),100),std::runtime_error);
    QFile corrupt(dir.filePath("corrupt.wav")); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("invalid"); corrupt.close();
    AudioFeedback fallback(100,corrupt.fileName(),12), synthetic(100,{},12);
    QVERIFY(!fallback.customBusy()); QVERIFY(!fallback.warning().isEmpty());
    QVERIFY(fallback.clip(AudioFeedback::Signal::Busy)==synthetic.clip(AudioFeedback::Signal::Busy));
    QCOMPARE(synthetic.clip(AudioFeedback::Signal::Busy).size(),std::size_t(13));
}
void TestBusyToneAsset::levels() {
    const auto pcm=AudioSignal::loadWav(asset());
    const auto base=decode(AudioSignal::encode(pcm,100)); QVERIFY(base.valid); QVERIFY(base.rms>0);
    for(int volume:{0,50,100}) for(int boost:{0,6,8,12}) {
        AudioFeedback bank(volume,asset(),boost); QVERIFY(bank.customBusy()); QVERIFY(bank.warning().isEmpty());
        const auto &clip=bank.clip(AudioFeedback::Signal::Busy); QCOMPARE(clip.size(),std::size_t(61));
        const auto measured=decode(clip); QVERIFY(measured.valid);
        if(!volume) { QCOMPARE(measured.peak,0.0); continue; }
        QVERIFY(measured.peak<=.98f);
        const double gain=20*std::log10(measured.rms/base.rms);
        const double wanted=boost+20*std::log10(volume/100.0);
        QVERIFY(std::abs(gain-wanted)<1.0);
        qInfo("Busy original volume=%d boost=%d decoded_peak=%.6f decoded_rms=%.6f measured_gain_db=%.3f packets=%zu",volume,boost,measured.peak,measured.rms,gain,clip.size());
    }
}
void TestBusyToneAsset::scheduling() {
    AudioFeedback bank(100,asset(),12); int count=0,endings=0; std::uint64_t lastFrame=0;
    const auto clip=bank.clip(AudioFeedback::Signal::Busy);
    auto deliver=[&](const AudioFeedback::Recipient &to,const QByteArray &bytes,std::uint64_t frame,bool last) {
        if(to.connection!=1 || bytes!=clip[std::size_t(count)%clip.size()] || (count && frame<=lastFrame)) return false;
        lastFrame=frame; ++count; if(last) ++endings; return true;
    };
    QVERIFY(bank.request(AudioFeedback::Signal::Busy,{1,1,0},0));
    for(std::int64_t now=0;now<=1200000;now+=20000) {
        if(now==1000000) QVERIFY(bank.request(AudioFeedback::Signal::Busy,{1,1,0},now));
        bank.tick(now,deliver);
    }
    QCOMPARE(count,61); QCOMPARE(endings,1); QCOMPARE(lastFrame,std::uint64_t(120));
    bank.tick(1220000,deliver); QCOMPARE(count,61); // Denied requests never queue another active Busy.
    QVERIFY(bank.request(AudioFeedback::Signal::Busy,{1,1,0},1240000)); bank.tick(1240000,deliver);
    QCOMPARE(count,62); QVERIFY(lastFrame>120);
    AudioFeedback shortBank(100); int shortCount=0;
    auto shortDelivery=[&](const AudioFeedback::Recipient&,const QByteArray&,std::uint64_t,bool){++shortCount;return true;};
    shortBank.request(AudioFeedback::Signal::Busy,{2,2,0},0);
    for(std::int64_t now=0;now<=300000;now+=20000) shortBank.tick(now,shortDelivery);
    shortBank.request(AudioFeedback::Signal::Busy,{2,2,0},900000);shortBank.tick(900000,shortDelivery);QCOMPARE(shortCount,13);
    shortBank.request(AudioFeedback::Signal::Busy,{2,2,0},1000000);shortBank.tick(1000000,shortDelivery);QCOMPARE(shortCount,14);
}
void TestBusyToneAsset::priority() {
    AudioFeedback bank(100,asset(),12); bank.configureSquelch(asset("squelch-open.wav"),asset("squelch-close.wav"),100,100);
    SquelchPlan::Cue open{},close{}; open.kind=SquelchPlan::Kind::Open; close.kind=SquelchPlan::Kind::Close;
    open.sender={9,9,0};open.recipient={1,1,0};close.sender=open.sender;close.recipient=open.recipient;
    open.createdUs=0; close.createdUs=100000;
    std::vector<QByteArray> received;std::vector<bool> endings;std::vector<std::uint64_t> frames;
    auto deliver=[&](const AudioFeedback::Recipient&,const QByteArray &bytes,std::uint64_t frame,bool last){received.push_back(bytes);endings.push_back(last);frames.push_back(frame);return true;};
    auto gate=[](const SquelchPlan::Cue&,bool){return true;};
    QVERIFY(bank.requestSquelch(open));bank.tick(0,deliver,gate);
    QVERIFY(bank.request(AudioFeedback::Signal::Busy,{1,1,0},10000));
    for(std::int64_t now=20000;now<=1400000;now+=20000) {
        if(now==100000) QVERIFY(bank.requestSquelch(close));
        bank.tick(now,deliver,gate);
    }
    const auto &busy=bank.clip(AudioFeedback::Signal::Busy);
    QCOMPARE(received.size(),busy.size()+2);QVERIFY(endings[1]);QVERIFY(endings.back());
    for(std::size_t i=0;i<busy.size();++i) QCOMPARE(received[i+2],busy[i]);
    for(std::size_t i=1;i<frames.size();++i) QVERIFY(frames[i]>frames[i-1]);
    QVERIFY(decode(received).valid);
}
QTEST_MAIN(TestBusyToneAsset)
#include "TestBusyToneAsset.moc"
