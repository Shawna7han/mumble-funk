// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#include "AudioFeedback.h"
#include "SimplexController.h"
#include <QtTest/QtTest>
#include <array>
#include <atomic>
#include <cmath>
#include <opus.h>
#include <thread>

class TestAudioFeedback : public QObject {
	Q_OBJECT
private slots:
	void tone();
	void decisions();
	void boundedQueueAndCooldown();
	void cancellationAndExpiry();
};

void TestAudioFeedback::tone() {
	double previous = 0;
	for (int volume : { 0, 25, 50, 100, 200 }) {
		AudioFeedback bank(volume);
		const auto &clip = bank.clip(AudioFeedback::Signal::Busy);
		QCOMPARE(clip.size(), std::size_t(13));
		int error            = 0;
		OpusDecoder *decoder = opus_decoder_create(48000, 1, &error);
		QCOMPARE(error, OPUS_OK);
		std::vector< float > samples;
		for (const auto &packet : clip) {
			std::array< float, 960 > pcm{};
			const int count = opus_decode_float(decoder, reinterpret_cast< const unsigned char * >(packet.constData()),
												packet.size(), pcm.data(), 960, 0);
			QCOMPARE(count, 960);
			samples.insert(samples.end(), pcm.begin(), pcm.end());
		}
		opus_decoder_destroy(decoder);
		bool finite = true;
		auto energy = [&](std::size_t from, std::size_t to) {
			double value = 0;
			for (auto i = from; i < to; ++i) {
				finite = finite && std::isfinite(samples[i]);
				value += samples[i] * samples[i];
			}
			return value / static_cast< double >(to - from);
		};
		const double first = energy(1000, 4000), gap = energy(5600, 6500), second = energy(7800, 11000);
		QVERIFY(finite);
		if (volume == 0) {
			QVERIFY(first < 1e-8);
			continue;
		}
		QVERIFY(first > 1e-5);
		QVERIFY(second > first * 0.5);
		QVERIFY(gap < first * 0.02);
		QVERIFY(first >= previous * 0.95);
		previous    = first;
		int crosses = 0;
		for (std::size_t i = 1001; i < 4000; ++i)
			if (samples[i - 1] <= 0 && samples[i] > 0)
				++crosses;
		QVERIFY(crosses >= 36 && crosses <= 39); // 600 Hz in 62.5 ms.
	}
}

void TestAudioFeedback::decisions() {
	std::int64_t now = 0;
	SimplexController engine(500, 1000, {}, [&] { return now; }); // No diagnostic sink.
	auto send = [&](std::uint64_t id, std::uint64_t frame, bool last = false, bool payload = true) {
		SimplexController::Packet p;
		p.context.connection = id;
		p.context.session    = static_cast< unsigned int >(id);
		p.frame              = frame;
		p.last               = last;
		p.payload            = payload;
		auto result          = engine.begin(p);
		return std::make_pair(result.allowed, result.feedback);
	};
	QVERIFY(send(1, 0).first);
	auto denied = send(2, 0);
	QVERIFY(!denied.first && denied.second); // busy
	for (unsigned int i = 0; i < 200; ++i)
		QVERIFY(!send(2, i).second); // includes silent stale-denial paths
	QVERIFY(send(1, 1, true).first);
	QVERIFY(!send(2, 200).first);
	QVERIFY(!send(2, 201).second);              // no automatic takeover
	QVERIFY(!send(2, 202, true, false).second); // valid marker re-arms
	QVERIFY(send(2, 203).first);
	QVERIFY(send(2, 204, true).first);
	denied = send(2, 204);
	QVERIFY(!denied.first && denied.second); // Idle sequence_not_advanced
	QVERIFY(!send(2, 203).second);
	now = 1000000;
	QVERIFY(send(3, 0).first);
	now = 1500000;
	engine.expire();
	denied = send(3, 1);
	QVERIFY(!denied.first && denied.second); // retry_required after timeout
	QVERIFY(!send(3, 2).second);
	now = 2500000;
	QVERIFY(send(3, 3).first);
	denied = send(3, 2);
	QVERIFY(!denied.first && denied.second); // owner rejects stale data too
	engine.invalidate(3, QStringLiteral("channel_changed"));
	QVERIFY(!send(3, 4).second); // same attempt, even after invalidation
	engine.disconnect(3);
	QVERIFY(send(3, 0).first);
}

void TestAudioFeedback::boundedQueueAndCooldown() {
	AudioFeedback engine(50);
	std::atomic< int > accepted{ 0 };
	std::vector< std::thread > threads;
	for (int i = 0; i < 4; ++i)
		threads.emplace_back([&] {
			for (int j = 0; j < 10000; ++j)
				if (engine.request(AudioFeedback::Signal::Busy, { 1, 2, 3 }, 0))
					++accepted;
		});
	for (auto &thread : threads)
		thread.join();
	QVERIFY(accepted > 0 && accepted <= 128);
	int sent      = 0;
	bool valid    = true;
	auto delivery = [&](const AudioFeedback::Recipient &to, const QByteArray &, std::uint64_t frame, bool last) {
		valid = valid && to.connection == 1;
		valid = valid && frame == static_cast< std::uint64_t >(sent * 2);
		valid = valid && last == (sent == 12);
		++sent;
		return true;
	};
	for (std::int64_t now = 0; now <= 300000; now += 20000)
		engine.tick(now, delivery);
	QCOMPARE(sent, 13);
	QVERIFY(valid);
	QVERIFY(engine.request(AudioFeedback::Signal::Busy, { 1, 2, 3 }, 400000));
	engine.tick(400000, delivery);
	QCOMPARE(sent, 13); // cooldown
	// Distinct identities are bounded too, even with many rejected users.
	for (std::uint64_t id = 2; id < 130; ++id)
		engine.request(AudioFeedback::Signal::Busy, { id, 2, 3 }, 500000);
	int recipients = 0;
	engine.tick(500000, [&](const AudioFeedback::Recipient &, const QByteArray &, std::uint64_t, bool) {
		++recipients;
		return true;
	});
	QVERIFY(recipients <= 63);
}

void TestAudioFeedback::cancellationAndExpiry() {
	AudioFeedback engine(50);
	int count     = 0;
	auto delivery = [&](const AudioFeedback::Recipient &, const QByteArray &, std::uint64_t, bool) {
		++count;
		return true;
	};
	engine.request(AudioFeedback::Signal::Busy, { 1, 1, 0 }, 0);
	engine.cancel(1);
	engine.tick(0, delivery);
	QCOMPARE(count, 0);
	engine.request(AudioFeedback::Signal::Busy, { 1, 1, 0 }, 0);
	engine.tick(400000, delivery);
	QCOMPARE(count, 0);
	engine.request(AudioFeedback::Signal::Busy, { 2, 1, 0 }, 400000);
	engine.tick(400000, delivery);
	QCOMPARE(count, 1);
	engine.cancel(2);
	engine.tick(420000, delivery);
	QCOMPARE(count, 1);
	engine.request(AudioFeedback::Signal::Busy, { 3, 1, 0 }, 500000);
	engine.tick(500000, delivery);
	QCOMPARE(count, 2);
	engine.tick(900000, delivery);
	QCOMPARE(count, 2); // stalled loop does not burst late frames
	engine.request(AudioFeedback::Signal::Busy, { 4, 1, 0 }, 900000);
	engine.tick(900000,
				[](const AudioFeedback::Recipient &, const QByteArray &, std::uint64_t, bool) { return false; });
	engine.tick(920000, delivery);
	QCOMPARE(count, 2); // failed recipient abandoned
}
QTEST_MAIN(TestAudioFeedback)
#include "TestAudioFeedback.moc"
