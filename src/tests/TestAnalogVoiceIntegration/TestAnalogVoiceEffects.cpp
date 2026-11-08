// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "Harness.h"
#include "AudioSignal.h"
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
#include "CodecGate.h"
#endif
namespace {
QByteArray settings(bool noise=false) {
    return "analogvoice=true\naudiofeedback=true\nsquelch=true\nbusytone=false\nsquelchopenvolume=50\nsquelchclosevolume=50\nfeedbackboostdb=0\nsquelchopenfile="
        + QByteArray(B4_ASSET_DIR)+"/squelch-open.wav\nsquelchclosefile="+QByteArray(B4_ASSET_DIR)
        +"/squelch-close.wav\nradionoisefile="+QByteArray(B4_ASSET_DIR)+"/radio-noise.wav\nradionoise="+(noise?"true":"false")+"\n";
}
QList<Received> from(const QList<Received> &packets, unsigned int session) {
    QList<Received> result; for(const auto &p:packets) if(p.session==session) result.append(p); return result;
}
std::vector<QByteArray> clip(bool close) {
    return AudioSignal::encodeWavSignal(QStringLiteral(B4_ASSET_DIR)+(close?"/squelch-close.wav":"/squelch-open.wav"),50,0);
}
}
class TestAnalogVoiceEffects : public QObject {
    Q_OBJECT
private slots:
    void profiles_data(); void profiles();
    void endings_data(); void endings();
    void noiseBusyAndTransport_data(); void noiseBusyAndTransport();
    void halfFramesAndRecipients();
    void handoffAndChannels();
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
    void blocked_data(); void blocked();
#endif
};
void TestAnalogVoiceEffects::profiles_data() {
    QTest::addColumn<QByteArray>("profile"); QTest::addColumn<int>("samples");
    for(const auto &name:{QByteArray("cb-am"),QByteArray("business-fm"),QByteArray("narrowband")})
        for(int samples:{480,960,1920,2880,5760})
            QTest::newRow((name+'-'+QByteArray::number(samples)).constData())<<name<<samples;
}
void TestAnalogVoiceEffects::profiles() {
    QFETCH(QByteArray,profile); QFETCH(int,samples);
    B4Fixture f; QVERIFY(f.start("true",2500,3000,false,settings()+"analogvoiceprofile="+profile+'\n'));
    Client sender,legacy,modern; QVERIFY(sender.login(f.port,"source",true));
    QVERIFY(legacy.login(f.port,"legacy")); QVERIFY(modern.login(f.port,"modern",true));
    const auto payload=opus(samples);
    Stream stream(funkvoice::profile(profile.toStdString())); QVERIFY(stream.begin(42)); Stream::Result expected;
    stream.process({reinterpret_cast<const unsigned char*>(payload.constData()),std::size_t(payload.size()),42,true},expected);
    QVERIFY(sendAudio(sender,payload,42,true));
    for(auto *receiver:{&legacy,&modern}) {
        const auto all=receiver->audio(350);
        const auto voice=from(all,sender.session), effects=from(all,receiver->session);
        QCOMPARE(voice.size(),int(expected.count));
        for(int i=0;i<voice.size();++i) {
            QCOMPARE(voice[i].payload,QByteArray(reinterpret_cast<const char*>(expected.packets[std::size_t(i)].payload.data()),int(expected.packets[std::size_t(i)].bytes)));
            QCOMPARE(voice[i].last,i+1==voice.size());
            QCOMPARE(all[i].session,sender.session); // All drain handed off before these finite effects.
        }
        auto expectedEffects=clip(false); const auto close=clip(true);
        expectedEffects.insert(expectedEffects.end(),close.begin(),close.end());
        QCOMPARE(effects.size(),int(expectedEffects.size()));
        for(int i=0;i<effects.size();++i) QCOMPARE(effects[i].payload,expectedEffects[std::size_t(i)]);
        QVERIFY(effects.back().last);
    }
}
void TestAnalogVoiceEffects::endings_data() {
    QTest::addColumn<int>("ending"); QTest::addColumn<bool>("udp");
    // Endings: confirmed payload, empty marker, timeout, no timeout Close,
    // Open-only, Close-only, disabled Squelch, ordered 5-ms bypass.
    for(int ending=0;ending<8;++ending) for(bool udp:{false,true})
        QTest::newRow(qPrintable(QString("%1-%2").arg(ending).arg(udp)))<<ending<<udp;
}
void TestAnalogVoiceEffects::endings() {
    QFETCH(int,ending); QFETCH(bool,udp);
    QByteArray config=settings()+"squelchclosetimeoutfallback=true\n";
    if(ending==3) config+="squelchclosetimeoutfallback=false\n";
    if(ending==4) config+="squelchclose=false\n";
    if(ending==5) config+="squelchopen=false\n";
    if(ending==6) config+="squelch=false\n";
    B4Fixture f; QVERIFY(f.start("true",500,1500,true,config));
    Client sender,receiver; QVERIFY(sender.login(f.port,"source")); QVERIFY(receiver.login(f.port,"receiver",udp));
    const bool timeout=ending==2||ending==3;
    QVERIFY(sendAudio(sender,opus(ending==7?240:960),32,!timeout&&ending!=1,udp));
    if(ending==1) QVERIFY(sendAudio(sender,{},34,true,udp));
    if(udp) QVERIFY(sender.udpBarrier()); else QVERIFY(sender.barrier());
    auto all=receiver.audio(timeout?850:350);
    const auto voice=from(all,sender.session), effects=from(all,receiver.session);
    QVERIFY(!voice.isEmpty()); QVERIFY(voice.back().last);
    std::vector<QByteArray> expected;
    if(ending!=5&&ending!=6) expected=clip(false);
    if(ending!=3&&ending!=4&&ending!=6) { const auto close=clip(true); expected.insert(expected.end(),close.begin(),close.end()); }
    QCOMPARE(effects.size(),int(expected.size()));
    for(int i=0;i<effects.size();++i) QCOMPARE(effects[i].payload,expected[std::size_t(i)]);
    // Finite Close starts after the actual last speech handoff, even on timeout.
    bool voiceEnded=false;
    for(const auto &p:all) {
        if(p.session==sender.session&&p.last) voiceEnded=true;
        if(p.session==receiver.session&&p.payload==clip(true)[0]) QVERIFY(voiceEnded);
    }
    QVERIFY(receiver.audio(100).isEmpty());
    f.server.stop(); QCOMPARE(f.count("released",timeout?"timeout_suspected":"marker_confirmed"),1);
    QCOMPARE(f.count("released",timeout?"marker_confirmed":"timeout_suspected"),0);
}
void TestAnalogVoiceEffects::noiseBusyAndTransport_data() {
    QTest::addColumn<bool>("modern"); QTest::addColumn<bool>("timeout"); QTest::addColumn<bool>("noiseOnly");
    for(bool modern:{false,true}) for(bool timeout:{false,true}) for(bool noiseOnly:{false,true})
        QTest::newRow(qPrintable(QString("%1-%2-%3").arg(modern).arg(timeout).arg(noiseOnly)))<<modern<<timeout<<noiseOnly;
}
void TestAnalogVoiceEffects::noiseBusyAndTransport() {
    QFETCH(bool,modern); QFETCH(bool,timeout); QFETCH(bool,noiseOnly);
    B4Fixture f; QByteArray config=settings(true)+"busytone=true\nbusytonevolume=50\nsquelchclosetimeoutfallback=true\n";
    if(noiseOnly) config+="squelch=false\n";
    QVERIFY(f.start("true",500,1500,false,config));
    Client sender,receiver,loser; QVERIFY(sender.login(f.port,"sender",modern));
    QVERIFY(receiver.login(f.port,"receiver",modern)); QVERIFY(loser.login(f.port,"loser",modern));
    QList<Received> all;
    // Paced speech, switching ingress TCP -> authenticated UDP -> TCP.
    for(int i=0;i<20;++i) {
        QVERIFY(sendAudio(sender,opus(960),std::uint64_t(i*2),!timeout&&i==19,i>=5&&i<15));
        if(i==5) QVERIFY(sendAudio(loser,opus(960),0,true));
        all+=receiver.audio(20);
    }
    all+=receiver.audio(timeout?850:350);
    const auto voice=from(all,sender.session), effects=from(all,receiver.session);
    QVERIFY(voice.size()>=20); QVERIFY(voice.back().last);
    QVERIFY(effects.size()>10);
    QCOMPARE(from(all,loser.session).size(),0);
    int ends=0,error=0;
    std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
    QVERIFY(decoder);
    for(int i=0;i<effects.size();++i) {
        if(effects[i].last) ++ends;
        if(i) QCOMPARE(effects[i].frame,effects[i-1].frame+2);
        float pcm[960]; QCOMPARE(opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char*>(effects[i].payload.constData()),effects[i].payload.size(),pcm,960,0),960);
    }
    QCOMPARE(ends,1); // One continuous private Open/Noise/Close stream.
    QVERIFY(effects.back().last);
    QVERIFY(all.front().session==sender.session); // No effect before initial voice.
    const auto denied=from(loser.audio(100),loser.session);
    const auto busy=AudioSignal::encode(AudioSignal::syntheticBusy(),50,0);
    int busyPackets=0; for(const auto &p:denied) if(std::find(busy.begin(),busy.end(),p.payload)!=busy.end()) ++busyPackets;
    QCOMPARE(busyPackets,int(busy.size()));
    QVERIFY(receiver.audio(100).isEmpty());
}
void TestAnalogVoiceEffects::halfFramesAndRecipients() {
    B4Fixture f; QVERIFY(f.start("true",2500,3000,false,settings(true)));
    Client sender,stays,moves,deaf,admin; QVERIFY(sender.login(f.port,"sender"));
    QVERIFY(stays.login(f.port,"stays")); QVERIFY(moves.login(f.port,"moves"));
    QVERIFY(deaf.login(f.port,"deaf")); QVERIFY(admin.login(f.port,"SuperUser"));
    const auto other=channel(admin,"other"); QVERIFY(other);
    QVERIFY(sendAudio(sender,opus(480),0,false)); QVERIFY(sender.barrier());
    QVERIFY(stays.audio(0).isEmpty()); // Half-frame accepted, no actual output yet.
    QVERIFY(moves.move(other)&&moves.move(0)); QVERIFY(setDeaf(deaf,true)&&setDeaf(deaf,false));
    Client late; QVERIFY(late.login(f.port,"late"));
    QVERIFY(sendAudio(sender,opus(480),1,true));
    const auto all=stays.audio(400);
    QCOMPARE(from(all,sender.session).size(),2);
    const auto effects=from(all,stays.session); QVERIFY(!effects.isEmpty()&&effects.back().last);
    for(auto *excluded:{&moves,&deaf,&late}) QVERIFY(excluded->audio(50).isEmpty());
}
void TestAnalogVoiceEffects::handoffAndChannels() {
    B4Fixture f; QVERIFY(f.start("true",2500,3000,false,settings(true)));
    Client a,b,receiver,admin,other,otherReceiver;
    QVERIFY(a.login(f.port,"A")); QVERIFY(b.login(f.port,"B")); QVERIFY(receiver.login(f.port,"receiver"));
    QVERIFY(admin.login(f.port,"SuperUser")); const auto room=channel(admin,"other"); QVERIFY(room);
    QVERIFY(other.login(f.port,"other")); QVERIFY(otherReceiver.login(f.port,"otherReceiver"));
    QVERIFY(other.move(room)&&otherReceiver.move(room));
    QVERIFY(sendAudio(a,opus(5760),0,true)); QVERIFY(a.barrier());
    QVERIFY(sendAudio(b,opus(960),20,true)); QVERIFY(sendAudio(other,opus(960),0,true));
    auto all=receiver.audio(400); bool endedA=false,seenB=false; int endsA=0,endsB=0;
    for(const auto &p:all) {
        if(p.session==a.session) { QVERIFY(!seenB); if(p.last) { endedA=true; ++endsA; } }
        if(p.session==b.session) { QVERIFY(endedA); seenB=true; if(p.last) ++endsB; }
        QVERIFY(p.session!=other.session);
    }
    QCOMPARE(endsA,1); QCOMPARE(endsB,1);
    all=otherReceiver.audio(100); QCOMPARE(from(all,other.session).size(),2);
    QVERIFY(!from(all,otherReceiver.session).isEmpty());
}
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
void TestAnalogVoiceEffects::blocked_data() {
    QTest::addColumn<int>("boundary");
    for(int boundary=0;boundary<5;++boundary) QTest::newRow(qPrintable(QString::number(boundary)))<<boundary;
}
void TestAnalogVoiceEffects::blocked() {
    QFETCH(int,boundary);
    B4Fixture f; CodecGate gate; QVERIFY(gate.attach(f));
    QVERIFY(f.start("true",500,1500,false,settings(true)));
    Client sender,receiver,admin; QVERIFY(sender.login(f.port,"sender")); QVERIFY(receiver.login(f.port,"receiver"));
    QVERIFY(admin.login(f.port,"SuperUser")); const auto other=channel(admin,"other"); QVERIFY(other);
    QVERIFY(sendAudio(sender,opus(960),0,false,true)); QVERIFY(gate.wait());
    QVERIFY(receiver.audio(0).isEmpty()); // Neither speech nor effects at admission.
    if(boundary==0) { // Fast confirmed handoff while worker is blocked.
        QVERIFY(sendAudio(sender,opus(960),2,true,true)); QVERIFY(sender.udpBarrier());
        QVERIFY(sendAudio(admin,opus(960),20,true,true)); QVERIFY(admin.udpBarrier());
    } else if(boundary==1) { sender.socket.abort(); QVERIFY(admin.barrier()); }
    else if(boundary==2) QVERIFY(sender.move(other));
    else if(boundary==3) { MumbleProto::UserState state; state.set_self_mute(true); QVERIFY(sender.send(Type::UserState,state)&&sender.barrier()); }
    else { QVERIFY(receiver.audio(550).isEmpty()); } // True timeout while codec blocked, job expires.
    gate.resume();
    auto all=receiver.audio(400);
    if(boundary) QVERIFY(all.isEmpty());
    else {
        bool b=false,aEnd=false;
        for(const auto &p:all) {
            if(p.session==sender.session) { QVERIFY(!b); aEnd=aEnd||p.last; }
            if(p.session==admin.session) { QVERIFY(aEnd); b=true; }
        }
        QVERIFY(b);
    }
}
#endif
QTEST_MAIN(TestAnalogVoiceEffects)
#include "TestAnalogVoiceEffects.moc"
