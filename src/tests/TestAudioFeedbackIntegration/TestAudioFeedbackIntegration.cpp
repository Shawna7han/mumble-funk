// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#include "../TestSimplexIntegration/Harness.h"
#include <array>
#include <cmath>
#include <cstring>
#include <opus.h>
#include "AudioSignal.h"
#include <memory>
using namespace SimplexTest;
class TestAudioFeedbackIntegration : public QObject {
	Q_OBJECT
private slots:
	void standardAsset_data();
	void standardAsset();
	void privateTone_data();
	void privateTone();
	void disabled_data();
	void disabled();
	void cancelAndReconnect();
	void independentChannels();
	void customTone_data();
	void customTone();
	void legacyNonceIsNotProtocolVersion();
	void reusedSession();
};
void TestAudioFeedbackIntegration::standardAsset_data() {
    QTest::addColumn<bool>("udp"); QTest::addColumn<bool>("modern"); QTest::addColumn<int>("mode");
    for(bool udp:{false,true}) for(bool modern:{false,true}) for(int mode:{0,1,2})
        QTest::newRow(qPrintable(QString("asset-%1-%2-mode%3").arg(udp?"UDP":"TCP").arg(modern?"protobuf":"legacy").arg(mode))) << udp << modern << mode;
}
void TestAudioFeedbackIntegration::standardAsset() {
    QFETCH(bool,udp);QFETCH(bool,modern);QFETCH(int,mode);
    const int boost=mode?12:0, volume=mode==2?0:100;
    const auto path=QStringLiteral(BUSY_ASSET_DIR)+"/busy-tone.wav";
    Fixture f; QVERIFY(f.start("true",5000,5000,false,"audiofeedback=true\nbusytonefile="+path.toUtf8()+"\nbusytonevolume="+QByteArray::number(volume)+"\nfeedbackboostdb="+QByteArray::number(boost)+"\n"));
    Client admin,a,b,observer,outsider;
    QVERIFY(admin.login(f.port,"SuperUser",modern));QVERIFY(a.login(f.port,"Owner",modern));
    QVERIFY(b.login(f.port,"Rejected",modern));QVERIFY(observer.login(f.port,"Listener",modern));QVERIFY(outsider.login(f.port,"Other",modern));
    const auto other=channel(admin,"Other");QVERIFY(other);QVERIFY(outsider.move(other));
    QVERIFY(a.speak());QVERIFY(observer.expect(a));b.audio();admin.audio();
    QVERIFY(b.speak(udp));if(udp) QVERIFY(b.udpBarrier());else QVERIFY(b.barrier());
    const auto packets=b.audio(1600,mode==2?-1:61);
    if(mode==2) QVERIFY(packets.isEmpty());
    else {
        const auto expected=AudioSignal::encode(AudioSignal::loadWav(path),100,boost);
        QCOMPARE(expected.size(),std::size_t(61));QCOMPARE(packets.size(),61);
        int error=0;
        std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000,1,&error),opus_decoder_destroy);QVERIFY(decoder);
        for(int i=0;i<packets.size();++i) {
            QCOMPARE(packets[i].session,b.session);QCOMPARE(packets[i].frame,std::uint64_t(i*2));QCOMPARE(packets[i].last,i==60);
            QCOMPARE(packets[i].payload,expected[std::size_t(i)]);
            std::array<float,960> pcm{};
            QCOMPARE(opus_decode_float(decoder.get(),reinterpret_cast<const unsigned char*>(packets[i].payload.data()),packets[i].payload.size(),pcm.data(),960,0),960);
            for(float sample:pcm) QVERIFY(std::isfinite(sample) && std::abs(sample)<1);
        }
    }
    QVERIFY(a.audio().isEmpty());QVERIFY(observer.audio().isEmpty());QVERIFY(outsider.audio().isEmpty());
    QVERIFY(a.speak(false,true));QVERIFY(observer.expect(a,true,true));b.audio();
    QVERIFY(b.speak(udp));if(udp) QVERIFY(b.udpBarrier());else QVERIFY(b.barrier());
    QVERIFY(observer.expect(b,false)); // No automatic channel takeover, even after a long Busy.
    QVERIFY(b.speak(udp,true));if(udp) QVERIFY(b.udpBarrier());else QVERIFY(b.barrier());
    b.audio();QVERIFY(b.speak(udp));if(udp) QVERIFY(b.udpBarrier());else QVERIFY(b.barrier());
    QVERIFY(observer.expect(b)); // Explicit release/rearm still permits speech.
}
void TestAudioFeedbackIntegration::privateTone_data() {
	QTest::addColumn< bool >("udp");
	QTest::addColumn< bool >("modern");
	QTest::newRow("legacy-TCP") << false << false;
	QTest::newRow("legacy-UDP") << true << false;
	QTest::newRow("protobuf-TCP") << false << true;
	QTest::newRow("protobuf-UDP") << true << true;
}
void TestAudioFeedbackIntegration::privateTone() {
	QFETCH(bool, udp);
	QFETCH(bool, modern);
	Fixture f;
	QVERIFY(f.start("true", 5000, 5000, false, "voicediagnostics=false\naudiofeedback=true\n"));
	Client a, b, observer;
	QVERIFY(a.login(f.port, "Owner", modern));
	QVERIFY(b.login(f.port, "Rejected", modern));
	QVERIFY(observer.login(f.port, "Observer", modern));
	QVERIFY(a.speak());
	QVERIFY(observer.expect(a));
	b.audio();
	QVERIFY(b.speak(udp));
	if (udp)
		QVERIFY(b.udpBarrier());
	else
		QVERIFY(b.barrier());
	// Keep rejected input active while feedback arrives (no physical key simulation claim).
	for (int i = 0; i < 8; ++i) {
		QVERIFY(b.speak(udp));
		QTest::qWait(15);
	}
	const auto tone = b.audio(400, 13);
	QCOMPARE(tone.size(), 13);
	int error            = 0;
	OpusDecoder *decoder = opus_decoder_create(48000, 1, &error);
	QCOMPARE(error, OPUS_OK);
	double energy = 0;
	for (int i = 0; i < tone.size(); ++i) {
		QCOMPARE(tone[i].session, b.session);
		QCOMPARE(tone[i].frame, static_cast< std::uint64_t >(i * 2));
		QCOMPARE(tone[i].last, i == 12);
		QVERIFY(tone[i].payload != audioContent);
		std::array< float, 960 > pcm{};
		QCOMPARE(opus_decode_float(decoder, reinterpret_cast< const unsigned char * >(tone[i].payload.constData()),
								   tone[i].payload.size(), pcm.data(), 960, 0),
				 960);
		for (float value : pcm)
			energy += value * value;
	}
	opus_decoder_destroy(decoder);
	QVERIFY(energy > 1);
	QVERIFY(a.audio().isEmpty());
	QVERIFY(observer.audio().isEmpty());
	QVERIFY(a.speak());
	QVERIFY(observer.expect(a)); // owner never displaced
	b.audio();
	QVERIFY(a.speak(false, true));
	QVERIFY(observer.expect(a, true, true));
	b.audio();
	QVERIFY(b.speak(udp));
	QVERIFY(observer.expect(b, false)); // continuous loser never inherits ownership
	QVERIFY(b.speak(udp, true));
	if (udp)
		QVERIFY(b.udpBarrier());
	else
		QVERIFY(b.barrier());
	QVERIFY(b.speak(udp));
	QVERIFY(observer.expect(b)); // existing marker/retry rule preserved
	f.server.stop();
	QVERIFY(f.events("simplex").isEmpty());
	QVERIFY(f.events("voice").isEmpty());
}
void TestAudioFeedbackIntegration::disabled_data() {
	QTest::addColumn< QByteArray >("config");
	QTest::addColumn< bool >("simplex");
	QTest::newRow("default-off") << QByteArray() << true;
	QTest::newRow("explicit-off") << QByteArray("audiofeedback=false\n") << true;
	QTest::newRow("busy-off") << QByteArray("audiofeedback=true\nbusytone=false\n") << true;
	QTest::newRow("zero-volume") << QByteArray("audiofeedback=true\nbusytonevolume=0\n") << true;
	QTest::newRow("simplex-off") << QByteArray("audiofeedback=true\n") << false;
}
void TestAudioFeedbackIntegration::disabled() {
	QFETCH(QByteArray, config);
	QFETCH(bool, simplex);
	Fixture f;
	QVERIFY(f.start(simplex ? "true" : "false", 5000, 5000, false, config));
	Client a, b, c;
	QVERIFY(a.login(f.port, "A"));
	QVERIFY(b.login(f.port, "B"));
	QVERIFY(c.login(f.port, "C"));
	QVERIFY(a.speak());
	QVERIFY(c.expect(a));
	b.audio();
	QVERIFY(b.speak());
	QVERIFY(b.barrier());
	QVERIFY(b.audio(400).isEmpty());
	QVERIFY(c.expect(b, !simplex));
}
void TestAudioFeedbackIntegration::cancelAndReconnect() {
	Fixture f;
	QVERIFY(f.start("true", 5000, 5000, false, "audiofeedback=true\n"));
	Client admin, a, b;
	QVERIFY(admin.login(f.port, "SuperUser"));
	QVERIFY(a.login(f.port, "A"));
	QVERIFY(b.login(f.port, "B"));
	const auto other = channel(admin, "Other");
	QVERIFY(other != 0);
	QVERIFY(a.move(0));
	QVERIFY(a.speak());
	b.audio();
	QVERIFY(b.speak());
	QVERIFY(b.barrier());
	// Drain at least one scheduled frame, then moving cancels active and pending work.
	QVERIFY(!b.audio(25, 1).isEmpty());
	QVERIFY(b.move(other));
	b.audio(40);
	QVERIFY(b.audio(350).isEmpty());
	QVERIFY(b.move(0));
	QVERIFY(b.speak(false, true));
	QVERIFY(b.barrier());
	QTest::qWait(1100);
	QVERIFY(a.speak());
	b.audio();
	QVERIFY(b.speak());
	QVERIFY(b.barrier());
	QVERIFY(!b.audio(25, 1).isEmpty());
	b.socket.abort();
	QTest::qWait(40);
	Client replacement;
	QVERIFY(replacement.login(f.port, "Replacement"));
	QVERIFY(
		replacement.audio(400).isEmpty()); // no old tone delivered on reconnect; explicit reuse is tested separately
}
void TestAudioFeedbackIntegration::independentChannels() {
	Fixture f;
	QVERIFY(f.start("true", 5000, 5000, false, "audiofeedback=true\n"));
	Client admin, a, b, c, d;
	QVERIFY(admin.login(f.port, "SuperUser"));
	QVERIFY(a.login(f.port, "A"));
	QVERIFY(b.login(f.port, "B"));
	QVERIFY(c.login(f.port, "C"));
	QVERIFY(d.login(f.port, "D"));
	const auto other = channel(admin, "Other");
	QVERIFY(other != 0);
	QVERIFY(a.move(0));
	QVERIFY(c.move(other));
	QVERIFY(d.move(other));
	QVERIFY(a.speak());
	b.audio();
	QVERIFY(c.speak());
	d.audio();
	// Different ingress paths are processed concurrently; feedback remains recipient-specific.
	QVERIFY(b.speak(true));
	QVERIFY(d.speak());
	QVERIFY(b.udpBarrier());
	QVERIFY(d.barrier());
	const auto left = b.audio(350, 13), right = d.audio(350, 13);
	QCOMPARE(left.size(), 13);
	QCOMPARE(right.size(), 13);
	for (const auto &packet : left)
		QCOMPARE(packet.session, b.session);
	for (const auto &packet : right)
		QCOMPARE(packet.session, d.session);
	QVERIFY(a.audio().isEmpty());
	QVERIFY(c.audio().isEmpty());
}
void TestAudioFeedbackIntegration::customTone_data() {
	QTest::addColumn< bool >("valid");
	QTest::newRow("custom-WAV") << true;
	QTest::newRow("invalid-fallback") << false;
}
void TestAudioFeedbackIntegration::customTone() {
	QFETCH(bool, valid);
	Fixture f;
	const auto path = f.directory.filePath("busy.wav");
	QByteArray bytes(44 + 9600, '\0');
	std::memcpy(bytes.data(), "RIFF", 4);
	qToLittleEndian< quint32 >(static_cast< quint32 >(bytes.size() - 8), bytes.data() + 4);
	std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
	qToLittleEndian< quint32 >(16, bytes.data() + 16);
	qToLittleEndian< quint16 >(1, bytes.data() + 20);
	qToLittleEndian< quint16 >(1, bytes.data() + 22);
	qToLittleEndian< quint32 >(48000, bytes.data() + 24);
	qToLittleEndian< quint32 >(96000, bytes.data() + 28);
	qToLittleEndian< quint16 >(2, bytes.data() + 32);
	qToLittleEndian< quint16 >(16, bytes.data() + 34);
	std::memcpy(bytes.data() + 36, "data", 4);
	qToLittleEndian< quint32 >(9600, bytes.data() + 40);
	for (int i = 0; i < 4800; ++i)
		qToLittleEndian< qint16 >(static_cast< qint16 >(30000 * std::sin(2 * 3.141592653589793 * 1000 * i / 48000)),
								  bytes.data() + 44 + i * 2);
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	QVERIFY(file.write(valid ? bytes : QByteArray("invalid")) > 0);
	file.close();
	QVERIFY(f.start("true", 5000, 5000, false,
					"audiofeedback=true\nbusytonevolume=100\nbusytonefile=" + path.toUtf8() + "\n"));
	Client a, b;
	QVERIFY(a.login(f.port, "A"));
	QVERIFY(b.login(f.port, "B"));
	QVERIFY(a.speak());
	b.audio();
	QVERIFY(b.speak());
	QVERIFY(b.barrier());
	const auto tone = b.audio(350, valid ? 6 : 13);
	QCOMPARE(tone.size(), valid ? 6 : 13);
	if (valid) {
		int error            = 0;
		OpusDecoder *decoder = opus_decoder_create(48000, 1, &error);
		QCOMPARE(error, OPUS_OK);
		double peak    = 0;
		int crosses    = 0;
		float previous = 0;
		for (const auto &packet : tone) {
			std::array< float, 960 > pcm{};
			QCOMPARE(opus_decode_float(decoder, reinterpret_cast< const unsigned char * >(packet.payload.constData()),
									   packet.payload.size(), pcm.data(), 960, 0),
					 960);
			for (float sample : pcm) {
				peak = std::max(peak, std::abs(static_cast< double >(sample)));
				if (previous <= 0 && sample > 0.01f)
					++crosses;
				previous = sample;
			}
		}
		opus_decoder_destroy(decoder);
		QVERIFY(peak > 0.1 && peak < 0.23);
		QVERIFY(crosses > 70);
	}
}
void TestAudioFeedbackIntegration::legacyNonceIsNotProtocolVersion() {
	Fixture f;
	QVERIFY(f.start("true", 5000, 5000, false, "audiofeedback=true\n"));
	Client a, b, observer;
	QVERIFY(a.login(f.port, "A"));
	QVERIFY(b.login(f.port, "B"));
	QVERIFY(observer.login(f.port, "Observer"));
	// A valid encrypted packet's first nonce byte can equal the protobuf ping header.
	// The unauthenticated ping probe must not change decoding of legacy plaintext.
	for (Client *client : { &a, &b }) {
		auto iv = client->crypt.getEncryptIV();
		iv[0]   = 0;
		QVERIFY(client->crypt.setEncryptIV(iv));
		MumbleProto::CryptSetup sync;
		sync.set_client_nonce(iv);
		QVERIFY(client->send(Type::CryptSetup, sync));
		QVERIFY(client->barrier());
		QVERIFY(client->udp.bind(QHostAddress(QHostAddress::LocalHost), 0));
	}
	QVERIFY(a.speak(true));
	QVERIFY(observer.expect(a));
	b.audio();
	QVERIFY(b.speak(true));
	QVERIFY(b.udpBarrier());
	const auto tone = b.audio(350, 13);
	QCOMPARE(tone.size(), 13);
	for (const auto &packet : tone)
		QCOMPARE(packet.session, b.session);
	QVERIFY(observer.audio().isEmpty());
}
void TestAudioFeedbackIntegration::reusedSession() {
	Fixture f;
	QVERIFY(f.start("true", 5000, 5000, false, "audiofeedback=true\nusers=2\n"));
	Client a, b;
	QVERIFY(a.login(f.port, "A"));
	QVERIFY(b.login(f.port, "B"));
	QVERIFY(a.speak());
	b.audio();
	QVERIFY(b.speak());
	QVERIFY(b.barrier());
	QVERIFY(!b.audio(25, 1).isEmpty());
	const auto oldSession = b.session;
	auto removed          = [&](unsigned int session) {
        QByteArray data;
        return a.receive(Type::UserRemove, data, [session](const QByteArray &bytes) {
            MumbleProto::UserRemove message;
            return message.ParseFromArray(bytes.data(), bytes.size()) && message.session() == session;
        });
	};
	b.socket.abort();
	QVERIFY(removed(oldSession));
	// The server's pool has 2*users IDs. Consume the two unused IDs, then reuse B's.
	bool reused = false;
	for (int i = 0; i < 4; ++i) {
		Client replacement;
		QVERIFY(replacement.login(f.port, "Replacement" + std::to_string(i)));
		if (replacement.session == oldSession) {
			reused = true;
			QVERIFY(replacement.audio(400).isEmpty());
			break;
		}
		const auto session = replacement.session;
		replacement.socket.abort();
		QVERIFY(removed(session));
	}
	QVERIFY2(reused, "The test must actually reuse the disconnected session number");
}
QTEST_MAIN(TestAudioFeedbackIntegration)
#include "TestAudioFeedbackIntegration.moc"
