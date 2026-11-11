// Copyright The Mumble-Funk Contributors. BSD-style license: see LICENSE.
#include "../TestSimplexIntegration/Harness.h"
#include "AudioSignal.h"
#include <array>
#include <cmath>
#include <opus.h>
using namespace SimplexTest;
static QString asset(const char *name) { return QStringLiteral(AUDIO_ASSET_DIR) + '/' + name; }
static QByteArray config(bool squelch = true) {
    // Private recording is supplied only through the test environment, never packaged.
    auto file = qEnvironmentVariable("MUMBLE_FUNK_NOISE_WAV", asset("squelch-close.wav"));
    return "audiofeedback=true\nradionoise=true\nradionoisevolume=20\nradionoisefile=" + file.toUtf8()
        + "\nsquelch=" + (squelch ? "true" : "false") + "\nsquelchopenfile=" + asset("squelch-open.wav").toUtf8()
        + "\nsquelchclosefile=" + asset("squelch-close.wav").toUtf8() + "\n";
}
static bool sendVoice(Client &client, const QByteArray &pcm, std::uint64_t frame, bool last, bool udp) {
    Mumble::Protocol::AudioData a; a.frameNumber = frame; a.isLastFrame = last; a.targetOrContext = 0;
    a.payload = {reinterpret_cast<const Mumble::Protocol::byte *>(pcm.data()), std::size_t(pcm.size())};
    Mumble::Protocol::UDPAudioEncoder<Mumble::Protocol::Role::Client> encoder(client.version);
    const auto data = encoder.encodeAudioPacket(a); QByteArray bytes(reinterpret_cast<const char *>(data.data()), int(data.size()));
    return udp ? client.sendEncrypted(bytes) : client.sendRaw(Type::UDPTunnel, bytes);
}
static QList<Received> from(const QList<Received> &all, unsigned session) {
    QList<Received> out; for (const auto &p : all) if (p.session == session) out.append(p); return out;
}
static bool appendAudio(const QList<Client::Message> &messages, Version::full_t version, QList<Received> &out) {
    for (const auto &message : messages) {
        if (message.first != Type::UDPTunnel) continue;
        Mumble::Protocol::UDPDecoder<Mumble::Protocol::Role::Client> decoder(version);
        const auto &bytes = message.second;
        if (!decoder.decode({reinterpret_cast<const Mumble::Protocol::byte *>(bytes.constData()), std::size_t(bytes.size())})
            || decoder.getMessageType() != Mumble::Protocol::UDPMessageType::Audio) return false;
        const auto p = decoder.getAudioData();
        out.append({p.senderSession, p.frameNumber, p.isLastFrame,
            QByteArray(reinterpret_cast<const char *>(p.payload.data()), int(p.payload.size()))});
    }
    return true;
}
// Decode with the receiver's actual stream boundaries. Close is now encoded
// with prior Open/Noise history, so reset-encoder byte equality is inappropriate.
static std::vector<float> decoded(const QList<Received> &packets) {
    std::vector<float> result;int error=0;
    std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(nullptr,opus_decoder_destroy);
    for(const auto &p:packets) {
        if(!decoder)decoder.reset(opus_decoder_create(48000,1,&error));
        if(!decoder || error) return {};
        std::array<float,960> pcm{};
        if(opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char *>(p.payload.constData()),p.payload.size(),pcm.data(),960,0)!=960)return {};
        for(float v:pcm) {if(!std::isfinite(v))return {};result.push_back(v);}
        if(p.last)decoder.reset();
    }
    return result;
}
static double closeCorrelation(const QList<Received> &effects) {
    const auto clip=AudioSignal::encodeWavSignal(asset("squelch-close.wav"),50);
    QList<Received> reference;
    for(const auto &bytes:clip) {Received r{};r.payload=bytes;reference.append(r);}
    const auto actual=decoded(effects),expected=decoded(reference);
    if(actual.size()<expected.size() || expected.size()<=960)return 0;
    double dot=0,a=0,b=0;
    // The first frame includes the preceding Noise encoder history/lookahead.
    // Remaining frames must contain the complete actual Close waveform.
    const auto offset=actual.size()-expected.size();
    for(std::size_t i=960;i<expected.size();++i){double x=actual[offset+i],y=expected[i];dot+=x*y;a+=x*x;b+=y*y;}
    return a>0 && b>0?dot/std::sqrt(a*b):0;
}
class TestNoiseIntegration : public QObject {
    Q_OBJECT
private slots:
    void transport_data(); void transport();
    void pacing_data(); void pacing();
    void longConfirmed();
    void shortReceiver_data(); void shortReceiver();
    void lifecycle_data(); void lifecycle();
    void reuse();
    void rapid();
    void disabled_data(); void disabled();
};
void TestNoiseIntegration::transport_data() {
    QTest::addColumn<bool>("modern"); QTest::addColumn<bool>("udp"); QTest::addColumn<int>("ending");
    for (bool modern : {false, true}) for (bool udp : {false, true}) for (int end : {0, 1, 2})
        QTest::newRow(qPrintable(QString("modern-%1-udp-%2-end-%3").arg(modern).arg(udp).arg(end))) << modern << udp << end;
}
void TestNoiseIntegration::transport() {
    QFETCH(bool, modern); QFETCH(bool, udp); QFETCH(int, ending);
    Fixture f; QVERIFY(f.start("true", ending ? 500 : 5000, 500, false,
        config() + "squelchclosetimeoutfallback=" + (ending == 2 ? "false\n" : "true\n")));
    Client a,b,c; QVERIFY(a.login(f.port,"A",modern)); QVERIFY(b.login(f.port,"B",modern)); QVERIFY(c.login(f.port,"C",modern));
    if (udp) { QVERIFY(a.udp.bind(QHostAddress(QHostAddress::LocalHost),0)); QVERIFY(a.udpBarrier()); }
    std::vector<float> pcm(960 * 9); for (std::size_t i=0;i<pcm.size();++i) pcm[i]=float(.1 * std::sin(double(i) * .03));
    const auto speech = AudioSignal::encode(pcm, 100);
    QTest::qWait(30); // Existing bandwidth budget must accrue after authentication.
    for (int i=0;i<8;++i) { QVERIFY(sendVoice(a,speech[std::size_t(i)],std::uint64_t(i*2),false,udp)); QTest::qWait(20); }
    if (udp) QVERIFY(a.udpBarrier()); else QVERIFY(a.barrier());
    auto received = b.audio(130); auto other = c.audio(30);
    auto effects = from(received,b.session); auto otherEffects = from(other,c.session);
    QVERIFY(effects.size()>3 && otherEffects.size()>3);
    QVERIFY(!effects[0].last && !effects[1].last);
    QVERIFY(!decoded(effects).empty());
    QCOMPARE(effects[2].payload,otherEffects[2].payload);
    if (!ending) {
        QVERIFY(b.speak()); QVERIFY(b.barrier()); received += b.audio(420);
        const auto busy = AudioSignal::encode(AudioSignal::syntheticBusy(),50);
        auto privatePackets = from(received,b.session);
        int first=-1; for(int i=0;i<privatePackets.size();++i) if(privatePackets[i].payload==busy[0]) { first=i; break; }
        QVERIFY(first>0); QVERIFY(privatePackets[first-1].last);
        QVERIFY(privatePackets.size()>=first+int(busy.size()));
        for(std::size_t i=0;i<busy.size();++i) QCOMPARE(privatePackets[first+int(i)].payload,busy[i]);
        QVERIFY(sendVoice(a,speech[8],16,true,udp));
        if(udp) QVERIFY(a.udpBarrier()); else QVERIFY(a.barrier());
    }
    received += b.audio(850); effects = from(received,b.session);
    const auto heard = from(received,a.session); QCOMPARE(heard.size(),ending?8:9);
    for(int i=0;i<heard.size();++i) {
        QCOMPARE(heard[i].payload,speech[std::size_t(i)]); QCOMPARE(heard[i].frame,std::uint64_t(i*2));
        QCOMPARE(heard[i].last,!ending && i==8);
    }
    const auto close = AudioSignal::encodeWavSignal(asset("squelch-close.wav"),50);
    int closes=0; std::uint64_t previous=0; bool first=true;
    int error=0; auto decoder=opus_decoder_create(48000,1,&error); QVERIFY(decoder && !error);
    for(const auto &p:effects) {
        if(!first) { QVERIFY(p.frame>previous); }
        first=false; previous=p.frame;
        std::array<float,960> decoded; QCOMPARE(opus_decode_float(decoder,
            reinterpret_cast<const unsigned char *>(p.payload.data()),p.payload.size(),decoded.data(),960,0),960);
        if(p.last) ++closes;
    }
    opus_decoder_destroy(decoder);
    QCOMPARE(closes,ending==0?3:1); QVERIFY(effects.back().last);
    if(ending!=2) QVERIFY2(closeCorrelation(effects)>.85,"Complete decoded Close missing after Noise");
    else QVERIFY(closeCorrelation(effects)<.85);
    QVERIFY(from(b.audio(150),b.session).isEmpty()); QVERIFY(from(a.audio(20),a.session).isEmpty());
}
void TestNoiseIntegration::pacing_data() {
    QTest::addColumn<bool>("udp");QTest::addColumn<bool>("flood");
    QTest::newRow("continuous-TCP")<<false<<false;
    QTest::newRow("continuous-encrypted-UDP-with-1000pps")<<true<<true;
}
void TestNoiseIntegration::pacing() {
    QFETCH(bool,udp);QFETCH(bool,flood);
    Fixture f;QVERIFY(f.start("true",5000,500,true,config()));
    Client source,receiver;QVERIFY(source.login(f.port,"TimingSource",true));
    QVERIFY(receiver.login(f.port,"TimingReceiver",false));
    if(udp){QVERIFY(source.udp.bind(QHostAddress(QHostAddress::LocalHost),0));QVERIFY(source.udpBarrier());}
    const auto voice=AudioSignal::encode(AudioSignal::syntheticBusy(),100);
    QTest::qWait(30);QElapsedTimer clock;clock.start();std::uint64_t frames=0,pings=0;
    qint64 nextVoice=0;QList<Received> all;std::vector<qint64> voiceTimes,effectTimes;
    while(clock.elapsed()<3000) {
        const auto ms=clock.elapsed();
        if(ms>=nextVoice){QVERIFY(sendVoice(source,voice[0],frames*2,false,udp));++frames;nextVoice+=20;}
        if(flood) {
            // Authenticated UDP pings exercise decrypt/read-lock traffic without
            // exceeding the unchanged voice bandwidth budget or changing speech.
            for(int n=0;n<10 && pings<std::uint64_t(ms);++n) {
                Mumble::Protocol::UDPPingEncoder<Mumble::Protocol::Role::Client> encoder(source.version);
                Mumble::Protocol::PingData ping;ping.timestamp=++pings;
                const auto bytes=encoder.encodePingPacket(ping);
                QVERIFY(source.sendEncrypted(QByteArray(reinterpret_cast<const char *>(bytes.data()),int(bytes.size()))));
            }
        }
        const auto packets=receiver.audio(0);const auto at=clock.nsecsElapsed()/1000;
        for(const auto &packet:packets) {
            all.append(packet);
            if(packet.session==receiver.session)effectTimes.push_back(at);
            if(packet.session==source.session)voiceTimes.push_back(at);
        }
        QTest::qWait(1);
    }
    const auto live=from(all,receiver.session);
    QVERIFY2(live.size()>=144,"Private stream delivered less than 96% of nominal 50-Hz audio during continuous voice");
    for(const auto &p:live)QVERIFY2(!p.last,"Private effect stream aborted during otherwise active voice");
    QVERIFY(sendVoice(source,voice[1],frames*2,true,udp));
    if(udp)QVERIFY(source.udpBarrier());else QVERIFY(source.barrier());
    all+=receiver.audio(700);
    const auto speech=from(all,source.session),effects=from(all,receiver.session);
    QCOMPARE(std::uint64_t(speech.size()),frames+1);
    for(int i=0;i<speech.size();++i) {
        QCOMPARE(speech[i].frame,std::uint64_t(i)*2);
        QCOMPARE(speech[i].payload,i==speech.size()-1?voice[1]:voice[0]);
        QCOMPARE(speech[i].last,i==speech.size()-1);
    }
    int ends=0;
    for(int i=0;i<effects.size();++i){if(i)QCOMPARE(effects[i].frame,effects[i-1].frame+2);if(effects[i].last)++ends;}
    QCOMPARE(ends,1);QVERIFY(effects.back().last);QVERIFY(closeCorrelation(effects)>.85);
    const auto maximumGap=[](const std::vector<qint64> &times){qint64 gap=0;for(std::size_t i=1;i<times.size();++i)gap=std::max(gap,times[i]-times[i-1]);return gap;};
    qInfo("Live receiver observations: speech=%d effects=%d UDP-pings=%llu max_speech_gap_us=%lld max_effect_gap_us=%lld (poll granularity ~5ms; not server send timestamps)",
        int(voiceTimes.size()),int(effectTimes.size()),static_cast<unsigned long long>(pings),
        static_cast<long long>(maximumGap(voiceTimes)),static_cast<long long>(maximumGap(effectTimes)));
    if(flood)QVERIFY(pings>=2500);
    f.server.stop();QCOMPARE(f.count("released","marker_confirmed"),1);QCOMPARE(f.count("released","timeout_suspected"),0);
}
void TestNoiseIntegration::longConfirmed() {
    // Exercise both input transports concurrently in separate real channels.
    // Short transport tests cannot catch expiry of a >60-second Noise stream.
    Fixture f; QVERIFY(f.start("true",5000,500,true,config()));
    Client admin,tcpSource,tcpReceiver,udpSource,udpReceiver;
    QVERIFY(admin.login(f.port,"SuperUser"));
    QVERIFY(tcpSource.login(f.port,"TCP-Speaker")); QVERIFY(tcpReceiver.login(f.port,"TCP-Listener"));
    QVERIFY(udpSource.login(f.port,"UDP-Speaker")); QVERIFY(udpReceiver.login(f.port,"UDP-Listener"));
    const auto udpChannel=channel(admin,"UDP"); QVERIFY(udpChannel);
    QVERIFY(udpSource.move(udpChannel)); QVERIFY(udpReceiver.move(udpChannel));
    QVERIFY(udpSource.udp.bind(QHostAddress(QHostAddress::LocalHost),0)); QVERIFY(udpSource.udpBarrier());
    const auto voice=AudioSignal::encode(AudioSignal::syntheticBusy(),100);
    QTest::qWait(30);
    QElapsedTimer elapsed; elapsed.start(); std::uint64_t frames=0;
    qint64 nextKeepalive=5000;
    while(elapsed.elapsed()<62000) {
        QVERIFY(sendVoice(tcpSource,voice[0],frames*2,false,false));
        QVERIFY(sendVoice(udpSource,voice[0],frames*2,false,true));
        ++frames; QTest::qWait(20);
        if(elapsed.elapsed()>=nextKeepalive) {
            // Real clients keep the control connection alive even while voice
            // travels over UDP. Preserve the server's normal 30-second timeout.
            for(auto *client : std::array<Client *,5>{{&admin,&tcpSource,&tcpReceiver,&udpSource,&udpReceiver}})
                QVERIFY(client->barrier());
            QVERIFY(udpSource.udpBarrier());
            nextKeepalive+=5000;
        }
    }
    QVERIFY(sendVoice(tcpSource,voice[1],frames*2,true,false));
    QVERIFY(sendVoice(udpSource,voice[1],frames*2,true,true));
    QVERIFY(tcpSource.barrier()); QVERIFY(udpSource.udpBarrier());
    const auto open=AudioSignal::encodeWavSignal(asset("squelch-open.wav"),50);
    const auto close=AudioSignal::encodeWavSignal(asset("squelch-close.wav"),50);
    const std::array<std::pair<Client *,Client *>,2> paths{{{&tcpReceiver,&tcpSource},{&udpReceiver,&udpSource}}};
    for(const auto &pair : paths) {
        const auto all=pair.first->audio(500);
        const auto heard=from(all,pair.second->session);
        QCOMPARE(std::uint64_t(heard.size()),frames+1);
        for(int i=0;i<heard.size();++i) {
            QCOMPARE(heard[i].payload,i==heard.size()-1?voice[1]:voice[0]);
            QCOMPARE(heard[i].frame,std::uint64_t(i)*2);
            QCOMPARE(heard[i].last,i==heard.size()-1);
        }
        const auto effects=from(all,pair.first->session);
        QVERIFY(effects.size()>int(open.size()+close.size()));
        int closes=0, error=0;
        std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
        QVERIFY(decoder && !error);
        for(int i=0;i<effects.size();++i) {
            if(i) QVERIFY2(effects[i].frame>effects[i-1].frame,"Private effect sequence went backwards after long Noise");
            if(effects[i].last) ++closes;
            std::array<float,960> pcm;
            QCOMPARE(opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char *>(effects[i].payload.constData()),
                                      effects[i].payload.size(),pcm.data(),960,0),960);
        }
        QCOMPARE(closes,1);
        const int first=effects.size()-int(close.size());
        QVERIFY(!effects[first-1].last);
        QCOMPARE(effects[first].frame,effects[first-1].frame+2);
        QVERIFY2(closeCorrelation(effects)>.85,"Complete Close waveform missing after long Noise");
        for(std::size_t i=0;i<close.size();++i) {
            QCOMPARE(effects[first+int(i)].last,i+1==close.size());
        }
        QVERIFY(from(pair.first->audio(150),pair.first->session).isEmpty());
    }
    f.server.stop();
    QCOMPARE(f.count("released","marker_confirmed"),2);
    QCOMPARE(f.count("released","timeout_suspected"),0);
}
void TestNoiseIntegration::shortReceiver_data() {
    QTest::addColumn<bool>("udp"); QTest::addColumn<bool>("timeout"); QTest::addColumn<int>("duration");
    QTest::newRow("2s-confirmed-TCP") << false << false << 2000;
    QTest::newRow("2s-confirmed-encrypted-UDP") << true << false << 2000;
    QTest::newRow("2s-timeout-encrypted-UDP") << true << true << 2000;
    QTest::newRow("10s-confirmed-encrypted-UDP") << true << false << 10000;
}
void TestNoiseIntegration::shortReceiver() {
    QFETCH(bool,udp); QFETCH(bool,timeout); QFETCH(int,duration);
    Fixture f; QVERIFY(f.start("true",500,500,true,config()+"squelchclosetimeoutfallback=true\n"));
    Client source,receiver; QVERIFY(source.login(f.port,"TestSource",true));
    QVERIFY(receiver.login(f.port,"LegacyReceiver",false));
    if(udp) { QVERIFY(source.udp.bind(QHostAddress(QHostAddress::LocalHost),0)); QVERIFY(source.udpBarrier()); }
    const auto voice=AudioSignal::encode(AudioSignal::syntheticBusy(),100);
    QTest::qWait(30); QElapsedTimer elapsed; elapsed.start(); std::uint64_t count=0;
    while(elapsed.elapsed()<duration) {
        QVERIFY(sendVoice(source,voice[0],count*2,false,udp)); ++count; QTest::qWait(20);
    }
    if(!timeout) QVERIFY(sendVoice(source,voice[1],count*2,true,udp)); // Windows-style final Opus frame with terminator.
    if(udp) QVERIFY(source.udpBarrier()); else QVERIFY(source.barrier());
    const auto all=receiver.audio(900);
    const auto speech=from(all,source.session);
    QCOMPARE(std::uint64_t(speech.size()),count+(timeout?0:1));
    for(std::uint64_t i=0;i<count;++i) {
        QCOMPARE(speech[int(i)].payload,voice[0]); QCOMPARE(speech[int(i)].frame,i*2); QVERIFY(!speech[int(i)].last);
    }
    if(!timeout) { QVERIFY(speech.back().last); QCOMPARE(speech.back().payload,voice[1]); }
    const auto effects=from(all,receiver.session);
    const auto close=AudioSignal::encodeWavSignal(asset("squelch-close.wav"),50);
    QVERIFY(effects.size()>int(close.size()+2));
    const int first=effects.size()-int(close.size());
    QVERIFY(!effects[first-1].last); QCOMPARE(effects[first].frame,effects[first-1].frame+2);
    QVERIFY2(closeCorrelation(effects)>.85,"Complete Close waveform missing at legacy receiver");
    int closeStarts=0;
    for(int i=0;i<effects.size();++i) {
        if(i) QVERIFY(effects[i].frame>effects[i-1].frame);
        if(effects[i].last) ++closeStarts;
        if(i) QCOMPARE(effects[i].frame,effects[i-1].frame+2);
    }
    QCOMPARE(closeStarts,1);
    for(std::size_t i=0;i<close.size();++i) {
        QCOMPARE(effects[first+int(i)].last,i+1==close.size());
    }
    QVERIFY(from(receiver.audio(100),receiver.session).isEmpty());
    f.server.stop();
    QCOMPARE(f.count("released",timeout?"timeout_suspected":"marker_confirmed"),1);
    qInfo("Private stream received: intermediate_end=0 final_close_packets=%d stream_ends=%d",int(close.size()),closeStarts);
}
void TestNoiseIntegration::lifecycle_data() {
    QTest::addColumn<int>("change"); for(int c=0;c<5;++c) QTest::newRow(qPrintable(QString::number(c))) << c;
}
void TestNoiseIntegration::lifecycle() {
    QFETCH(int,change); Fixture f; QVERIFY(f.start("true",5000,500,false,config(false)));
    Client admin,a,b; QVERIFY(admin.login(f.port,"SuperUser")); QVERIFY(a.login(f.port,"A")); QVERIFY(b.login(f.port,"B"));
    auto other=channel(admin,"Other"); QVERIFY(other);
    const auto speech=AudioSignal::encode(AudioSignal::syntheticBusy(),100);
    QTest::qWait(30);
    QVERIFY(sendVoice(a,speech[0],0,false,false)); QVERIFY(a.barrier());
    auto prior = from(b.audio(180),b.session); QVERIFY(!prior.isEmpty());
    QList<Client::Message> beforeInvalidation, afterInvalidation;
    const auto moveFresh = [&](Client &actor, unsigned destination, QList<Client::Message> &preceding) {
        const auto matches = [&](const QByteArray &bytes) {
            MumbleProto::UserState reply;
            return reply.ParseFromArray(bytes.data(),bytes.size()) && reply.session()==actor.session
                && reply.has_channel_id() && reply.channel_id()==destination;
        };
        // The ping fences earlier server replies; remove only stale matching
        // acknowledgements, preserving every audio packet for validation below.
        if (!b.barrier()) return false;
        b.discard(Type::UserState,matches);
        MumbleProto::UserState request; request.set_session(actor.session); request.set_channel_id(destination);
        QByteArray reply;
        return actor.send(Type::UserState,request) && b.receive(Type::UserState,reply,matches,4000,&preceding);
    };
    if(change==0 || change==1) {
        Client &actor = change==0 ? b : a;
        QVERIFY(moveFresh(actor,other,beforeInvalidation));
        QVERIFY(moveFresh(actor,0,afterInvalidation));
    }
    if(change==2) {
        const auto removed = [&](const QByteArray &bytes) {
            MumbleProto::UserRemove reply;
            return reply.ParseFromArray(bytes.data(),bytes.size()) && reply.session()==a.session;
        };
        QVERIFY(b.barrier()); b.discard(Type::UserRemove,removed);
        a.socket.abort(); QByteArray reply;
        QVERIFY(b.receive(Type::UserRemove,reply,removed,4000,&beforeInvalidation));
    }
    if(change==3) {
        const auto muted = [&](const QByteArray &bytes) {
            MumbleProto::UserState reply;
            return reply.ParseFromArray(bytes.data(),bytes.size()) && reply.session()==a.session
                && reply.has_mute() && reply.mute();
        };
        QVERIFY(b.barrier()); b.discard(Type::UserState,muted);
        MumbleProto::UserState m; m.set_session(a.session); m.set_mute(true);
        QVERIFY(admin.send(Type::UserState,m)); QByteArray reply;
        QVERIFY(b.receive(Type::UserState,reply,muted,4000,&beforeInvalidation));
    }
    if(change==4) {
        MumbleProto::ACL acl; acl.set_channel_id(0); acl.set_inherit_acls(false);
        auto entry=acl.add_acls(); entry->set_group("all"); entry->set_apply_here(true);
        entry->set_apply_subs(true); entry->set_deny(8); // Existing Speak permission bit.
        QVERIFY(admin.send(Type::ACL,acl)); QVERIFY(admin.barrier());
        QVERIFY(b.barrier(&beforeInvalidation));
    }
    QList<Received> before, after;
    QVERIFY(appendAudio(beforeInvalidation,b.version,before));
    QVERIFY(appendAudio(afterInvalidation,b.version,after));
    prior += from(before,b.session);
    after += b.audio(0);
    // Wait for completion, not an assumed 80-ms drain. Use the harness's existing
    // receive deadline. A receiver that was away at delivery may lose its best-
    // effort terminal; sender cancellation must close the still-present receiver.
    bool ended = std::any_of(prior.begin(),prior.end(),[](const Received &p){return p.last;})
        || std::any_of(after.begin(),after.end(),[](const Received &p){return p.last;});
    if (!ended) {
        QByteArray reply; QList<Client::Message> preceding;
        const bool arrived = b.receive(Type::UDPTunnel,reply,{},4000,&preceding);
        if (arrived) {
            preceding.append(qMakePair(Type::UDPTunnel,reply));
            QVERIFY(appendAudio(preceding,b.version,after));
        } else {
            QVERIFY2(change==0,"Missing terminal after sender invalidation");
            after += b.audio(0); // Also reject unexpected packets retained on timeout.
        }
    }
    for (const auto &packet : after) {
        QCOMPARE(packet.session,b.session);
        QVERIFY2(packet.last,"Noise payload after confirmed invalidation");
    }
    const auto stream = prior + after;
    int terminals = 0;
    for (int i=0;i<stream.size();++i) {
        const auto &packet = stream[i];
        QCOMPARE(packet.session,b.session);
        if (i) QCOMPARE(packet.frame,stream[i-1].frame+2);
        QCOMPARE(opus_packet_get_nb_samples(reinterpret_cast<const unsigned char *>(packet.payload.constData()),
            packet.payload.size(),48000),960);
        if (packet.last) { ++terminals; QCOMPARE(i,stream.size()-1); }
    }
    if (change==0) QVERIFY(terminals<=1); else QCOMPARE(terminals,1);
    QVERIFY(!decoded(stream).empty());
    QVERIFY2(b.audio(160).isEmpty(),"Audio after completed/invalidated private stream");
}
void TestNoiseIntegration::reuse() {
    Fixture f; QVERIFY(f.start("true",5000,500,false,config(false)+"users=2\n"));
    Client a,b; QVERIFY(a.login(f.port,"A")); QVERIFY(b.login(f.port,"B"));
    auto speech=AudioSignal::encode(AudioSignal::syntheticBusy(),100);
    QTest::qWait(30);
    QVERIFY(sendVoice(a,speech[0],0,false,false)); QVERIFY(a.barrier()); QVERIFY(!from(b.audio(150),b.session).isEmpty());
    const auto session=b.session; b.socket.abort();
    auto removed=[&](unsigned id) { QByteArray bytes; return a.receive(Type::UserRemove,bytes,[id](const QByteArray &data) {
        MumbleProto::UserRemove m; return m.ParseFromArray(data.data(),data.size()) && m.session()==id; }); };
    QVERIFY(removed(session)); bool reused=false;
    for(int i=0;i<6;++i) {
        Client next; QVERIFY(next.login(f.port,"Replacement"));
        if(next.session==session) { reused=true; QVERIFY(from(next.audio(180),next.session).isEmpty()); break; }
        const auto id=next.session; next.socket.abort(); QVERIFY(removed(id));
    }
    QVERIFY(reused);
}
void TestNoiseIntegration::rapid() {
    Fixture f; QVERIFY(f.start("true",5000,1,false,config()));
    Client a,b,c; QVERIFY(a.login(f.port,"A")); QVERIFY(b.login(f.port,"B")); QVERIFY(c.login(f.port,"C"));
    const auto speech=AudioSignal::encode(AudioSignal::syntheticBusy(),100);
    QTest::qWait(30);
    for(int n=0;n<4;++n) {
        Client &source = n%2 ? c : a;
        QVERIFY(sendVoice(source,speech[0],std::uint64_t(n*4),false,false)); QVERIFY(source.barrier()); QTest::qWait(100);
        QVERIFY(sendVoice(source,speech[1],std::uint64_t(n*4+2),true,false)); QVERIFY(source.barrier());
    }
    const auto all=b.audio(300); int voices=0; std::uint64_t last=0; bool first=true;
    for(const auto &packet:all) {
        if(packet.session==b.session) { if(!first) { QVERIFY(packet.frame>last); } first=false; last=packet.frame; }
        else if(packet.session==a.session || packet.session==c.session) {
            const int frame=voices/2; QCOMPARE(packet.session,frame%2?c.session:a.session);
            QCOMPARE(packet.payload,speech[std::size_t(voices%2)]); QCOMPARE(packet.last,voices%2==1); ++voices;
        }
    }
    QCOMPARE(voices,8); QVERIFY(from(b.audio(150),b.session).isEmpty());
}
void TestNoiseIntegration::disabled_data() {
    QTest::addColumn<QByteArray>("setting");
    for(const auto &s:{QByteArray("radionoise=false\n"),QByteArray("radionoisevolume=101\n"),QByteArray("radionoisevolume=0\n"),QByteArray("radionoisefile=\n"),QByteArray("radionoisefile=/nonexistent/noise.wav\n")})
        QTest::newRow(s.constData()) << s;
}
void TestNoiseIntegration::disabled() {
    QFETCH(QByteArray,setting); Fixture f; QVERIFY(f.start("true",5000,500,false,config(false)+setting));
    Client a,b; QVERIFY(a.login(f.port,"A")); QVERIFY(b.login(f.port,"B"));
    auto speech=AudioSignal::encode(AudioSignal::syntheticBusy(),100);
    QTest::qWait(30);
    QVERIFY(sendVoice(a,speech[0],0,true,false)); QVERIFY(a.barrier()); const auto data=b.audio(250);
    QCOMPARE(from(data,a.session).size(),1); QVERIFY(from(data,b.session).isEmpty());
}
QTEST_MAIN(TestNoiseIntegration)
#include "TestNoiseIntegration.moc"
