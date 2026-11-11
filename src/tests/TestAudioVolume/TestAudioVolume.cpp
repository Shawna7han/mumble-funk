// Copyright The Mumble-Funk Contributors. BSD-style license: see LICENSE.
#include "AudioFeedback.h"
#include "AudioSignal.h"
#include <QtTest/QtTest>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <opus.h>
#include <stdexcept>

using Clip = std::vector<QByteArray>;
// Frozen reference for pre-boost arithmetic and Opus settings; independent of
// the production gain calculation. Explicit zero must remain byte-identical.
static Clip legacy(const std::vector<float> &pcm, int volume, bool shortSignal) {
    int error = 0;
    std::unique_ptr<OpusEncoder, decltype(&opus_encoder_destroy)> encoder(
        opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &error), opus_encoder_destroy);
    if (!encoder || error || opus_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(24000)) != OPUS_OK) return {};
    float peak = 0;
    for (float s : pcm) peak = std::max(peak, std::abs(s));
    const float gain = (peak > .2f ? .2f / peak : 1.f) * static_cast<float>(std::max(0, std::min(100, volume))) / 100;
    int lookahead = 0;
    opus_encoder_ctl(encoder.get(), OPUS_GET_LOOKAHEAD(&lookahead));
    const auto length = shortSignal ? pcm.size() - 960 + std::size_t(lookahead) : pcm.size();
    Clip result;
    for (std::size_t at = 0; at < length; at += 960) {
        std::array<float,960> frame{};
        for (std::size_t i=0; i<960 && at+i<pcm.size(); ++i) frame[i] = pcm[at+i] * gain;
        std::array<unsigned char,512> bytes{};
        const int n = opus_encode_float(encoder.get(),frame.data(),960,bytes.data(),512);
        if(n < 0) return {};
        result.emplace_back(reinterpret_cast<const char*>(bytes.data()), n);
    }
    return result;
}
struct Level { double peak=0, rms=0; std::size_t count=0; bool valid=true; };
static Level decode(const Clip &clip) {
    int error=0;
    std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(
        opus_decoder_create(48000,1,&error), opus_decoder_destroy);
    Level out;
    if (!decoder || error) { out.valid=false; return out; }
    for(const auto &packet:clip) {
        std::array<float,960> pcm{};
        const int n=opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char*>(packet.data()),packet.size(),pcm.data(),960,0);
        if(n!=960) { out.valid=false; return out; }
        for(float s:pcm) {
            out.valid=out.valid && std::isfinite(s) && std::abs(s)<1.f;
            out.peak=std::max(out.peak,double(std::abs(s))); out.rms+=double(s)*s; ++out.count;
        }
    }
    out.rms=std::sqrt(out.rms/static_cast<double>(out.count));
    return out;
}
static QString asset(const char *name) { return QStringLiteral(AUDIO_ASSET_DIR)+"/"+name; }
class TestAudioVolume:public QObject {
    Q_OBJECT
private slots:
    void zeroCompatibility();
    void levels();
    void invalid();
};
void TestAudioVolume::zeroCompatibility() {
    for(int volume:{0,25,50,100,200}) {
        const auto busy=AudioSignal::syntheticBusy();
        QVERIFY(AudioSignal::encode(busy,volume,0)==legacy(busy,volume,false));
        QVERIFY(AudioSignal::encode(busy,volume)==legacy(busy,volume,false));
        const auto custom = AudioSignal::loadWav(asset("squelch-close.wav"));
        QVERIFY(AudioSignal::encode(custom,volume,0)==legacy(custom,volume,false));
        AudioFeedback bank(volume,asset("squelch-close.wav"),0);
        QVERIFY(bank.clip(AudioFeedback::Signal::Busy)==legacy(custom,volume,false));
        const auto originalBusy = AudioSignal::loadWav(asset("busy-tone.wav"));
        QVERIFY(AudioSignal::encode(originalBusy,volume,0)==legacy(originalBusy,volume,false));
        for(const char *name:{"squelch-open.wav","squelch-close.wav"}) {
            const auto wav=AudioSignal::loadWav(asset(name),AudioSignal::LengthPolicy::ShortSignal);
            QVERIFY(AudioSignal::encodeWavSignal(asset(name),volume,0)==legacy(wav,volume,true));
        }
    }
}
void TestAudioVolume::levels() {
    // Real shipped WAVs, original synthetic Busy, custom Busy WAV, and loud/quiet
    // adversarial PCM. All are decoded, not inferred from a config or formula.
    for(int kind=0;kind<10;++kind) {
        std::vector<float> pcm(960*8);
        for(std::size_t i=0;i<pcm.size();++i)
            pcm[i]=float((kind==4 ? 1.0 : .01)*std::sin(2*3.141592653589793*600*static_cast<double>(i)/48000));
        if (kind>=6) {
            unsigned noise=12345;
            for (std::size_t i=0;i<pcm.size();++i) {
                noise=noise*1664525u+1013904223u;
                if(kind==6) pcm[i]=(i%80<40)?1.f:-1.f;
                if(kind==7) pcm[i]=(i%960==0)?1.f:0.f;
                if(kind==8) pcm[i]=(noise&1u)?1.f:-1.f;
                if(kind==9) pcm[i]=float(double(noise)/4294967295.0*2-1);
            }
        }
        auto encode=[&](int boost,int volume) {
            AudioFeedback bank(volume, kind==3?asset("squelch-close.wav"):QString(),boost);
            if(kind==1 || kind==2) {
                bank.configureSquelch(asset("squelch-open.wav"),asset("squelch-close.wav"),volume,volume);
                return bank.clip(kind==1?AudioFeedback::Signal::SquelchOpen:AudioFeedback::Signal::SquelchClose);
            }
            return kind>=4 ? AudioSignal::encode(pcm,volume,boost) : bank.clip(AudioFeedback::Signal::Busy);
        };
        const auto baseline=encode(0,100); const auto base=decode(baseline);
        QVERIFY(base.valid); QVERIFY(base.rms>0);
        for(int db:{0,3,6,9,12}) {
            const auto packets=encode(db,100); const auto level=decode(packets);
            QVERIFY(level.valid); if(db) QVERIFY(level.peak<=.98); QCOMPARE(packets.size(),baseline.size()); QCOMPARE(level.count,base.count);
            const double measured=20*std::log10(level.rms/base.rms);
            QVERIFY2((kind>=6 && measured<=db+.8 && measured>=-.8) || std::abs(measured-db)<.8,qPrintable(QString("kind %1 db %2 actual %3").arg(kind).arg(db).arg(measured)));
            qInfo("effect=%d boost=%d decoded_peak=%.6f decoded_rms=%.6f measured_gain_db=%.3f packets=%zu",
                  kind,db,level.peak,level.rms,measured,packets.size());
            const auto silent=decode(encode(db,0)); QVERIFY(silent.valid); QVERIFY(silent.peak<1e-7);
            // Volume still acts independently; neither normalization nor boost unmutes zero.
            const auto half=decode(encode(db,50)); QVERIFY(half.valid);
            if(kind<6) QVERIFY(std::abs(20*std::log10(half.rms/level.rms)+6.0206)<.8);
            else QVERIFY(half.rms<level.rms && half.peak<=.98);
        }
    }
}
void TestAudioVolume::invalid() {
    for(int db:{-1,13,100,std::numeric_limits<int>::max(),std::numeric_limits<int>::min()}) {
        QVERIFY_EXCEPTION_THROWN(AudioSignal::encode(AudioSignal::syntheticBusy(),100,db),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(AudioSignal::encodeWavSignal(asset("squelch-open.wav"),100,db),std::runtime_error);
    }
    for(float s:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),1.01f})
        QVERIFY_EXCEPTION_THROWN(AudioSignal::encode(std::vector<float>(960,s),100,12),std::runtime_error);
}
QTEST_MAIN(TestAudioVolume)
#include "TestAudioVolume.moc"
