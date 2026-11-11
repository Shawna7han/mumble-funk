// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "Harness.h"
#include <array>
#include <cmath>
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
#include "CodecGate.h"
#endif

class TestAnalogVoiceRapid : public QObject {
	Q_OBJECT
private slots:
	void bursts_data();
	void bursts();
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
	void expiredGenerationNeedsBoundary();
#endif
};

void TestAnalogVoiceRapid::bursts_data() {
	QTest::addColumn<bool>("analog");
	QTest::addColumn<int>("transport"); // TCP, UDP, alternating ingress + two speakers.
	QTest::addColumn<int>("count");
	QTest::addColumn<bool>("marker");
	for (bool analog : {false, true})
		for (int transport : {0, 1, 2}) {
			for (int count : {10, 50})
				QTest::newRow(qPrintable(QString("%1-%2-%3-marker").arg(analog).arg(transport).arg(count)))
					<< analog << transport << count << true;
			QTest::newRow(qPrintable(QString("%1-%2-10-timeout").arg(analog).arg(transport)))
				<< analog << transport << 10 << false;
		}
}

void TestAnalogVoiceRapid::bursts() {
	QFETCH(bool, analog);
	QFETCH(int, transport);
	QFETCH(int, count);
	QFETCH(bool, marker);
	// No marker + alternating speakers would legitimately trigger Simplex Busy.
	const bool twoSpeakers = marker && transport == 2;
	B4Fixture f;
	const QByteArray assets(B4_ASSET_DIR);
	QVERIFY(f.start("true", 500, 500, true,
		QByteArray("analogvoice=") + (analog ? "true" : "false")
		+ "\nanalogvoiceprofile=cb-am\naudiofeedback=true\nbusytone=true\nsquelch=true\n"
		  "squelchclosetimeoutfallback=true\nradionoise=true\nbusytonevolume=100\n"
		  "squelchopenvolume=80\nsquelchclosevolume=80\nfeedbackboostdb=8\nradionoisevolume=20\n"
		+ "busytonefile=" + assets + "/busy-tone.wav\nsquelchopenfile=" + assets
		+ "/squelch-open.wav\nsquelchclosefile=" + assets + "/squelch-close.wav\nradionoisefile="
		+ assets + "/radio-noise.wav\n"));
	Client a, b, legacy, modern;
	QVERIFY(a.login(f.port, "mumla", true));
	if (twoSpeakers) QVERIFY(b.login(f.port, "second", true));
	QVERIFY(legacy.login(f.port, "windows"));
	const bool multiple = count == 50;
	if (multiple) QVERIFY(modern.login(f.port, "modern", true));
	const bool receiveUdp = transport != 0;
	if (receiveUdp) {
		QVERIFY(legacy.udp.bind(QHostAddress(QHostAddress::LocalHost), 0));
		QVERIFY(legacy.udpBarrier());
	}
	std::array<Client *, 2> receivers{{&legacy, &modern}};
	std::array<std::array<std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)>, 2>, 2> decoders{{
		{{{nullptr, opus_decoder_destroy}, {nullptr, opus_decoder_destroy}}},
		{{{nullptr, opus_decoder_destroy}, {nullptr, opus_decoder_destroy}}}}};
	std::array<std::array<std::uint64_t, 2>, 2> lastFrame{};
	std::array<std::array<bool, 2>, 2> seen{};
	const std::array<int, 5> pauses{{50, 100, 200, 300, 500}};
	const std::array<int, 5> durations{{480, 960, 1920, 2880, 5760}};
	int submitted = 0;
	for (int burst = 0; burst < count; ++burst) {
		Client &sender = twoSpeakers && burst % 2 ? b : a;
		const std::size_t speaker = &sender == &b ? 1 : 0;
		const int samples = durations[std::size_t(burst) % durations.size()];
		const auto payload = opus(samples);
		const std::uint64_t first = 1000 + std::uint64_t(burst) * 100;
		const bool udp = transport == 1 || (transport == 2 && burst % 2);
		std::array<QList<Received>, 2> received;
		auto collect = [&](int milliseconds) {
			QElapsedTimer timer; timer.start();
			do {
				for (std::size_t r = 0; r < (multiple ? 2U : 1U); ++r) {
					received[r] += receivers[r]->audio(0);
					if (r == 0 && receiveUdp) {
						Packet packet;
						while (receiveAudio(*receivers[r], packet, true, 0))
							received[r].append({packet.session, packet.frame, packet.last, packet.payload});
					}
				}
				QTest::qWait(1);
			} while (timer.elapsed() < milliseconds);
		};
		// An unmarked 30-ms burst legitimately remains in the B2 half-frame +
		// lookahead + reserve until more source audio or the 500-ms timeout.
		const int packets = !marker && samples == 480 ? 5 : 3;
		for (int p = 0; p < packets; ++p) {
			QVERIFY(sendAudio(sender, payload, first + std::uint64_t(p * samples / 480), marker && p + 1 == packets, udp));
			++submitted;
			collect(samples / 48); // Media-cadence comparison; TestAnalogVoiceBurst covers coalesced arrivals.
		}
		collect(pauses[std::size_t(burst) % pauses.size()]);
		for (std::size_t r = 0; r < (multiple ? 2U : 1U); ++r) {
			int speech = 0;
			for (const auto &packet : received[r]) {
				if (packet.session == receivers[r]->session) continue; // Existing private effect stream.
				QCOMPARE(packet.session, sender.session);
				if (packet.frame < first) continue; // Ordered predecessor reserve/PLC, not this PTT.
				++speech;
				if (seen[r][speaker]) QVERIFY(packet.frame > lastFrame[r][speaker]);
				seen[r][speaker] = true; lastFrame[r][speaker] = packet.frame;
				if (!decoders[r][speaker]) {
					int error = 0; decoders[r][speaker].reset(opus_decoder_create(48000, 1, &error));
					QCOMPARE(error, OPUS_OK);
				}
				std::array<float, 5760> pcm{};
				const int n = opus_decode_float(decoders[r][speaker].get(),
					reinterpret_cast<const unsigned char *>(packet.payload.constData()), packet.payload.size(),
					pcm.data(), int(pcm.size()), 0);
				QVERIFY(n > 0);
				double energy = 0;
				for (std::size_t i = 0; i < std::size_t(n); ++i) { QVERIFY(std::isfinite(pcm[i])); energy += pcm[i] * pcm[i]; }
				QVERIFY(energy > 0);
				if (!analog) QCOMPARE(packet.payload, payload);
				if (packet.last) opus_decoder_ctl(decoders[r][speaker].get(), OPUS_RESET_STATE);
			}
			if (!speech) {
				f.server.stop();
				qWarning().noquote() << "Missing PTT" << burst << "recipient" << r << "source frame" << first
					<< "samples" << samples << "pause" << pauses[std::size_t(burst) % pauses.size()]
					<< "UDP" << udp << f.server.readAll();
			}
			QVERIFY(speech > 0); // Actual received AND decoded audio, not just a Simplex grant.
		}
	}
	f.server.stop();
	const auto output = f.server.readAll();
	if (analog) {
		const auto summary = output.mid(std::max(0, output.lastIndexOf("Analog voice stopped:")));
		QVERIFY2(output.contains(" inputFull=0 ") && output.contains(" expired=0 ")
			&& output.contains(" codecErrors=0 ") && output.contains(" noLane=0 "), summary.constData());
		QVERIFY2(output.contains("accepted=" + QByteArray::number(submitted) + ' '), summary.constData());
		// The existing logger deliberately uses a lossy try-lock queue. Exact
		// admission totals come from service counters, never diagnostic coverage.
		QVERIFY(f.count("analog_submit", "accepted") > 0);
		QVERIFY(f.count("analog_result", "ready") > 0);
		QVERIFY(f.count("analog_handoff", "local_handoff") > 0);
		QVERIFY(f.count("analog_discard") == 0);
		for (const auto &event : f.events("simplex")) {
			if (!event.value("event").toString().startsWith("analog_")) continue;
			QVERIFY(event.value("generation").toString().toULongLong() > 0);
			QVERIFY(event.value("routing_epoch").toString().toULongLong() > 0);
			QVERIFY(!event.contains("payload"));
		}
	}
	QCOMPARE(f.count("denied", "busy"), 0);
}
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
void TestAnalogVoiceRapid::expiredGenerationNeedsBoundary() {
	B4Fixture f;
	CodecGate gate;
	QVERIFY(gate.attach(f));
	QVERIFY(f.start("true", 500, 500, true, "analogvoice=true\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "mumla", true));
	QVERIFY(receiver.login(f.port, "windows"));
	const auto payload = opus(960);
	QVERIFY(sendAudio(sender, payload, 0, false));
	QVERIFY(gate.wait());
	QTest::qWait(60); // Intentionally exceed the unchanged 40-ms job budget.
	gate.resume();
	QVERIFY(sender.barrier());
	receiver.audio(80); // Drain any fault reserve; it is not successful new speech.
	QVERIFY(sendAudio(sender, payload, 10, false));
	QVERIFY(sendAudio(sender, payload, 12, false));
	QVERIFY(sender.barrier());
	QVERIFY(receiver.audio(80).isEmpty()); // Simplex accepts; this generation stays faulted.
	// Only a real existing boundary/rearm permits a fresh generation. No raw
	// fallback, artificial release, or restart inside the failed transmission.
	receiver.audio(550);
	QVERIFY(sendAudio(sender, payload, 100, true));
	Packet packet;
	QVERIFY(receiveAudio(receiver, packet));
	QCOMPARE(packet.session, sender.session);
	QVERIFY(packet.frame >= 100);
	QVERIFY(packet.payload != payload);
	f.server.stop();
	QCOMPARE(f.count("granted"), 2);
	QCOMPARE(f.count("denied"), 0);
	QVERIFY(f.count("analog_result", "expired") > 0);
	const auto output = f.server.readAll();
	QVERIFY2(output.contains(" faulted=2 "), output.constData());
	QVERIFY(f.count("analog_submit", "faulted") > 0);
	QVERIFY(f.count("analog_handoff", "local_handoff") > 0);
}
#endif
QTEST_MAIN(TestAnalogVoiceRapid)
#include "TestAnalogVoiceRapid.moc"
