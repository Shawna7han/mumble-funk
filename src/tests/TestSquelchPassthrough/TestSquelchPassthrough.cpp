// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
// Phase 2A has NO on-air Squelch dispatcher. These tests protect unchanged voice
// and Busy with real, decodable Opus and actual authenticated transports.
#include "../TestSimplexIntegration/Harness.h"
#include "AudioSignal.h"
#include <array>
#include <cmath>
#include <memory>
#include <opus.h>
using namespace SimplexTest;

static bool sendOpus(Client &client, const QByteArray &payload, std::uint64_t frame, bool last, bool udp) {
	Mumble::Protocol::AudioData audio;
	audio.frameNumber=frame; audio.isLastFrame=last; audio.targetOrContext=0;
	audio.payload={reinterpret_cast<const Mumble::Protocol::byte *>(payload.constData()),std::size_t(payload.size())};
	Mumble::Protocol::UDPAudioEncoder<Mumble::Protocol::Role::Client> encoder(client.version);
	const auto encoded=encoder.encodeAudioPacket(audio);
	const QByteArray bytes(reinterpret_cast<const char *>(encoded.data()),int(encoded.size()));
	return udp ? client.sendEncrypted(bytes) : client.sendRaw(Type::UDPTunnel,bytes);
}
static QList<Received> receiveUDP(Client &client,int count) {
	QList<Received> result; QElapsedTimer timer; timer.start();
	while (timer.elapsed()<2000 && result.size()<count) {
		if (!client.udp.hasPendingDatagrams()) client.udp.waitForReadyRead(10);
		while (client.udp.hasPendingDatagrams()) {
			QByteArray encrypted(int(client.udp.pendingDatagramSize()),'\0');
			if (client.udp.readDatagram(encrypted.data(),encrypted.size()) != encrypted.size() || encrypted.size()<5) return {};
			QByteArray plain(encrypted.size()-4,'\0');
			if (!client.crypt.decrypt(reinterpret_cast<const unsigned char *>(encrypted.constData()),
				reinterpret_cast<unsigned char *>(plain.data()),unsigned(encrypted.size()))) return {};
			Mumble::Protocol::UDPDecoder<Mumble::Protocol::Role::Client> decoder(client.version);
			if (!decoder.decode({reinterpret_cast<const Mumble::Protocol::byte *>(plain.constData()),std::size_t(plain.size())})) return {};
			if (decoder.getMessageType()!=Mumble::Protocol::UDPMessageType::Audio) continue;
			const auto a=decoder.getAudioData();
			result.append({a.senderSession,a.frameNumber,a.isLastFrame,
				QByteArray(reinterpret_cast<const char *>(a.payload.data()),int(a.payload.size()))});
		}
	}
	return result;
}
class TestSquelchPassthrough : public QObject {
	Q_OBJECT
private slots:
	void unchangedVoice_data();
	void unchangedVoice();
};
void TestSquelchPassthrough::unchangedVoice_data() {
	QTest::addColumn<bool>("modern"); QTest::addColumn<bool>("udpIn"); QTest::addColumn<bool>("udpOut");
	for (bool modern : {false,true}) for (bool in : {false,true}) for (bool out : {false,true})
		QTest::newRow(qPrintable(QString("%1-in-%2-out-%3").arg(modern ? "protobuf" : "legacy").arg(in ? "UDP" : "TCP").arg(out ? "UDP" : "TCP"))) << modern << in << out;
}
void TestSquelchPassthrough::unchangedVoice() {
	QFETCH(bool,modern); QFETCH(bool,udpIn); QFETCH(bool,udpOut);
	Fixture f; QVERIFY(f.start("true",5000,5000,true,"audiofeedback=true\n"));
	Client admin,a,b,observer,outsider;
	QVERIFY(admin.login(f.port,"SuperUser",modern)); QVERIFY(a.login(f.port,"Speaker",modern));
	QVERIFY(b.login(f.port,"Rejected",modern)); QVERIFY(observer.login(f.port,"Listener",modern));
	QVERIFY(outsider.login(f.port,"OtherChannel",modern));
	const auto other=channel(admin,"Other"); QVERIFY(other!=0); QVERIFY(outsider.move(other));
	if (udpIn) { QVERIFY(a.udp.bind(QHostAddress(QHostAddress::LocalHost),0)); QVERIFY(a.udpBarrier()); }
	if (udpOut) { QVERIFY(observer.udp.bind(QHostAddress(QHostAddress::LocalHost),0)); QVERIFY(observer.udpBarrier()); }
	std::vector<float> pcm(960*8);
	for (std::size_t i=0;i<pcm.size();++i) pcm[i]=float(.15*std::sin(2*3.141592653589793*317*double(i)/48000));
	const auto voice=AudioSignal::encode(pcm,100); QCOMPARE(voice.size(),std::size_t(8));
	// Model a real capture cadence. Instant bursts immediately after login can
	// exceed the unchanged server bandwidth gate, especially on fast UDP paths.
	for (int i=0;i<4;++i) {
		QTest::qWait(20);
		QVERIFY(sendOpus(a,voice[std::size_t(i)],std::uint64_t(i*2),false,udpIn));
	}
	if (udpIn) QVERIFY(a.udpBarrier()); else QVERIFY(a.barrier());
	auto heard=udpOut ? receiveUDP(observer,4) : observer.audio(80,4); if (heard.size()!=4) { f.server.stop(); for (const auto &event : f.events("simplex")) qInfo().noquote()<<QJsonDocument(event).toJson(QJsonDocument::Compact); }
	QCOMPARE(heard.size(),4);
	b.audio(); QVERIFY(b.speak()); QVERIFY(b.barrier());
	const auto busy=b.audio(400,13); QCOMPARE(busy.size(),13);
	for (int i=0;i<busy.size();++i) { QCOMPARE(busy[i].session,b.session); QCOMPARE(busy[i].frame,std::uint64_t(i*2)); QCOMPARE(busy[i].last,i==12); }
	for (int i=4;i<8;++i) {
		QTest::qWait(20);
		QVERIFY(sendOpus(a,voice[std::size_t(i)],std::uint64_t(i*2),i==7,udpIn));
	}
	if (udpIn) QVERIFY(a.udpBarrier()); else QVERIFY(a.barrier());
	heard += udpOut ? receiveUDP(observer,4) : observer.audio(80,4); QCOMPARE(heard.size(),8);
	int error=0;
	std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> expected(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
	std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> received(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
	QVERIFY(expected && received); QCOMPARE(error,OPUS_OK);
	for (int i=0;i<heard.size();++i) {
		QCOMPARE(heard[i].session,a.session); QCOMPARE(heard[i].frame,std::uint64_t(i*2)); QCOMPARE(heard[i].last,i==7);
		QCOMPARE(heard[i].payload,voice[std::size_t(i)]);
		std::array<float,960> x{},y{};
		QCOMPARE(opus_decode_float(expected.get(),reinterpret_cast<const unsigned char *>(voice[std::size_t(i)].constData()),voice[std::size_t(i)].size(),x.data(),960,0),960);
		QCOMPARE(opus_decode_float(received.get(),reinterpret_cast<const unsigned char *>(heard[i].payload.constData()),heard[i].payload.size(),y.data(),960,0),960);
		QVERIFY(x==y); // Including the first and last decoded samples, not only packet counts.
	}
	QVERIFY(a.audio().isEmpty()); QVERIFY(outsider.audio().isEmpty());
	QVERIFY(observer.audio().isEmpty());
	if (udpOut) { QTest::qWait(40); QVERIFY(!observer.udp.hasPendingDatagrams()); }
	// A blocked continuous attempt never inherits the newly released channel.
	QVERIFY(b.speak()); QVERIFY(b.barrier()); QVERIFY(observer.audio().isEmpty());
	if (udpOut) { QTest::qWait(40); QVERIFY(!observer.udp.hasPendingDatagrams()); }
	f.server.stop();
	QCOMPARE(f.count("denied", "not_eligible"), 0);
}
QTEST_MAIN(TestSquelchPassthrough)
#include "TestSquelchPassthrough.moc"
