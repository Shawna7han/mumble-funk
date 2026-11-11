// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "AnalogVoiceService.h"
#include "Harness.h"
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
#	include "CodecGate.h"
#endif

namespace {
QByteArray settings(const QString &profile, bool analog = true) {
	const QByteArray assets(B4_ASSET_DIR);
	return QByteArray("analogvoice=") + (analog ? "true" : "false") + "\nanalogvoiceprofile=" + profile.toUtf8()
		   + "\naudiofeedback=true\nbusytone=true\nsquelch=true\nsquelchclosetimeoutfallback=true\nradionoise=true\n"
		   + "busytonefile=" + assets + "/busy-tone.wav\nsquelchopenfile=" + assets + "/squelch-open.wav\n"
		   + "squelchclosefile=" + assets + "/squelch-close.wav\nradionoisefile=" + assets + "/radio-noise.wav\n";
}
// One TLS write deliberately coalesces complete voice messages. This exercises
// Connection::socketRead's buffered-message loop, not a paced approximation.
bool burst(Client &client, const QByteArray &payload, int samples, std::uint64_t first, int count, bool marker,
		   bool empty, bool udp, QByteArray *deferred = nullptr) {
	QByteArray combined;
	Mumble::Protocol::UDPAudioEncoder< Mumble::Protocol::Role::Client > encoder(client.version);
	for (int p = 0; p < count + int(empty); ++p) {
		Mumble::Protocol::AudioData audio;
		if (p < count)
			audio.payload = { reinterpret_cast< const unsigned char * >(payload.constData()),
							  std::size_t(payload.size()) };
		audio.frameNumber = first + std::uint64_t(p * samples / 480);
		audio.isLastFrame = empty ? p == count : marker && p + 1 == count;
		const auto bytes  = encoder.encodeAudioPacket(audio);
		const QByteArray wire(reinterpret_cast< const char * >(bytes.data()), int(bytes.size()));
		if (udp) {
			if (!client.sendEncrypted(wire))
				return false;
		} else {
			QByteArray header(6, '\0');
			qToBigEndian< quint16 >(static_cast< quint16 >(Type::UDPTunnel), header.data());
			qToBigEndian< quint32 >(quint32(wire.size()), header.data() + 2);
			combined += header;
			combined += wire;
		}
	}
	if (deferred && !udp) {
		*deferred += combined;
		return true;
	}
	return udp || (client.socket.write(combined) == combined.size() && client.socket.flush());
}
QList< Received > collect(Client &receiver, int milliseconds, bool udp = false) {
	QList< Received > result;
	QElapsedTimer timer;
	timer.start();
	do {
		result += receiver.audio(0);
		if (udp) {
			Packet packet;
			while (receiveAudio(receiver, packet, true, 0))
				result.append({ packet.session, packet.frame, packet.last, packet.payload });
		}
		QTest::qWait(1);
	} while (timer.elapsed() < milliseconds);
	return result;
}
QList< Received > speech(const QList< Received > &packets, unsigned int session) {
	QList< Received > result;
	for (const auto &packet : packets)
		if (packet.session == session)
			result.append(packet);
	return result;
}
bool decodes(const QList< Received > &packets) {
	int error = OPUS_OK;
	std::unique_ptr< OpusDecoder, decltype(&opus_decoder_destroy) > decoder(opus_decoder_create(48000, 1, &error),
																			opus_decoder_destroy);
	if (!decoder || error || packets.isEmpty())
		return false;
	double energy = 0;
	for (const auto &packet : packets) {
		if (packet.payload.isEmpty() && packet.last)
			continue;
		std::array< float, 5760 > pcm{};
		const int n =
			opus_decode_float(decoder.get(), reinterpret_cast< const unsigned char * >(packet.payload.constData()),
							  packet.payload.size(), pcm.data(), int(pcm.size()), 0);
		if (n <= 0)
			return false;
		for (int i = 0; i < n; ++i) {
			if (!std::isfinite(pcm[std::size_t(i)]))
				return false;
			energy += pcm[std::size_t(i)] * pcm[std::size_t(i)];
		}
	}
	return energy > 0;
}
} // namespace

class TestAnalogVoiceBurst : public QObject {
	Q_OBJECT
private slots:
	void capacity_data();
	void capacity();
	void pauses_data();
	void pauses();
	void codecReason_data();
	void codecReason();
	void modernEmptyMarker();
	void sameConnectionPendingTail();
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
	void faultRecovery_data();
	void faultRecovery();
#endif
};
void TestAnalogVoiceBurst::capacity_data() {
	QTest::addColumn< QString >("profile");
	QTest::addColumn< int >("samples");
	QTest::addColumn< int >("transport");
	for (const QString &profile : { QString("cb-am"), QString("business-fm"), QString("narrowband"), QString("off") })
		for (int samples : { 480, 960, 1920, 2880, 5760 })
			for (int transport = 0; transport < 4; ++transport) {
				if (profile == "off" && samples != 960)
					continue;
				QTest::newRow(qPrintable(QString("%1-%2-%3").arg(profile).arg(samples).arg(transport)))
					<< profile << samples << transport;
			}
}
void TestAnalogVoiceBurst::capacity() {
	QFETCH(QString, profile);
	QFETCH(int, samples);
	QFETCH(int, transport);
	const bool analog = profile != "off";
	B4Fixture f;
	QVERIFY(f.start("true", 500, 500, true, settings(analog ? profile : "cb-am", analog)));
	Client sender, receiver, modern;
	// Empty terminators are supported by legacy ingress. The modern decoder
	// explicitly rejects empty opus_data, including terminators (unchanged B4).
	QVERIFY(sender.login(f.port, "legacy-sender"));
	QVERIFY(receiver.login(f.port, "legacy"));
	QVERIFY(modern.login(f.port, "modern", true));
	QVERIFY(sender.udp.bind(QHostAddress(QHostAddress::LocalHost), 0));
	QVERIFY(receiver.udp.bind(QHostAddress(QHostAddress::LocalHost), 0));
	QVERIFY(receiver.udpBarrier());
	const auto payload = opus(samples);
	const int count    = int(funkvoice::AnalogVoiceService::InputSamples) / samples;
	for (int turn = 0; turn < 2; ++turn) {
		const bool udp = transport == 1 || (transport == 2 && turn) || (transport == 3 && !turn);
		// Fence the selected connection before sending the actual audio over
		// that ingress. No priming speech or speculative generation is needed.
		if (udp)
			QVERIFY(sender.udpBarrier());
		else
			QVERIFY(sender.barrier());
		const std::uint64_t first = 1000 + std::uint64_t(turn) * 100;
		QVERIFY(burst(sender, payload, samples, first, count, !analog, analog, udp));
		const auto legacy = speech(collect(receiver, 80, true), sender.session);
		const auto newer  = speech(collect(modern, 20), sender.session);
		if (legacy.isEmpty() || newer.isEmpty()) {
			f.server.stop();
			qWarning().noquote() << f.server.readAll();
		}
		QVERIFY(decodes(legacy));
		QVERIFY(decodes(newer));
		QCOMPARE(legacy.size(), newer.size());
		QVERIFY(legacy.last().last);
		QList< Received > reference;
		if (analog) {
			Stream stream(funkvoice::profile(profile.toStdString()));
			QVERIFY(stream.begin(first));
			for (int p = 0; p <= count; ++p) {
				Stream::Result result;
				stream.process({ p == count ? nullptr : reinterpret_cast< const unsigned char * >(payload.constData()),
								 p == count ? 0U : std::size_t(payload.size()),
								 first + std::uint64_t(p * samples / 480), p == count },
							   result);
				QCOMPARE(result.error, Stream::Error::None);
				for (std::size_t i = 0; i < result.count; ++i) {
					const auto &packet = result.packets[i];
					reference.append(
						{ sender.session, packet.frame, packet.end != Stream::End::None,
						  QByteArray(reinterpret_cast< const char * >(packet.payload.data()), int(packet.bytes)) });
				}
			}
			QCOMPARE(legacy.size(), reference.size());
		} else
			QCOMPARE(legacy.size(), count);
		for (int i = 0; i < legacy.size(); ++i) {
			QCOMPARE(legacy[i].payload, newer[i].payload);
			QCOMPARE(legacy[i].frame, newer[i].frame);
			if (i)
				QVERIFY(legacy[i].frame >= legacy[i - 1].frame + std::uint64_t(analog ? 2 : 0));
			if (analog) {
				QCOMPARE(legacy[i].payload, reference[i].payload);
				QCOMPARE(legacy[i].frame, reference[i].frame);
				QCOMPARE(legacy[i].last, reference[i].last);
			} else
				QCOMPARE(legacy[i].payload, payload);
		}
	}
	f.server.stop();
	const auto output = f.server.readAll();
	if (analog) {
		QVERIFY2(output.contains(" inputFull=0 ") && output.contains(" expired=0 ")
					 && output.contains(" codecErrors=0 "),
				 output.constData());
		QVERIFY2(output.contains("accepted=" + QByteArray::number(2 * (count + 1)) + ' '), output.constData());
	}
}
void TestAnalogVoiceBurst::pauses_data() {
	QTest::addColumn< int >("pause");
	QTest::addColumn< int >("ending");
	QTest::addColumn< int >("transport");
	int index = 0;
	for (int pause : { 50, 100, 200, 300, 500, 1000 }) {
		for (int ending = 0; ending < 3; ++ending)
			QTest::newRow(qPrintable(QString("pause-%1-end-%2").arg(pause).arg(ending)))
				<< pause << ending << (index + ending) % 4;
		++index;
	}
}
void TestAnalogVoiceBurst::pauses() {
	QFETCH(int, pause);
	QFETCH(int, ending);
	QFETCH(int, transport);
	B4Fixture f;
	QVERIFY(f.start("true", 500, 500, true, settings("cb-am")));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "mumla", true));
	QVERIFY(receiver.login(f.port, "windows"));
	QVERIFY(sender.udp.bind(QHostAddress(QHostAddress::LocalHost), 0));
	const auto payload = opus(960);
	for (int turn = 0; turn < 4; ++turn) {
		const bool udp = transport == 1 || (transport == 2 && turn % 2) || (transport == 3 && !(turn % 2));
		if (udp)
			QVERIFY(sender.udpBarrier());
		else
			QVERIFY(sender.barrier());
		const std::uint64_t first = 1000 + std::uint64_t(turn) * 100;
		QVERIFY(burst(sender, payload, 960, first, 5, ending == 0, false, udp));
		const auto packets = speech(collect(receiver, ending == 2 ? std::max(650, pause) : pause), sender.session);
		QList< Received > current;
		for (const auto &packet : packets)
			if (packet.frame >= first)
				current.append(packet);
		if (current.isEmpty()) {
			f.server.stop();
			qWarning().noquote() << f.server.readAll();
		}
		QVERIFY(decodes(current));
		for (int i = 1; i < packets.size(); ++i)
			QVERIFY(packets[i].frame > packets[i - 1].frame);
		if (ending == 0 || ending == 2)
			QVERIFY(current.last().last);
		// A short physical release without a marker is not a confirmed boundary.
		if (ending == 1 && pause < 500)
			for (const auto &packet : current)
				QVERIFY(!packet.last);
	}
	f.server.stop();
	const auto output = f.server.readAll();
	QVERIFY2(output.contains("accepted=20 ") && output.contains(" inputFull=0 ") && output.contains(" expired=0 ")
				 && output.contains(" codecErrors=0 ") && output.contains(" faulted=0 "),
			 output.constData());
}
void TestAnalogVoiceBurst::codecReason_data() {
	QTest::addColumn< bool >("malformed");
	QTest::newRow("overlapping-monotone-frames") << false;
	QTest::newRow("valid-duration-invalid-packet") << true;
}
void TestAnalogVoiceBurst::codecReason() {
	QFETCH(bool, malformed);
	B4Fixture f;
	QVERIFY(f.start("true", 500, 500, true, "analogvoice=true\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver"));
	const auto payload = opus(960);
	QVERIFY(sendAudio(sender, payload, 0, false));
	QVERIFY(sendAudio(sender, payload, 2, false));
	Packet packet;
	QVERIFY(receiveAudio(receiver, packet));
	QVERIFY(sendAudio(sender, malformed ? QByteArray::fromHex("faff") : payload, malformed ? 4 : 3, false));
	QVERIFY(sender.barrier());
	collect(receiver, 50);
	QVERIFY(sendAudio(sender, payload, 10, false));
	QVERIFY(sender.barrier());
	QVERIFY(speech(collect(receiver, 30), sender.session).isEmpty());
	QVERIFY(sendAudio(sender, {}, 12, true));
	QVERIFY(sender.barrier());
	QVERIFY(sendAudio(sender, payload, 100, true));
	const auto fresh = speech(collect(receiver, 50), sender.session);
	QVERIFY(decodes(fresh));
	for (const auto &out : fresh)
		QVERIFY(out.frame >= 100);
	f.server.stop();
	const auto output = f.server.readAll();
	QVERIFY2(output.contains(" codecErrors=1 ") && output.contains(" inputFull=0 "), output.constData());
	QVERIFY2(output.contains(malformed ? "codecStream=1 " : "codecIgnored=1 "), output.constData());
	for (const auto &event : f.events("simplex")) {
		if (event.value("reason") != "codec" || event.value("codec_stage") == "none")
			continue;
		// Root cause belongs to the failing result; a later empty terminal batch
		// reports the latched failure without inventing another codec operation.
		QCOMPARE(event.value("codec_stage").toString(), malformed ? QString("stream") : QString("ignored"));
		QCOMPARE(event.value("stream_error").toString(), malformed ? QString("invalid_payload") : QString("none"));
		QCOMPARE(event.value("codec_error").toInt(), malformed ? OPUS_INVALID_PACKET : OPUS_OK);
	}
}
void TestAnalogVoiceBurst::sameConnectionPendingTail() {
	B4Fixture f;
	QVERIFY(f.start("true", 500, 500, true, settings("cb-am")));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "mumla", true));
	QVERIFY(receiver.login(f.port, "windows"));
	const auto payload = opus(960);
	QByteArray combined;
	QVERIFY(burst(sender, payload, 960, 0, 1, true, false, false, &combined));
	QVERIFY(burst(sender, payload, 960, 100, 5, true, false, false, &combined));
	// Both generations enter the same socketRead turn. The predecessor's
	// queued dispatcher cannot run inside that callback; the worker also
	// preserves the same connection's output floor until its terminal take.
	QVERIFY(sender.socket.write(combined) == combined.size() && sender.socket.flush());
	const auto all = speech(collect(receiver, 100), sender.session);
	QList< Received > first, next;
	bool successor = false;
	for (const auto &packet : all) {
		if (packet.frame >= 100) {
			successor = true;
			next.append(packet);
		} else {
			QVERIFY(!successor);
			first.append(packet);
		}
	}
	QVERIFY(decodes(first));
	QVERIFY(decodes(next));
	QVERIFY(first.last().last && next.last().last);
	QCOMPARE(first.size(), 2);
	QCOMPARE(next.size(), 6);
	f.server.stop();
	const auto output = f.server.readAll();
	QVERIFY2(output.contains("accepted=6 ") && output.contains(" inputFull=0 ") && output.contains(" expired=0 ")
				 && output.contains(" codecErrors=0 ") && output.contains(" faulted=0 "),
			 output.constData());
}
void TestAnalogVoiceBurst::modernEmptyMarker() {
	B4Fixture f;
	QVERIFY(f.start("true", 500, 500, true, "analogvoice=true\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "modern", true));
	QVERIFY(receiver.login(f.port, "receiver"));
	const auto payload = opus(960);
	QVERIFY(burst(sender, payload, 960, 0, 5, false, false, false));
	QVERIFY(decodes(speech(collect(receiver, 30), sender.session)));
	// The unchanged protobuf decoder rejects this before Simplex/service. It
	// cannot be used as a confirmed boundary, in either the direct or analog path.
	QVERIFY(sendAudio(sender, {}, 10, true));
	QVERIFY(sender.barrier());
	QVERIFY(speech(collect(receiver, 20), sender.session).isEmpty());
	QVERIFY(sendAudio(sender, payload, 12, true));
	const auto ended = speech(collect(receiver, 30), sender.session);
	QVERIFY(decodes(ended));
	QVERIFY(ended.last().last);
	QVERIFY(sendAudio(sender, payload, 100, true));
	const auto fresh = speech(collect(receiver, 30), sender.session);
	QVERIFY(decodes(fresh));
	for (const auto &packet : fresh)
		QVERIFY(packet.frame >= 100);
	f.server.stop();
	const auto output = f.server.readAll();
	QVERIFY2(output.contains("accepted=7 ") && output.contains(" codecErrors=0 ") && output.contains(" faulted=0 "),
			 output.constData());
}
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
void TestAnalogVoiceBurst::faultRecovery_data() {
	QTest::addColumn< bool >("marker");
	QTest::addColumn< int >("transport");
	for (bool marker : { false, true })
		for (int transport = 0; transport < 4; ++transport)
			QTest::newRow(qPrintable(QString("end-%1-transport-%2").arg(marker).arg(transport))) << marker << transport;
}
void TestAnalogVoiceBurst::faultRecovery() {
	QFETCH(bool, marker);
	QFETCH(int, transport);
	B4Fixture f;
	CodecGate gate;
	QVERIFY(gate.attach(f));
	QVERIFY(f.start("true", 500, 500, true, "analogvoice=true\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "mumla", true));
	QVERIFY(receiver.login(f.port, "windows"));
	QVERIFY(sender.udp.bind(QHostAddress(QHostAddress::LocalHost), 0));
	const bool firstUdp = transport == 1 || transport == 3;
	if (firstUdp)
		QVERIFY(sender.udpBarrier());
	const auto payload = opus(960);
	QVERIFY(sendAudio(sender, payload, 0, false, firstUdp));
	QVERIFY(gate.wait());
	// Six queued 20-ms packets fill the unchanged sample budget. The seventh
	// is genuine overload even with all 10-ms packet slots available.
	QVERIFY(burst(sender, payload, 960, 2, 8, false, false, firstUdp));
	if (firstUdp)
		QVERIFY(sender.udpBarrier());
	else
		QVERIFY(sender.barrier());
	const bool nextUdp = transport == 1 || transport == 2;
	if (nextUdp)
		QVERIFY(sender.udpBarrier());
	QVERIFY(sendAudio(sender, payload, 18, false, nextUdp));
	QVERIFY(sendAudio(sender, payload, 20, false, nextUdp));
	if (nextUdp)
		QVERIFY(sender.udpBarrier());
	else
		QVERIFY(sender.barrier());
	QVERIFY(receiver.audio(0).isEmpty());
	if (marker) {
		QVERIFY(sendAudio(sender, payload, 22, true, nextUdp));
		if (nextUdp)
			QVERIFY(sender.udpBarrier());
		else
			QVERIFY(sender.barrier());
	}
	gate.resume();
	collect(receiver, marker ? 50 : 650);
	QVERIFY(sendAudio(sender, payload, 100, true, nextUdp));
	const auto fresh = speech(collect(receiver, 80), sender.session);
	QVERIFY(decodes(fresh));
	for (const auto &packet : fresh) {
		QVERIFY(packet.frame >= 100);
		QVERIFY(packet.payload != payload);
	}
	QVERIFY(fresh.last().last);
	f.server.stop();
	const auto output = f.server.readAll();
	QVERIFY2(output.contains(" inputFull=1 ") && output.contains(" codecErrors=0 "), output.constData());
	QVERIFY2(output.contains(marker ? " faulted=4 " : " faulted=3 "), output.constData());
	QVERIFY2(output.contains("packetLimitHits=0 sampleLimitHits=1 "), output.constData());
}
#endif
QTEST_MAIN(TestAnalogVoiceBurst)
#include "TestAnalogVoiceBurst.moc"
