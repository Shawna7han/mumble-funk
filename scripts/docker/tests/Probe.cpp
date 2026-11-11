// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by the BSD-style license in LICENSE.
#include "../../../src/tests/TestSimplexIntegration/Harness.h"
#include "AudioSignal.h"
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <opus.h>
using namespace SimplexTest;

bool login(Client &client, quint16 port, const std::string &name, const std::string &password) {
 client.port=port;
 client.version=Version::fromComponents(1,5,0);
 QObject::connect(&client.socket,QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),[&client] {client.socket.ignoreSslErrors();});
 client.socket.connectToHostEncrypted(QStringLiteral("127.0.0.1"),port);
 if(!client.socket.waitForEncrypted(5000)) return false;
 MumbleProto::Version version; version.set_version_v1(0x10500);
 MumbleProto::Authenticate auth; auth.set_username(name); auth.set_password(password); auth.set_opus(true);
 QByteArray bytes;
 if(!client.send(Type::Version,version) || !client.send(Type::Authenticate,auth) || !client.receive(Type::ServerSync,bytes)) return false;
 MumbleProto::ServerSync sync;
 if(!sync.ParseFromArray(bytes.data(),bytes.size())) return false;
 client.session=sync.session();
 return client.crypt.isValid();
}

// Exercise the unmodified installer configuration, including its 500-ms lease.
// Expected clips use the shipped WAVs at volume 50 and boost 8, never packet counts
// inferred from the server. Owner keepalives prevent artificial timeout releases.
static void installedAudio(Client &a, Client &b, Client &observer, bool muted) {
 auto check=[](bool ok,const char *message) {if(!ok) {std::cerr<<message<<std::endl;std::exit(1);}};
 auto clip=[&](const char *name,bool busy) {
  const QString path=QStringLiteral(AUDIO_ASSET_DIR)+"/"+name;
  return busy?AudioSignal::encode(AudioSignal::loadWav(path),muted?0:50,8):AudioSignal::encodeWavSignal(path,muted?0:50,8);
 };
 auto verify=[&](const QList<Received> &packets,const std::vector<QByteArray> &expected,unsigned session) {
  if(std::size_t(packets.size())!=expected.size()) std::cerr<<"Audio session "<<session<<": received "<<packets.size()<<", expected "<<expected.size()<<std::endl;
  check(std::size_t(packets.size())==expected.size(),"Installed audio missing or wrong length");
  int error=0;
  std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
  check(bool(decoder) && error==OPUS_OK,"Installed audio decoder failed");
  for(int i=0;i<packets.size();++i) {
   check(packets[i].session==session && packets[i].payload==expected[std::size_t(i)],"Installed audio identity, volume or boost changed");
   check(packets[i].last==(i+1==packets.size()),"Installed audio terminator changed");
   if(i) check(packets[i].frame==packets[i-1].frame+2,"Installed audio sequence invalid");
   std::array<float,960> pcm{};
   check(opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char*>(packets[i].payload.data()),packets[i].payload.size(),pcm.data(),960,0)==960,"Installed audio undecodable");
   for(float sample:pcm) check(std::isfinite(sample) && std::abs(sample)<1 && (!muted || std::abs(sample)<1e-7f),"Installed audio level unsafe or mute failed");
  }
 };
 std::vector<float> samples(960);
 for(std::size_t i=0;i<samples.size();++i) samples[i]=float(.1*std::sin(2*3.141592653589793*300*double(i)/48000));
 const auto voice=AudioSignal::encode(samples,100).front();
 std::uint64_t sequence=0;
 auto speak=[&](bool udp=false,bool last=false) {
  Mumble::Protocol::AudioData audio;audio.frameNumber=sequence;sequence+=2;audio.isLastFrame=last;audio.targetOrContext=0;
  audio.payload={reinterpret_cast<const Mumble::Protocol::byte*>(voice.data()),std::size_t(voice.size())};
  Mumble::Protocol::UDPAudioEncoder<Mumble::Protocol::Role::Client> encoder(a.version);
  const auto packet=encoder.encodeAudioPacket(audio);
  const QByteArray bytes(reinterpret_cast<const char*>(packet.data()),int(packet.size()));
  return udp?a.sendEncrypted(bytes):a.sendRaw(Type::UDPTunnel,bytes);
 };
 auto cues=[&](const QList<Received> &packets,unsigned session) {
  QList<Received> result;
  for(const auto &packet:packets) {
   if(packet.session==session) result.append(packet);
   else check(packet.session==a.session && packet.payload==voice,"Speech modified or wrong recipient");
  }
  return result;
 };
 // Match the existing real-Opus tests: allow the fresh bandwidth window to age.
 QTest::qWait(30);
 check(speak() && a.barrier(),"Installed owner speech failed");
 const auto first=observer.audio(150,3);
 int voices=0;for(const auto &p:first) if(p.session==a.session) {++voices;check(!p.last && p.payload==voice,"First speech changed");}
 if(voices!=1) {std::cerr<<"First voices="<<voices<<", packets="<<first.size()<<", owner="<<a.session<<", observer="<<observer.session<<std::endl;for(const auto &p:first) std::cerr<<"session="<<p.session<<" frame="<<p.frame<<" last="<<p.last<<std::endl;}
 check(voices==1,"First speech missing");
 const auto open=cues(first,observer.session);verify(open,clip("squelch-open.wav",false),observer.session);
 b.audio();check(b.speak(true) && b.udpBarrier(),"Installed encrypted UDP rejection failed");
 QList<Received> busy;QElapsedTimer timer;timer.start();
 do {
  check(speak(),"Installed owner keepalive failed");
  busy.append(cues(b.audio(20),b.session));
 } while(timer.elapsed()<1600 && (muted || busy.size()<61));
 verify(busy,muted?std::vector<QByteArray>{}:clip("busy-tone.wav",true),b.session);
 check(a.audio().isEmpty() && cues(observer.audio(),observer.session).isEmpty(),"Busy leaked or owner lost Simplex lease");
 check(speak(true,true) && a.udpBarrier(),"Installed encrypted UDP end failed");
 const auto ending=observer.audio(200);
 voices=0;for(const auto &p:ending) if(p.session==a.session) {++voices;check(p.last && p.payload==voice,"Last speech changed");}
 check(voices==1,"Last speech missing");
 const auto close=cues(ending,observer.session);verify(close,clip("squelch-close.wav",false),observer.session);
 check(close.first().frame>open.last().frame,"Close sequence reused Open");
 check(a.audio().isEmpty(),"Feedback sent to original sender");
 check(observer.audio(650).isEmpty(),"Confirmed end produced a second timeout Close");
 check(speak() && a.barrier(),"Timeout stream start failed");
 const auto restarted=observer.audio(150,3);
 verify(cues(restarted,observer.session),clip("squelch-open.wav",false),observer.session);
 voices=0;for(const auto &p:restarted) if(p.session==a.session) {++voices;check(!p.last && p.payload==voice,"Timeout speech changed");}
 check(voices==1,"Timeout speech missing");
 const auto expired=observer.audio(800);
 const auto timeoutClose=cues(expired,observer.session);
 check(expired.size()==timeoutClose.size(),"Timeout fabricated a speech end marker");
 verify(timeoutClose,clip("squelch-close.wav",false),observer.session);
 check(timeoutClose.first().frame>close.last().frame,"Timeout Close reused sequence");
 check(observer.audio(600).isEmpty(),"Repeated timeout duplicated Close");
 std::cout<<"Installed audio defaults verified: Open, Close, Busy, boost8, volume50; TCP/encrypted UDP and Simplex"<<(muted?"; volume0 silent":"")<<std::endl;
}
// Exercise the published container, its persistent imported WAV and the SAME
// server engine as native builds. These are protocol/decoder tests, not hearing tests.
static void radioNoise(Client &a, Client &b, Client &receiver) {
 auto check=[](bool ok,const char *message){if(!ok){std::cerr<<message<<std::endl;std::exit(1);}};
 const auto voice=AudioSignal::encode(AudioSignal::syntheticBusy(),100).front();
 const auto close=AudioSignal::encodeWavSignal(QStringLiteral(AUDIO_ASSET_DIR)+"/squelch-close.wav",50,8);
 const auto busy=AudioSignal::encode(AudioSignal::loadWav(QStringLiteral(AUDIO_ASSET_DIR)+"/busy-tone.wav"),50,8);
 auto decode=[&](const QList<Received> &packets){
  int error=0;std::vector<float> result;
  std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
  check(decoder && !error,"Radio decoder initialization failed");
  for(const auto &p:packets){std::array<float,960> pcm{};
   check(opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char*>(p.payload.constData()),p.payload.size(),pcm.data(),960,0)==960,"Radio stream undecodable");
   for(float value:pcm){check(std::isfinite(value)&&std::abs(value)<1,"Radio sample invalid");result.push_back(value);}
  }return result;
 };
 QList<Received> reference;for(const auto &packet:close)reference.append({0,0,false,packet});
 const auto expected=decode(reference);
 std::uint64_t frame=0,previousEnd=0;
 for(int test=0;test<4;++test){
  const bool udp=test%2,timeout=test>=2;const int duration=test==3?62000:2000;
  if(udp){
   if(a.udp.state()==QAbstractSocket::UnconnectedState)check(a.udp.bind(QHostAddress(QHostAddress::LocalHost),0),"Radio UDP bind failed");
   if(receiver.udp.state()==QAbstractSocket::UnconnectedState)check(receiver.udp.bind(QHostAddress(QHostAddress::LocalHost),0),"Receiver UDP bind failed");
   check(a.udpBarrier()&&receiver.udpBarrier(),"Encrypted UDP connectivity failed");
  }
  auto read=[&](int ms){
   auto all=receiver.audio(ms);
   while(receiver.udp.state()==QAbstractSocket::BoundState && receiver.udp.hasPendingDatagrams()){
    QByteArray encrypted(int(receiver.udp.pendingDatagramSize()),'\0');
    check(receiver.udp.readDatagram(encrypted.data(),encrypted.size())==encrypted.size()&&encrypted.size()>4,"Receiver UDP read failed");
    QByteArray plain(encrypted.size()-4,'\0');
    check(receiver.crypt.decrypt(reinterpret_cast<const unsigned char*>(encrypted.constData()),reinterpret_cast<unsigned char*>(plain.data()),unsigned(encrypted.size())),"Effect UDP authentication failed");
    Mumble::Protocol::UDPDecoder<Mumble::Protocol::Role::Client> decoder(receiver.version);
    check(decoder.decode({reinterpret_cast<const Mumble::Protocol::byte*>(plain.constData()),std::size_t(plain.size())}),"Effect UDP decode failed");
    if(decoder.getMessageType()==Mumble::Protocol::UDPMessageType::Audio){const auto p=decoder.getAudioData();
     all.append({p.senderSession,p.frameNumber,p.isLastFrame,QByteArray(reinterpret_cast<const char*>(p.payload.data()),int(p.payload.size()))});}
   }return all;
  };
  auto speak=[&](bool last){Mumble::Protocol::AudioData audio;audio.frameNumber=frame;frame+=2;audio.isLastFrame=last;audio.targetOrContext=0;
   audio.payload={reinterpret_cast<const Mumble::Protocol::byte*>(voice.constData()),std::size_t(voice.size())};
   Mumble::Protocol::UDPAudioEncoder<Mumble::Protocol::Role::Client> encoder(a.version);const auto bytes=encoder.encodeAudioPacket(audio);
   QByteArray packet(reinterpret_cast<const char*>(bytes.data()),int(bytes.size()));return udp?a.sendEncrypted(packet):a.sendRaw(Type::UDPTunnel,packet);
  };
  QTest::qWait(30);QElapsedTimer timer;timer.start();qint64 due=0,nextKeepalive=5000;int count=0;bool rejected=false;
  const auto startFrame=frame;QList<Received> all,bob;
  while(timer.elapsed()<duration){
   if(timer.elapsed()>=due){check(speak(false),"Continuous container speech send failed");++count;due+=20;}
   if(test==0&&!rejected&&timer.elapsed()>=400){check(b.speak(true)&&b.udpBarrier(),"Busy rejection failed");rejected=true;}
   // Real clients keep TLS control alive even when media travels over UDP.
   if(timer.elapsed()>=nextKeepalive){
    check(a.barrier()&&b.barrier()&&receiver.barrier(),"Radio control keepalive failed");
    if(udp)check(a.udpBarrier(),"Radio UDP keepalive failed");nextKeepalive+=5000;
   }
   all+=read(0);bob+=b.audio(0);QTest::qWait(1);
  }
  if(!timeout){check(speak(true),"Confirmed container terminator failed");++count;}
  check(udp?a.udpBarrier():a.barrier(),"Container voice fence failed");
  all+=read(900);bob+=b.audio(0);
  QList<Received> effects;int heard=0;
  for(const auto &p:all){
   if(p.session==receiver.session)effects.append(p);
   else {check(p.session==a.session&&p.payload==voice,"Speech changed or Busy leaked to wrong receiver");
    check(p.frame==startFrame+std::uint64_t(heard)*2,"Speech sequence missing");++heard;
    check(p.last==(!timeout&&heard==count),"Speech end flag changed");}
  }
  check(heard==count,"Original speech missing");
  check(effects.size()>duration/25&&effects.size()>int(close.size()+1),"Continuous radio stream incomplete");
  check(!effects.first().last&&effects.last().last,"Radio final marker missing");
  if(test)check(effects.first().frame>previousEnd,"Radio stream reused old sequence");
  for(int i=0;i<effects.size();++i){check(effects[i].last==(i+1==effects.size()),"Intermediate radio terminator");
   if(i)check(effects[i].frame==effects[i-1].frame+2,"Radio sequence gap or reset");}
  previousEnd=effects.last().frame;
  const auto actual=decode(effects);check(actual.size()>=expected.size(),"Close too short");
  const auto offset=actual.size()-expected.size();double dot=0,x2=0,y2=0;
  for(std::size_t i=960;i<expected.size();++i){double x=actual[offset+i],y=expected[i];dot+=x*y;x2+=x*x;y2+=y*y;}
  check(x2>0&&y2>0&&dot/std::sqrt(x2*y2)>.85,"Complete decoded Close missing after container Noise");
  if(test==0){int first=-1;for(int i=0;i<bob.size();++i)if(bob[i].session==b.session&&bob[i].payload==busy.front()){first=i;break;}
   check(first>0&&bob[first-1].last,"Busy did not safely interrupt private effect");
   check(bob.size()>=first+int(busy.size()),"Busy incomplete");
   for(std::size_t i=0;i<busy.size();++i)check(bob[first+int(i)].payload==busy[i]&&bob[first+int(i)].last==(i+1==busy.size()),"Busy priority or bytes changed");}
  check(a.audio(0).isEmpty(),"Private radio effects sent to original sender");
  std::cout<<"PASS: container radio "<<duration<<"ms "<<(udp?"encrypted UDP":"TCP")<<(timeout?" timeout":" confirmed")<<"; speech="<<heard<<" effects="<<effects.size()<<"; one final marker, decoded Close"<<std::endl;
 }
 std::cout<<"Container Open/Noise/Close verified; TCP + encrypted UDP, Busy and unchanged speech"<<std::endl;
}

int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 if(argc<2) return 2;
 std::string password; std::getline(std::cin,password);
 const auto port=static_cast<quint16>(QString::fromLocal8Bit(argv[1]).toUInt());
 auto check=[](bool ok,const char *message) {if(!ok) {std::cerr<<message<<std::endl;std::exit(1);}};
 const bool feedback = argc > 2 && QString::fromLocal8Bit(argv[2]) == "--audio-feedback";
 bool validCount=true;
 const int expectedPackets=argc>3?QString::fromLocal8Bit(argv[3]).toInt(&validCount):13;
 check(validCount && expectedPackets>=1 && expectedPackets<=64,"Invalid expected Busy packet count");
 Client a,b,observer;
 check(login(a,port,"DockerAlice",password),"TCP/TLS login A failed");
 check(login(b,port,"DockerBob",password),"TCP/TLS login B failed");
 check(login(observer,port,"DockerObserver",password),"TCP/TLS observer login failed");
 if(argc>2 && QString::fromLocal8Bit(argv[2])=="--radio-noise") {radioNoise(a,b,observer);return 0;}
 if(argc>2 && (QString::fromLocal8Bit(argv[2])=="--audio-defaults" || QString::fromLocal8Bit(argv[2])=="--audio-defaults-muted")) {
  installedAudio(a,b,observer,QString::fromLocal8Bit(argv[2])=="--audio-defaults-muted");return 0;
 }
 check(a.speak(),"TCP send failed"); check(observer.expect(a),"TCP voice not forwarded"); b.audio();
 check(b.speak(true) && b.udpBarrier(),"Encrypted UDP voice/ping failed");
 check(observer.expect(b,false),"Blocked UDP speech leaked");
 if(feedback) {
  const auto tone=b.audio(std::max(400,expectedPackets*20+400),expectedPackets);
  check(tone.size()==expectedPackets,"Private busy tone missing or wrong duration");
  int error=0;
  std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
  check(bool(decoder) && error==OPUS_OK,"Cannot initialize feedback decoder");
  for(int i=0;i<tone.size();++i) {
   check(tone[i].session==b.session,"Busy tone has wrong recipient identity");
   check(tone[i].frame==static_cast<std::uint64_t>(i*2),"Busy tone frame sequence changed");
   check(tone[i].last==(i+1==expectedPackets),"Busy tone end marker missing");
   check(!tone[i].payload.isEmpty() && tone[i].payload!=audioContent,"Busy tone payload invalid");
   std::array<float,960> samples{};
   check(opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char*>(tone[i].payload.data()),tone[i].payload.size(),samples.data(),960,0)==960,"Busy tone not decodable");
   for(float value:samples) check(std::isfinite(value) && std::abs(value)<1,"Busy tone decoded level unsafe");
  }
  check(a.audio().isEmpty() && observer.audio().isEmpty(),"Private busy tone leaked to channel");
 }

 check(a.speak(false,true) && observer.expect(a,true,true),"Owner terminator not forwarded");
 check(b.speak() && observer.expect(b,false),"Rejected sender automatically took over");
 check(b.speak(false,true) && b.barrier(),"Rejected end marker failed");
 check(b.speak(true) && b.udpBarrier() && observer.expect(b),"Encrypted UDP voice did not acquire released channel");
 check(b.speak(true,true) && b.udpBarrier() && observer.expect(b,true,true),"UDP terminator failed");
 if(feedback) std::cout<<"Private busy tone verified; ";
 std::cout<<"Published TCP + encrypted UDP speech and Simplex verified"<<std::endl;
 return 0;
}
