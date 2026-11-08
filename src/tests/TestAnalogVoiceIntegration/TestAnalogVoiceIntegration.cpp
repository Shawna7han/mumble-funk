#include "Harness.h"
class TestAnalogVoiceIntegration : public QObject {
	Q_OBJECT
private slots:
	void disabledEffects_data();
	void disabledEffects();
	void disabled_data();
	void disabled();
	void profiles_data();
	void profiles();
	void halfFramesAndRouting();
	void orderingAndTransport();
	void invalidAndUnsupported();
	void timeout();
	void independentChannelsAndCodecLimit();
	void senderInvalidation_data();
	void senderInvalidation();
	void emptyMarkerAndLoss();
	void bandwidthAndEffectBoundary();
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
	void blockedWorkerOverload();
	void blockedWorkerDisconnectAndShutdown();
#endif
};
void TestAnalogVoiceIntegration::disabledEffects_data() {
	QTest::addColumn< QByteArray >("config");
	QTest::newRow("missing") << QByteArray();
	QTest::newRow("false") << QByteArray("analogvoice=false\n");
}
void TestAnalogVoiceIntegration::disabledEffects() {
	QFETCH(QByteArray, config);
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false,
					config + "audiofeedback=true\nsquelch=true\nradionoise=true\nbusytone=false\nsquelchopenfile="
						+ QByteArray(B4_ASSET_DIR) + "/squelch-open.wav\nsquelchclosefile=" + QByteArray(B4_ASSET_DIR)
						+ "/squelch-close.wav\nradionoisefile=" + QByteArray(B4_ASSET_DIR) + "/radio-noise.wav\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver"));
	const auto payload = opus(960);
	QVERIFY(sendAudio(sender, payload, 0, true));
	const auto packets = receiver.audio(800, 1);
	int speech = 0, effects = 0;
	bool effectEnd = false;
	for (const auto &packet : packets) {
		if (packet.session == sender.session) {
			++speech;
			QCOMPARE(packet.payload, payload);
			QVERIFY(packet.last);
		} else {
			QCOMPARE(packet.session, receiver.session);
			++effects;
			effectEnd = effectEnd || packet.last;
		}
	}
	QCOMPARE(speech, 1);
	QVERIFY(effects > 0 && effectEnd);
}
void TestAnalogVoiceIntegration::disabled_data() {
	QTest::addColumn< QByteArray >("config");
	QTest::newRow("missing") << QByteArray();
	QTest::newRow("no-simplex") << QByteArray("analogvoice=true\nsimplex=false\n");
	QTest::newRow("false") << QByteArray("analogvoice=false\n");
	QTest::newRow("bad-profile") << QByteArray("analogvoice=true\nanalogvoiceprofile=invalid\n");
	QTest::newRow("bad-switch") << QByteArray("analogvoice=invalid\n");
}
void TestAnalogVoiceIntegration::disabled() {
	QFETCH(QByteArray, config);
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false, config));
	Client sender, old, modern;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(old.login(f.port, "old"));
	QVERIFY(modern.login(f.port, "modern", true));
	const auto payload = opus(960);
	QVERIFY(!payload.isEmpty());
	QVERIFY(sendAudio(sender, payload, 37, true));
	for (auto *receiver : { &old, &modern }) {
		Packet packet;
		QVERIFY(receiveAudio(*receiver, packet));
		QCOMPARE(packet.payload, payload);
		QCOMPARE(packet.session, sender.session);
		QCOMPARE(packet.frame, std::uint64_t(37));
		QVERIFY(packet.last && packet.positional);
		QCOMPARE(packet.position[0], 1.25f);
		QCOMPARE(packet.volume, 1.0f);
	}
}
void TestAnalogVoiceIntegration::profiles_data() {
	QTest::addColumn< QByteArray >("profile");
	QTest::addColumn< int >("samples");
	for (const auto &name : { QByteArray("cb-am"), QByteArray("business-fm"), QByteArray("narrowband") })
		for (int samples : { 480, 960, 1920, 2880, 5760 })
			QTest::newRow((name + '-' + QByteArray::number(samples)).constData()) << name << samples;
}
void TestAnalogVoiceIntegration::profiles() {
	QFETCH(QByteArray, profile);
	QFETCH(int, samples);
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false, "analogvoice=true\nanalogvoiceprofile=" + profile + '\n'));
	Client sender, old, modern;
	QVERIFY(sender.login(f.port, "sender", true));
	QVERIFY(old.login(f.port, "old"));
	QVERIFY(modern.login(f.port, "modern", true));
	const auto payload = opus(samples);
	QVERIFY(!payload.isEmpty());
	Stream reference(funkvoice::profile(profile.toStdString()));
	QVERIFY(reference.begin(100));
	Stream::Result expected;
	reference.process({ reinterpret_cast< const unsigned char * >(payload.constData()),
						static_cast< std::size_t >(payload.size()), 100, true },
					  expected);
	QVERIFY(expected.count > 0);
	QVERIFY(sendAudio(sender, payload, 100, true));
	int error = 0;
	std::unique_ptr< OpusDecoder, decltype(&opus_decoder_destroy) > decoder(opus_decoder_create(48000, 1, &error),
																			opus_decoder_destroy);
	QVERIFY(decoder && !error);
	for (std::size_t i = 0; i < expected.count; ++i) {
		Packet a, b;
		const bool received = receiveAudio(old, a);
		if (!received) {
			f.server.stop();
			qWarning().noquote() << f.server.readAll();
		}
		QVERIFY(received);
		QVERIFY(receiveAudio(modern, b));
		QCOMPARE(a.payload, b.payload);
		QCOMPARE(a.payload, QByteArray(reinterpret_cast< const char * >(expected.packets[i].payload.data()),
									   static_cast< int >(expected.packets[i].bytes)));
		QCOMPARE(a.frame, expected.packets[i].frame);
		QCOMPARE(b.frame, a.frame);
		QCOMPARE(a.session, sender.session);
		QCOMPARE(b.session, sender.session);
		QCOMPARE(a.last, i + 1 == expected.count);
		QCOMPARE(b.last, a.last);
		if (i == 0) {
			QVERIFY(a.positional && b.positional);
			QCOMPARE(a.position[0], 1.25f);
		}
		float pcm[960];
		QCOMPARE(opus_decode_float(decoder.get(), reinterpret_cast< const unsigned char * >(a.payload.constData()),
								   a.payload.size(), pcm, 960, 0),
				 960);
	}
}
void TestAnalogVoiceIntegration::halfFramesAndRouting() {
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false, "analogvoice=true\n"));
	Client sender, stays, moves, deaf, admin;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(stays.login(f.port, "stays", true));
	QVERIFY(moves.login(f.port, "moves"));
	QVERIFY(deaf.login(f.port, "deaf"));
	QVERIFY(admin.login(f.port, "SuperUser"));
	const auto other = channel(admin, "other");
	QVERIFY(other);
	const auto half = opus(480);
	QVERIFY(sendAudio(sender, half, 0, false, false, 7.0f));
	QVERIFY(sender.barrier()); // Admission precedes the following control changes.
	QVERIFY(moves.move(other) && moves.move(0));
	QVERIFY(setDeaf(deaf, true) && setDeaf(deaf, false));
	Client late;
	QVERIFY(late.login(f.port, "late"));
	QVERIFY(sendAudio(sender, half, 1, true, false, 99.0f));
	Packet first, last;
	QVERIFY(receiveAudio(stays, first));
	QVERIFY(receiveAudio(stays, last));
	QCOMPARE(first.position[0], 7.0f);
	QVERIFY(last.last);
	for (auto *receiver : { &moves, &deaf, &late }) {
		Packet packet;
		QVERIFY(!receiveAudio(*receiver, packet, false, 50));
	}
}
void TestAnalogVoiceIntegration::orderingAndTransport() {
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false, "analogvoice=true\n"));
	Client a, b, receiver;
	QVERIFY(a.login(f.port, "a", true));
	QVERIFY(b.login(f.port, "b"));
	QVERIFY(receiver.login(f.port, "receiver", true));
	// A denied target authenticates the receiver's UDP endpoint without a lease.
	QVERIFY(receiver.sendFrame(0, true, true, 1));
	QVERIFY(receiver.udpBarrier());
	const auto payload = opus(960);
	QVERIFY(sendAudio(a, payload, 0, true, true));
	QVERIFY(a.udpBarrier());
	QVERIFY(sendAudio(b, payload, 0, true));
	Packet p;
	std::uint64_t floor = 0;
	for (auto *sender : { &a, &b }) {
		for (int i = 0; i < 2; ++i) {
			QVERIFY(receiveAudio(receiver, p, true));
			QCOMPARE(p.session, sender->session);
			QCOMPARE(p.last, i == 1);
			if (sender == &a)
				floor = p.frame + 2;
		}
	}
	// Same connection, another codec lane and TCP ingress preserve its output clock.
	QVERIFY(sendAudio(a, payload, 2, true));
	QVERIFY(receiveAudio(receiver, p, true));
	QCOMPARE(p.session, a.session);
	QVERIFY(p.frame >= floor);
	QVERIFY(receiveAudio(receiver, p, true) && p.last);
}
void TestAnalogVoiceIntegration::invalidAndUnsupported() {
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false, "analogvoice=true\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver"));
	const auto shortPacket = opus(240);
	QVERIFY(!shortPacket.isEmpty());
	QVERIFY(sendAudio(sender, shortPacket, 0, true));
	Packet packet;
	QVERIFY(receiveAudio(receiver, packet));
	QCOMPARE(packet.payload, shortPacket); // Ordered whole-generation B2 bypass.
	QVERIFY(packet.last);
	QVERIFY(sendAudio(sender, QByteArray("\xff", 1), 1, true));
	QVERIFY(sender.barrier());
	QVERIFY(!receiveAudio(receiver, packet, false, 50));
	QVERIFY(sendAudio(sender, opus(960), 2, true));
	QVERIFY(receiveAudio(receiver, packet));
	QVERIFY(receiveAudio(receiver, packet) && packet.last);
	// Shutdown while data can still be on the worker.
	QVERIFY(sendAudio(sender, opus(5760), 20, false));
	f.server.stop();
	QCOMPARE(f.server.exitStatus(), QProcess::NormalExit);
}
void TestAnalogVoiceIntegration::timeout() {
	B4Fixture f;
	QVERIFY(f.start("true", 150, 3000, false, "analogvoice=true\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver"));
	QVERIFY(sendAudio(sender, opus(480), 0, false));
	Packet packet;
	QVERIFY(receiveAudio(receiver, packet));
	QVERIFY(packet.last);
}

void TestAnalogVoiceIntegration::independentChannelsAndCodecLimit() {
	B4Fixture f;
	QVERIFY(f.start("true", 5000, 5000, false, "analogvoice=true\nmessageburst=20\nautobanAttempts=0\n"));
	Client admin;
	QVERIFY(admin.login(f.port, "SuperUser"));
	std::array< Client, 5 > senders, receivers;
	for (std::size_t i = 0; i < senders.size(); ++i) {
		const auto room = channel(admin, ("room" + std::to_string(i)).c_str());
		QVERIFY2(room, qPrintable(QStringLiteral("channel %1: %2").arg(i).arg(QString::fromUtf8(f.server.readAll()))));
		QVERIFY(senders[i].login(f.port, "sender" + std::to_string(i)));
		QVERIFY(receivers[i].login(f.port, "receiver" + std::to_string(i), i < 4));
		QVERIFY(senders[i].move(room) && receivers[i].move(room));
	}
	const auto half = opus(480);
	for (auto &sender : senders) {
		QVERIFY(sendAudio(sender, half, 0, false));
		QVERIFY(sender.barrier());
	}
	Packet packet;
	QVERIFY(receiveAudio(receivers[4], packet));
	QCOMPARE(packet.payload, half); // Four occupied codecs force ordered raw admission.
	QCOMPARE(packet.session, senders[4].session);
	for (std::size_t i = 0; i < senders.size(); ++i) {
		QVERIFY(sendAudio(senders[i], {}, 0, true));
		if (!receiveAudio(receivers[i], packet)) {
			f.server.stop();
			QFAIL(qPrintable(QStringLiteral("sender %1: %2").arg(i).arg(QString::fromUtf8(f.server.readAll()))));
		}
		QCOMPARE(packet.session, senders[i].session);
		QVERIFY(packet.last);
		if (i < 4)
			QVERIFY(packet.payload != half);
		else
			QVERIFY(packet.payload.isEmpty());
	}
}
void TestAnalogVoiceIntegration::senderInvalidation_data() {
	QTest::addColumn< QByteArray >("change");
	for (const char *name : { "channel", "mute", "acl", "disconnect" })
		QTest::newRow(name) << QByteArray(name);
}
void TestAnalogVoiceIntegration::senderInvalidation() {
	QFETCH(QByteArray, change);
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false, "analogvoice=true\n"));
	Client admin, sender, receiver;
	QVERIFY(admin.login(f.port, "SuperUser"));
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver"));
	const auto room = channel(admin, "other");
	QVERIFY(room);
	QVERIFY(sendAudio(sender, opus(480), 0, false));
	QVERIFY(sender.barrier());
	if (change == "channel") {
		QVERIFY(sender.move(room) && sender.move(0));
	} else if (change == "mute") {
		MumbleProto::UserState state;
		state.set_session(sender.session);
		state.set_mute(true);
		QVERIFY(admin.send(Type::UserState, state) && admin.barrier());
		state.set_mute(false);
		QVERIFY(admin.send(Type::UserState, state) && admin.barrier());
	} else if (change == "acl") {
		MumbleProto::ACL acl;
		acl.set_channel_id(0);
		acl.set_inherit_acls(false);
		auto *rule = acl.add_acls();
		rule->set_group("all");
		rule->set_apply_here(true);
		rule->set_apply_subs(true);
		rule->set_deny(0x8); // ChanACL::Speak
		QVERIFY(admin.send(Type::ACL, acl) && admin.barrier());
		acl.clear_acls();
		QVERIFY(admin.send(Type::ACL, acl) && admin.barrier());
	} else {
		sender.socket.abort();
		QByteArray removed;
		QVERIFY(receiver.receive(Type::UserRemove, removed));
		Client replacement;
		QVERIFY(replacement.login(f.port, "replacement"));
		Packet packet;
		QVERIFY(!receiveAudio(receiver, packet, false, 50));
		QVERIFY(!receiveAudio(replacement, packet, false, 50));
		QVERIFY(sendAudio(replacement, opus(960), 0, true));
		QVERIFY(receiveAudio(receiver, packet));
		QCOMPARE(packet.session, replacement.session);
		QVERIFY(receiveAudio(receiver, packet) && packet.last);
		return;
	}
	// A marker rearms Simplex, but cannot resurrect the invalidated half frame.
	QVERIFY(sendAudio(sender, {}, 1, true) && sender.barrier());
	Packet packet;
	QVERIFY(!receiveAudio(receiver, packet, false, 50));
	QVERIFY(sendAudio(sender, opus(960), 2, true));
	QVERIFY(receiveAudio(receiver, packet));
	QCOMPARE(packet.session, sender.session);
	QVERIFY(receiveAudio(receiver, packet) && packet.last);
}
void TestAnalogVoiceIntegration::emptyMarkerAndLoss() {
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false, "analogvoice=true\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver", true));
	const auto payload = opus(960);
	Stream reference(funkvoice::profile("cb-am"));
	QVERIFY(reference.begin(0));
	std::vector< Stream::Packet > expected;
	for (const auto &input : { Stream::Input{ reinterpret_cast< const unsigned char * >(payload.constData()),
											  static_cast< std::size_t >(payload.size()), 0, false },
							   Stream::Input{ reinterpret_cast< const unsigned char * >(payload.constData()),
											  static_cast< std::size_t >(payload.size()), 6, false },
							   Stream::Input{ nullptr, 0, 6, true } }) {
		Stream::Result result;
		reference.process(input, result);
		expected.insert(expected.end(), result.packets.begin(), result.packets.begin() + result.count);
		QVERIFY(sendAudio(sender, input.bytes ? payload : QByteArray(), input.frame, input.end));
		QVERIFY(sender.barrier());
	}
	QVERIFY(!expected.empty());
	for (const auto &referencePacket : expected) {
		Packet packet;
		QVERIFY(receiveAudio(receiver, packet));
		QCOMPARE(packet.payload, QByteArray(reinterpret_cast< const char * >(referencePacket.payload.data()),
											static_cast< int >(referencePacket.bytes)));
		QCOMPARE(packet.frame, referencePacket.frame);
		QCOMPARE(packet.last, referencePacket.end != Stream::End::None);
	}
}
void TestAnalogVoiceIntegration::bandwidthAndEffectBoundary() {
	B4Fixture f;
	QVERIFY(f.start("true", 2500, 3000, false,
					"analogvoice=true\naudiofeedback=true\nsquelch=true\nradionoise=true\nbusytone=false\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver"));
	QVERIFY(sendAudio(sender, opus(960), 0, true));
	Packet packet;
	QVERIFY(receiveAudio(receiver, packet));
	QCOMPARE(packet.session, sender.session);
	QVERIFY(receiveAudio(receiver, packet) && packet.last);
	QCOMPARE(packet.session, sender.session);
	QVERIFY(!receiveAudio(receiver, packet, false, 100)); // Invalid/missing WAV assets cannot enable optional effects.
	B4Fixture limited;
	QVERIFY(limited.start("true", 2500, 3000, false, "analogvoice=true\nbandwidth=8\n"));
	Client denied, listener;
	QVERIFY(denied.login(limited.port, "denied"));
	QVERIFY(listener.login(limited.port, "listener"));
	QVERIFY(sendAudio(denied, opus(960), 0, true) && denied.barrier());
	QVERIFY(!receiveAudio(listener, packet, false, 100));
}


#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
#	include "CodecGate.h"
void TestAnalogVoiceIntegration::blockedWorkerOverload() {
	B4Fixture f;
	CodecGate gate;
	QVERIFY(gate.attach(f));
	QVERIFY(f.start("true", 5000, 5000, false, "analogvoice=true\n"));
	Client sender, receiver, successor;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver"));
	QVERIFY(successor.login(f.port, "successor"));
	const auto payload = opus(960);
	QVERIFY(sendAudio(sender, payload, 0, false));
	QVERIFY(gate.wait());
	// A round trip on the main thread completes while the codec is held. Codec
	// work therefore cannot own the voice lock or the Simplex mutex.
	QVERIFY(sender.barrier());
	for (std::uint64_t frame = 2; frame < 18; frame += 2)
		QVERIFY(sendAudio(sender, payload, frame, false));
	QVERIFY(sendAudio(sender, {}, 18, true) && sender.barrier());
	// Admit another sender while the old worker is still blocked. No output
	// may bypass the predecessor's channel barrier.
	QVERIFY(sendAudio(successor, payload, 20, true, true) && successor.udpBarrier());
	QVERIFY(receiver.audio(0).isEmpty());
	gate.resume();
	Packet packet;
	bool completed = false, seenSuccessor = false;
	for (int i = 0; i < 4 && !completed; ++i) {
		QVERIFY(receiveAudio(receiver, packet));
		QVERIFY(packet.payload != payload); // No covert raw fallback after the fault.
		if (packet.session == sender.session) {
			QVERIFY(!seenSuccessor);
		} else {
			QCOMPARE(packet.session, successor.session);
			seenSuccessor = true;
		}
		completed = seenSuccessor && packet.frame >= 20 && packet.last;
	}
	QVERIFY(completed);
	f.server.stop();
	const auto output = f.server.readAll();
	if (!output.contains(" inputFull=1 ") || f.server.exitStatus() != QProcess::NormalExit || f.server.exitCode() != 0)
		qInfo().noquote() << output;
	QCOMPARE(f.server.exitStatus(), QProcess::NormalExit);
	QCOMPARE(f.server.exitCode(), 0);
	QVERIFY2(
		output.contains(" inputFull=1 "),
		qPrintable(QStringLiteral("exitStatus=%1 exitCode=%2").arg(f.server.exitStatus()).arg(f.server.exitCode())));
}
void TestAnalogVoiceIntegration::blockedWorkerDisconnectAndShutdown() {
	B4Fixture f;
	CodecGate gate;
	QVERIFY(gate.attach(f));
	QVERIFY(f.start("true", 5000, 5000, false, "analogvoice=true\n"));
	Client sender, receiver;
	QVERIFY(sender.login(f.port, "sender"));
	QVERIFY(receiver.login(f.port, "receiver"));
	QVERIFY(sendAudio(sender, opus(960), 0, true));
	QVERIFY(gate.wait());
	sender.socket.abort();
	QByteArray removed;
	QVERIFY(receiver.receive(Type::UserRemove, removed));
	QVERIFY(receiver.barrier());
	// Shutdown must join the active codec while keeping its QObject context alive.
	// Regress the exact failure: deliver SIGTERM to the paused codec thread,
	// not whichever thread the kernel happens to choose for kill(process).
	QCOMPARE(::syscall(SYS_tgkill, static_cast< pid_t >(f.server.processId()), gate.workerThread, SIGTERM), 0L);
	gate.resume();
	QVERIFY(f.server.waitForFinished(5000));
	QCOMPARE(f.server.exitStatus(), QProcess::NormalExit);
	if (f.server.exitCode() != 0)
		qInfo().noquote() << f.server.readAll();
	QCOMPARE(f.server.exitCode(), 0);
	Packet packet;
	QVERIFY(!receiveAudio(receiver, packet, false, 20));
}
#endif

QTEST_MAIN(TestAnalogVoiceIntegration)
#include "TestAnalogVoiceIntegration.moc"
