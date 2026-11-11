// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#include "AudioSignal.h"
#include "AudioFeedback.h"
#include "SquelchPlan.h"
#include "SimplexController.h"
#include "MumbleProtocol.h"
#include <QtTest/QtTest>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEndian>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <opus.h>
#include <thread>

using Plan = SquelchPlan;
using Identity = Plan::Identity;
static const Identity sourceIdentity{1, 10, 0}, listener{2, 11, 0};
static QString asset(const char *name) { return QStringLiteral(SQUELCH_ASSET_DIR) + '/' + name; }
static std::vector<float> decode(const std::vector<QByteArray> &packets) {
	int error = 0;
	std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000, 1, &error), opus_decoder_destroy);
	if (!decoder || error != OPUS_OK) throw std::runtime_error("decoder creation failed");
	std::vector<float> result;
	for (const auto &packet : packets) {
		std::array<float, 5760> pcm{};
		int n = opus_decode_float(decoder.get(), reinterpret_cast<const unsigned char *>(packet.constData()),
			packet.size(), pcm.data(), 5760, 0);
		if (n != 960) throw std::runtime_error("expected decodable 20-ms Opus packet");
		result.insert(result.end(), pcm.begin(), pcm.begin() + n);
	}
	return result;
}

class TestSquelchFoundation : public QObject {
	Q_OBJECT
private slots:
	void originalAssetsAndPadding();
	void signalTailAndArbitration();
	void malformedAssets();
	void shortLengthBoundaries();
	void staleCues();
	void confirmedLifecycle();
	void simplexDecisions();
	void invalidationAndIdentity();
	void rapidHandoff();
	void timeoutLifecycle();
	void timeoutInvalidation();
	void timeoutController();
	void timeoutBusyPriority();
	void boundedAndConcurrent();
	void protocolSequence_data();
	void protocolSequence();
	void naiveSpliceIsNotTransparent_data();
	void naiveSpliceIsNotTransparent();
};

void TestSquelchFoundation::originalAssetsAndPadding() {
	QFile file(asset("squelch-open.wav")); QVERIFY(file.open(QIODevice::ReadOnly));
	QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex(),
		QByteArray("81573e3a11db089de7cf2bbb2918e0747a2edde12923c399ac5042513d038770"));
	QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(asset("squelch-open.wav")), std::runtime_error);
	const auto open = AudioSignal::loadWav(asset("squelch-open.wav"), AudioSignal::LengthPolicy::ShortSignal);
	const auto close = AudioSignal::loadWav(asset("squelch-close.wav"), AudioSignal::LengthPolicy::ShortSignal);
	QCOMPARE(open.size(), std::size_t(897 + 960));
	QCOMPARE(close.size(), std::size_t(3122 + 960));
	QVERIFY(std::all_of(open.begin()+897, open.end(), [](float x) { return x == 0; }));
	QVERIFY(std::all_of(close.begin()+3122, close.end(), [](float x) { return x == 0; }));
	QVERIFY(close == AudioSignal::loadWav(asset("squelch-close.wav"))); // Busy-compatible lengths unchanged
	QFile closeFile(asset("squelch-close.wav")); QVERIFY(closeFile.open(QIODevice::ReadOnly));
	QCOMPARE(QCryptographicHash::hash(closeFile.readAll(), QCryptographicHash::Sha256).toHex(),
		QByteArray("8d9091e0e4ee36433eb8aa707a7fa4a19ccec8ce5dee85ef1d91fa478afd0e31"));
	const auto opening = AudioSignal::encode(open, 100), closing = AudioSignal::encode(close, 100);
	QCOMPARE(opening.size(), std::size_t(2)); QCOMPARE(closing.size(), std::size_t(5));
	for (const auto &packets : {opening, closing}) {
		const auto samples = decode(packets);
		double energy = 0, peak = 0;
		for (float value : samples) { QVERIFY(std::isfinite(value)); energy += value*value; peak = std::max(peak, double(std::abs(value))); }
		QVERIFY(energy > .01); QVERIFY(peak < .35); // Opus reconstruction can overshoot the 0.2 input ceiling.
	}
	const auto silent = decode(AudioSignal::encode(open, 0));
	QVERIFY(std::all_of(silent.begin(), silent.end(), [](float x) { return std::abs(x) < 1e-5; }));
}

void TestSquelchFoundation::malformedAssets() {
	QTemporaryDir directory; QVERIFY(directory.isValid());
	const auto path = directory.filePath("bad.wav");
	QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(path, AudioSignal::LengthPolicy::ShortSignal), std::runtime_error);
	QFile original(asset("squelch-open.wav")); QVERIFY(original.open(QIODevice::ReadOnly)); const auto good = original.readAll();
	// Locate actual PCM format/data chunks (the originals include ancillary chunks).
	const int fmt = good.indexOf("fmt ") + 8, data = good.indexOf("data") + 8;
	QVERIFY(fmt >= 20); QVERIFY(data > fmt);
	std::vector<QByteArray> invalid{QByteArray("not WAV"), good.left(good.size()-1), QByteArray(131073, 'x')};
	for (int offset : {fmt, fmt+2, fmt+4, fmt+14}) {
		auto bytes = good; bytes[offset] = char(bytes.at(offset) ^ 0x02); invalid.push_back(bytes);
	}
	auto empty = good; qToLittleEndian<quint32>(0, empty.data()+data-4); invalid.push_back(empty);
	for (const auto &bytes : invalid) {
		QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size())); file.close();
		QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(path, AudioSignal::LengthPolicy::ShortSignal), std::runtime_error);
	}
}

void TestSquelchFoundation::shortLengthBoundaries() {
	QTemporaryDir directory; QVERIFY(directory.isValid());
	for (unsigned int rate : {8000u,16000u,24000u,48000u}) {
		for (unsigned int frames : {0u, rate/1000-1, rate/1000, rate/50, rate, rate+1}) {
			QByteArray bytes(int(44+frames*2), '\0');
			std::memcpy(bytes.data(),"RIFF",4); std::memcpy(bytes.data()+8,"WAVEfmt ",8);
			qToLittleEndian<quint32>(quint32(bytes.size()-8),bytes.data()+4);
			qToLittleEndian<quint32>(16,bytes.data()+16); qToLittleEndian<quint16>(1,bytes.data()+20);
			qToLittleEndian<quint16>(1,bytes.data()+22); qToLittleEndian<quint32>(rate,bytes.data()+24);
			qToLittleEndian<quint32>(rate*2,bytes.data()+28); qToLittleEndian<quint16>(2,bytes.data()+32);
			qToLittleEndian<quint16>(16,bytes.data()+34); std::memcpy(bytes.data()+36,"data",4);
			qToLittleEndian<quint32>(frames*2,bytes.data()+40);
			const auto path=directory.filePath("boundary.wav"); QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes),qint64(bytes.size())); file.close();
			if (frames<rate/1000 || frames>rate) {
				QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(path,AudioSignal::LengthPolicy::ShortSignal),std::runtime_error);
			} else {
				const auto pcm=AudioSignal::loadWav(path,AudioSignal::LengthPolicy::ShortSignal);
				QCOMPARE(pcm.size(),std::size_t(frames)*48000/rate+960);
				QVERIFY(!AudioSignal::encode(pcm,50).empty());
				if (frames<rate/50) QVERIFY_EXCEPTION_THROWN(AudioSignal::loadWav(path),std::runtime_error);
				else QVERIFY(pcm==AudioSignal::loadWav(path));
			}
		}
	}
}

void TestSquelchFoundation::staleCues() {
	std::int64_t now=0; Plan plan(true,[&] {return now;});
	auto cues=plan.observe(sourceIdentity,true,0,true,true,true,{listener});
	now=-1; QVERIFY(!plan.consume(cues[0],sourceIdentity,listener,true));
	now=0; QVERIFY(plan.consume(cues[0],sourceIdentity,listener,true));
	now=Plan::MaxCloseCueAgeUs+1; QVERIFY(!plan.consume(cues[1],sourceIdentity,listener,true));
	plan.clear();
	cues=plan.observe(sourceIdentity,true,2,true,true,true,{listener});
	now+=Plan::MaxCueAgeUs+1; QVERIFY(!plan.consume(cues[0],sourceIdentity,listener,true));
}

void TestSquelchFoundation::confirmedLifecycle() {
	Plan disabled;
	QVERIFY(disabled.observe(sourceIdentity, true, 0, true, false, true, {listener}).empty());
	Plan plan(true);
	QVERIFY(plan.observe(sourceIdentity, false, 0, true, false, true, {listener}).empty());
	QVERIFY(plan.observe(sourceIdentity, true, 0, false, true, false, {listener}).empty());
	QVERIFY(plan.observe(sourceIdentity, true, 0, true, false, false, {listener}).empty());
	const Identity other{3, 12, 9};
	auto open = plan.observe(sourceIdentity, true, 0, true, false, true, {sourceIdentity, listener, listener, other});
	QCOMPARE(open.size(), std::size_t(1)); QCOMPARE(open[0].kind, Plan::Kind::Open);
	QVERIFY(plan.consume(open[0], sourceIdentity, listener, true));
	QVERIFY(!plan.consume(open[0], sourceIdentity, listener, true));
	QVERIFY(plan.observe(sourceIdentity, true, 2, true, false, true, {listener}).empty());
	QVERIFY(plan.observe(sourceIdentity, true, 1, false, true, false, {listener}).empty()); // stale terminator
	auto close = plan.observe(sourceIdentity, true, 2, false, true, false, {listener}); // same-sequence empty marker
	QCOMPARE(close.size(), std::size_t(1)); QCOMPARE(close[0].kind, Plan::Kind::Close);
	QVERIFY(plan.consume(close[0], sourceIdentity, listener, true));
	QVERIFY(!plan.consume(close[0], sourceIdentity, listener, true));
	QVERIFY(plan.observe(sourceIdentity, true, 2, false, true, false, {listener}).empty());
	// One-packet transmission: Open before Close; neither may be dispatched twice.
	auto single = plan.observe(sourceIdentity, true, 4, true, true, true, {listener});
	QCOMPARE(single.size(), std::size_t(2));
	QVERIFY(!plan.consume(single[1], sourceIdentity, listener, true));
	QVERIFY(plan.consume(single[0], sourceIdentity, listener, true));
	QVERIFY(plan.consume(single[1], sourceIdentity, listener, true));
}

void TestSquelchFoundation::simplexDecisions() {
	std::int64_t now = 0;
	SimplexController simplex(500, 1000, {}, [&] { return now; }); Plan plan(true);
	auto packet = [&](Identity who, std::uint64_t frame, bool last = false, bool payload = true) {
		SimplexController::Packet p;
		p.context.connection=who.connection; p.context.session=who.session; p.context.channel=who.channel;
		p.frame=frame; p.payload=payload; p.last=last;
		auto decision=simplex.begin(p);
		return plan.observe(who, decision.allowed, frame, payload, last, payload, {listener});
	};
	QCOMPARE(packet(sourceIdentity,0).size(), std::size_t(1));
	Identity loser{4,14,0};
	QVERIFY(packet(loser,0).empty());
	QCOMPARE(packet(sourceIdentity,2,true).size(), std::size_t(1));
	QVERIFY(packet(loser,2).empty()); // no automatic takeover
	QVERIFY(packet(loser,4,true,false).empty());
	QCOMPARE(packet(loser,6).size(), std::size_t(1));
	now=500000; simplex.expire(); plan.invalidate(loser.connection); // suspected end: omit Close
	QVERIFY(packet(loser,8).empty());
	QCOMPARE(plan.size(), std::size_t(0));
}

void TestSquelchFoundation::invalidationAndIdentity() {
	Plan plan(true);
	auto cue=plan.observe(sourceIdentity,true,0,true,false,true,{listener})[0];
	Identity reused=listener; reused.connection=99;
	QVERIFY(!plan.consume(cue,sourceIdentity,reused,true));
	Identity moved=listener; moved.channel=2;
	QVERIFY(!plan.consume(cue,sourceIdentity,moved,true));
	QVERIFY(!plan.consume(cue,sourceIdentity,listener,false));
	plan.invalidate(listener.connection); // leave and return with same session/connection
	QVERIFY(!plan.consume(cue,sourceIdentity,listener,true));
	plan.invalidate(sourceIdentity.connection);
	QVERIFY(!plan.consume(cue,sourceIdentity,listener,true));
	auto replacement=plan.observe(sourceIdentity,true,2,true,false,true,{listener})[0];
	QVERIFY(replacement.transmission != cue.transmission);
	QVERIFY(!plan.consume(cue,sourceIdentity,listener,true));
	Identity sourceReuse=sourceIdentity; sourceReuse.connection=100;
	QVERIFY(!plan.consume(replacement,sourceReuse,listener,true));
	plan.clear();
	QVERIFY(!plan.consume(replacement,sourceIdentity,listener,true));
}

void TestSquelchFoundation::rapidHandoff() {
	Plan plan(true);
	auto old=plan.observe(sourceIdentity,true,0,true,true,true,{listener});
	QVERIFY(plan.consume(old[0],sourceIdentity,listener,true));
	Identity next{3,13,0};
	auto fresh=plan.observe(next,true,0,true,false,true,{listener});
	QVERIFY(!plan.consume(old[1],sourceIdentity,listener,true)); // unsent Close superseded
	QVERIFY(plan.consume(fresh[0],next,listener,true));
	Identity parallel{4,14,1}, parallelListener{5,15,1};
	auto independent=plan.observe(parallel,true,0,true,true,true,{parallelListener});
	QVERIFY(plan.consume(independent[0],parallel,parallelListener,true));
	QVERIFY(plan.consume(independent[1],parallel,parallelListener,true));
	QCOMPARE(plan.size(), std::size_t(2));
}

void TestSquelchFoundation::timeoutLifecycle() {
	std::int64_t now=0; Plan plan(true,[&]{return now;});
	QVERIFY(plan.timeout(sourceIdentity,0).empty());
	QVERIFY(plan.observe(sourceIdentity,false,0,true,false,true,{listener}).empty());
	QVERIFY(plan.observe(sourceIdentity,true,0,false,true,false,{listener}).empty());
	QVERIFY(plan.observe(sourceIdentity,true,0,true,false,false,{listener}).empty());
	QVERIFY(plan.timeout(sourceIdentity,0).empty());
	auto open=plan.observe(sourceIdentity,true,0,true,false,true,{listener})[0];
	const auto close=plan.timeout(sourceIdentity,0); QCOMPARE(close.size(),std::size_t(1));
	QVERIFY(!plan.consume(close[0],sourceIdentity,listener,true)); // No Close without accepted Open.
	QVERIFY(plan.consume(open,sourceIdentity,listener,true));
	QVERIFY(plan.consume(close[0],sourceIdentity,listener,true));
	QVERIFY(!plan.consume(close[0],sourceIdentity,listener,true));
	for(int i=0;i<20;++i) QVERIFY(plan.timeout(sourceIdentity,0).empty());
	QVERIFY(plan.observe(sourceIdentity,true,0,false,true,false,{listener}).empty());
	// Same sender and reset sequence: the old queued/playing Close is obsolete.
	auto fresh=plan.observe(sourceIdentity,true,0,true,false,true,{listener})[0];
	QVERIFY(fresh.transmission!=close[0].transmission); QVERIFY(!plan.current(close[0]));
	QVERIFY(!plan.consume(close[0],sourceIdentity,listener,true));
	QVERIFY(plan.consume(fresh,sourceIdentity,listener,true));
	auto confirmed=plan.observe(sourceIdentity,true,2,false,true,false,{listener});
	QCOMPARE(confirmed.size(),std::size_t(1)); QVERIFY(plan.timeout(sourceIdentity,2).empty());
	QVERIFY(plan.consume(confirmed[0],sourceIdentity,listener,true));
}

void TestSquelchFoundation::timeoutInvalidation() {
	for(int boundary=0;boundary<7;++boundary) {
		std::int64_t now=0; Plan plan(true,[&]{return now;});
		auto open=plan.observe(sourceIdentity,true,0,true,false,true,{listener})[0];
		QVERIFY(plan.consume(open,sourceIdentity,listener,true)); now=500000;
		auto close=plan.timeout(sourceIdentity,0); QCOMPARE(close.size(),std::size_t(1));
		Identity source=sourceIdentity, receiver=listener;
		if(boundary==0) plan.invalidate(source.connection); // Disconnect or channel out/back.
		if(boundary==1) plan.invalidate(receiver.connection);
		if(boundary==2) source.connection=100; // Same numeric session, different connection.
		if(boundary==3) receiver.connection=100;
		if(boundary==4) receiver.channel=9;
		if(boundary==5) now+=Plan::MaxCloseCueAgeUs+1;
		QVERIFY(!plan.consume(close[0],source,receiver,boundary!=6));
	}
	Plan mismatched(true);
	auto open=mismatched.observe(sourceIdentity,true,0,true,false,true,{listener})[0];
	Identity foreign=sourceIdentity; foreign.connection=99;
	QVERIFY(mismatched.timeout(foreign,0).empty());
	QVERIFY(mismatched.consume(open,sourceIdentity,listener,true));
	QVERIFY(mismatched.timeout(sourceIdentity,2).empty()); // Unobserved/invalid last packet.
	QVERIFY(!mismatched.current(open));
}

void TestSquelchFoundation::timeoutController() {
	std::int64_t now=0; Plan plan(true,[&]{return now;}); std::vector<Plan::Cue> pending;
	int timeouts=0,markers=0;
	SimplexController simplex(500,500,[&](const SimplexController::Event &event) {
		if(event.event!="released") return;
		if(event.reason=="marker_confirmed") { ++markers; return; }
		if(event.reason=="timeout_suspected") {
			++timeouts; auto cues=plan.timeout({event.context.connection,event.context.session,event.context.channel},event.frame);
			pending.insert(pending.end(),cues.begin(),cues.end());
		} else plan.invalidate(event.context.connection);
	},[&]{return now;});
	auto packet=[&](Identity who,std::uint64_t frame,bool last=false,bool payload=true) {
		SimplexController::Packet p; p.context.connection=who.connection; p.context.session=who.session;
		p.context.channel=who.channel; p.frame=frame; p.last=last; p.payload=payload;
		auto decision=simplex.begin(p);
		return plan.observe(who,decision.allowed,frame,payload,last,payload,{listener});
	};
	auto open=packet(sourceIdentity,0)[0]; QVERIFY(plan.consume(open,sourceIdentity,listener,true));
	Identity loser{3,12,0}; QVERIFY(packet(loser,0).empty());
	now=499999; simplex.expire(); QCOMPARE(timeouts,0);
	now=500000; simplex.expire(); QCOMPARE(timeouts,1); QCOMPARE(pending.size(),std::size_t(1));
	for(int i=0;i<4;++i) { now+=50000; simplex.expire(); }
	QCOMPARE(timeouts,1); QCOMPARE(pending.size(),std::size_t(1));
	QVERIFY(packet(sourceIdentity,2,true,false).empty()); QCOMPARE(markers,0); // Late marker after timeout: no second Close.
	auto next=packet(loser,0)[0]; QVERIFY(!plan.current(open));
	QVERIFY(!plan.consume(pending[0],sourceIdentity,listener,true)); // Queued old Close canceled by new grant.
	QVERIFY(plan.consume(next,loser,listener,true));
	QCOMPARE(packet(loser,2,true).size(),std::size_t(1)); QCOMPARE(markers,1);
	now+=1000000; simplex.expire(); QCOMPARE(timeouts,1); // Confirmed stream never falls back.
}

void TestSquelchFoundation::timeoutBusyPriority() {
	std::int64_t now=0; Plan plan(true,[&]{return now;}); AudioFeedback feedback(50);
	feedback.configureSquelch(asset("squelch-open.wav"),asset("squelch-close.wav"),50,50);
	auto gate=[&](const Plan::Cue &cue,bool starting){return starting ? plan.consume(cue,cue.sender,cue.recipient,true):plan.current(cue);};
	std::vector<QByteArray> packets; std::vector<std::uint64_t> frames; std::vector<bool> endings;
	auto deliver=[&](const AudioFeedback::Recipient &,const QByteArray &bytes,std::uint64_t frame,bool last){
		packets.push_back(bytes); frames.push_back(frame); endings.push_back(last); return true;
	};
	QVERIFY(feedback.requestSquelch(plan.observe(sourceIdentity,true,0,true,false,true,{listener})[0]));
	feedback.tick(now,deliver,gate); now=20000; feedback.tick(now,deliver,gate);
	packets.clear(); frames.clear(); endings.clear(); now=500000;
	QVERIFY(feedback.requestSquelch(plan.timeout(sourceIdentity,0)[0])); feedback.tick(now,deliver,gate);
	now+=10000; QVERIFY(feedback.request(AudioFeedback::Signal::Busy,{listener.connection,listener.session,listener.channel},now));
	for(now=520000;now<=1000000;now+=20000) feedback.tick(now,deliver,gate);
	const auto busy=feedback.clip(AudioFeedback::Signal::Busy);
	QCOMPARE(packets.size(),busy.size()+2); QVERIFY(endings[1]); QVERIFY(endings.back());
	for(std::size_t i=0;i<busy.size();++i) QCOMPARE(packets[i+2],busy[i]);
	for(std::size_t i=1;i<frames.size();++i) QVERIFY(frames[i]>frames[i-1]);
	QVERIFY(!decode(packets).empty());
}

void TestSquelchFoundation::boundedAndConcurrent() {
	Plan plan(true);
	std::vector<Identity> audience;
	for (unsigned int i=0;i<1000;++i) audience.push_back({100+i,100+i,0});
	auto cues=plan.observe(sourceIdentity,true,0,true,true,true,audience);
	QCOMPARE(cues.size(), std::size_t(2*Plan::MaxRecipients));
	std::atomic<int> dispatches{0}; std::vector<std::thread> threads;
	for (int i=0;i<8;++i) threads.emplace_back([&] { if (plan.consume(cues[0],sourceIdentity,cues[0].recipient,true)) ++dispatches; });
	for (auto &thread:threads) thread.join();
	QCOMPARE(dispatches.load(),1);
	threads.clear();
	for (unsigned int i=0;i<8;++i) threads.emplace_back([&,i] {
		for (unsigned int j=1;j<=40;++j) { unsigned int id=1+i*40+j;
			plan.observe({10000+id,10000+id,id},true,0,true,false,true,{});
		}
	});
	for (auto &thread:threads) thread.join();
	QVERIFY(plan.size() <= Plan::MaxChannels);
	// try-lock may omit optional effects under contention. Fill sequentially to
	// verify the hard bound independently of how many workers obtained the lock.
	for (unsigned i=1000;i<1200;++i) plan.observe({i,i,i},true,0,true,false,true,{});
	QCOMPARE(plan.size(), Plan::MaxChannels);
	QVERIFY(plan.observe({90000,90000,90000},true,0,true,false,true,{}).empty());
	plan.clear(); QCOMPARE(plan.size(),std::size_t(0));
}

void TestSquelchFoundation::protocolSequence_data() {
	QTest::addColumn<bool>("modern"); QTest::newRow("legacy") << false; QTest::newRow("protobuf") << true;
}
void TestSquelchFoundation::protocolSequence() {
	QFETCH(bool, modern);
	const auto version=Version::fromComponents(1,modern ? 5 : 2,0);
	const auto packets=AudioSignal::encode(AudioSignal::loadWav(asset("squelch-close.wav")),50);
	std::uint64_t frame=100;
	for (std::size_t i=0;i<packets.size();++i) {
		Mumble::Protocol::AudioData audio;
		audio.senderSession=17; audio.frameNumber=frame; audio.isLastFrame=i+1==packets.size();
		audio.targetOrContext=Mumble::Protocol::AudioContext::NORMAL;
		audio.payload={reinterpret_cast<const Mumble::Protocol::byte *>(packets[i].constData()),std::size_t(packets[i].size())};
		Mumble::Protocol::UDPAudioEncoder<Mumble::Protocol::Role::Server> encoder(version);
		Mumble::Protocol::UDPDecoder<Mumble::Protocol::Role::Client> decoder(version);
		QVERIFY(decoder.decode(encoder.encodeAudioPacket(audio)));
		const auto result=decoder.getAudioData();
		QCOMPARE(result.frameNumber,frame); QCOMPARE(result.senderSession,17u); QCOMPARE(result.isLastFrame,audio.isLastFrame);
		QCOMPARE(QByteArray(reinterpret_cast<const char *>(result.payload.data()),int(result.payload.size())),packets[i]);
		const int samples=opus_packet_get_nb_samples(result.payload.data(),int(result.payload.size()),48000);
		QCOMPARE(samples,960); frame+=std::uint64_t(samples/480);
	}
	QCOMPARE(frame,std::uint64_t(110));
}

void TestSquelchFoundation::naiveSpliceIsNotTransparent_data() {
	QTest::addColumn<int>("bitrate");
	QTest::newRow("voice-16k") << 16000; QTest::newRow("voice-24k") << 24000; QTest::newRow("voice-64k") << 64000;
}
void TestSquelchFoundation::naiveSpliceIsNotTransparent() {
	QFETCH(int,bitrate);
	int error=0;
	std::unique_ptr<OpusEncoder,decltype(&opus_encoder_destroy)> encoder(opus_encoder_create(48000,1,OPUS_APPLICATION_VOIP,&error),opus_encoder_destroy);
	QVERIFY(encoder); QCOMPARE(error,OPUS_OK); QCOMPARE(opus_encoder_ctl(encoder.get(),OPUS_SET_BITRATE(bitrate)),OPUS_OK);
	std::vector<QByteArray> voice;
	for (int packet=0;packet<8;++packet) {
		std::array<float,960> pcm{};
		for (int i=0;i<960;++i) { const int n=960*packet+i;
			pcm[std::size_t(i)]=float(.15*(std::sin(2*3.141592653589793*233*n/48000)+.2*std::sin(2*3.141592653589793*877*n/48000))); }
		std::array<unsigned char,4000> bytes{};
		int length=opus_encode_float(encoder.get(),pcm.data(),960,bytes.data(),int(bytes.size())); QVERIFY(length>0);
		voice.emplace_back(reinterpret_cast<const char *>(bytes.data()),length);
	}
	const auto baseline=decode(voice);
	QVERIFY(baseline==decode(voice)); // Independent fresh decoder: voice itself is deterministic.
	auto prefixed=AudioSignal::encode(AudioSignal::loadWav(asset("squelch-open.wav"),AudioSignal::LengthPolicy::ShortSignal),100);
	const auto prefixSamples=prefixed.size()*960;
	prefixed.insert(prefixed.end(),voice.begin(),voice.end());
	const auto combined=decode(prefixed);
	double difference=0,maximum=0;
	for (std::size_t i=0;i<baseline.size();++i) {
		double d=baseline[i]-combined[prefixSamples+i]; difference+=d*d; maximum=std::max(maximum,std::abs(d));
	}
	qInfo("naive independent Opus splice: bitrate=%d max PCM delta=%g RMS delta=%g",bitrate,maximum,std::sqrt(difference/static_cast<double>(baseline.size())));
	QVERIFY2(maximum>1e-7,"This counterexample must demonstrate decoder-state contamination; it does not quantify audibility");
}

void TestSquelchFoundation::signalTailAndArbitration() {
	AudioFeedback feedback(50);
	feedback.configureSquelch(asset("squelch-open.wav"),asset("squelch-close.wav"),50,50);
	const auto &open=feedback.clip(AudioFeedback::Signal::SquelchOpen);
	const auto &close=feedback.clip(AudioFeedback::Signal::SquelchClose);
	QCOMPARE(open.size(),std::size_t(2)); QCOMPARE(close.size(),std::size_t(4));
	// Actual lookahead-compensating duration, without altering the source WAV.
	QCOMPARE(decode(open).size(),std::size_t(1920)); QCOMPARE(decode(close).size(),std::size_t(3840));
	const auto full=decode(AudioSignal::encode(AudioSignal::loadWav(asset("squelch-open.wav"),AudioSignal::LengthPolicy::ShortSignal),50));
	QVERIFY(full==decode(open)); // Open's old two-packet representation remains identical.
	const auto longClose=decode(AudioSignal::encode(AudioSignal::loadWav(asset("squelch-close.wav")),50));
	const auto shortClose=decode(close);
	QVERIFY(std::equal(shortClose.begin(),shortClose.end(),longClose.begin()));
	// Independent volume controls, with attenuation-only peak handling.
	const auto silent=decode(AudioSignal::encodeWavSignal(asset("squelch-open.wav"),0));
	for(float x:silent) QVERIFY(std::abs(x)<1e-5f);
	std::int64_t now=0; Plan plan(true,[&]{return now;});
	auto cue=plan.observe(sourceIdentity,true,0,true,false,true,{listener})[0];
	QVERIFY(feedback.requestSquelch(cue));
	std::vector<QByteArray> packets; std::vector<std::uint64_t> sequence; std::vector<bool> endings;
	auto gate=[&](const Plan::Cue &c,bool starting){return starting ? plan.consume(c,c.sender,c.recipient,true):plan.current(c);};
	bool recipientValid=true;
	auto deliver=[&](const AudioFeedback::Recipient &to,const QByteArray &bytes,std::uint64_t frame,bool last){
		recipientValid = recipientValid && to.connection==listener.connection; packets.push_back(bytes); sequence.push_back(frame); endings.push_back(last); return true;
	};
	// Busy interrupts only optional cues, with a nonempty end boundary, and then
	// plays all original packets. Its cooldown is independent of Squelch starts.
	feedback.tick(now,deliver,gate); QCOMPARE(packets.size(),std::size_t(1));
	now=10000; QVERIFY(feedback.request(AudioFeedback::Signal::Busy,{listener.connection,listener.session,listener.channel},now));
	for(now=20000;now<=400000;now+=20000) feedback.tick(now,deliver,gate);
	QCOMPARE(packets.size(),std::size_t(15)); QVERIFY(endings[1]); QVERIFY(endings.back());
	const auto busy=feedback.clip(AudioFeedback::Signal::Busy);
	for(std::size_t i=0;i<busy.size();++i) QCOMPARE(packets[i+2],busy[i]);
	for(std::size_t i=1;i<sequence.size();++i) QVERIFY(sequence[i]>sequence[i-1]);
	QVERIFY(!decode(packets).empty()); QVERIFY(recipientValid);
	// Cancelled generation cannot continue playback even with a reused session.
	AudioFeedback canceled(50); canceled.configureSquelch(asset("squelch-open.wav"),asset("squelch-close.wav"),50,50);
	plan.clear(); cue=plan.observe(sourceIdentity,true,2,true,false,true,{listener})[0];
	QVERIFY(canceled.requestSquelch(cue)); const auto before=packets.size();
	plan.invalidate(listener.connection); canceled.tick(now,deliver,gate); QCOMPARE(packets.size(),before);
}

QTEST_MAIN(TestSquelchFoundation)
#include "TestSquelchFoundation.moc"
