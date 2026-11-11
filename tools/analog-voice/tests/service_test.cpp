// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "AnalogVoiceService.h"
#ifdef VOICE_TEST_SIMPLEX
#	include "SimplexController.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Service = funkvoice::AnalogVoiceService;
using Stream  = funkvoice::AnalogVoiceStream;
#define CHECK(expression)                                   \
	do {                                                    \
		if (!(expression)) {                                \
			std::cerr << __LINE__ << ": " #expression "\n"; \
			std::abort();                                   \
		}                                                   \
	} while (false)

namespace {
// All scheduling uses handshakes; wall timeouts only bound a broken test.
struct Events {
	std::mutex mutex;
	std::condition_variable changed;
	std::size_t count = 0;
	void wake() {
		std::lock_guard< std::mutex > lock(mutex);
		++count;
		changed.notify_all();
	}
	Service::Batch next(Service &service) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for (;;) {
			std::unique_lock< std::mutex > lock(mutex);
			const std::size_t before = count;
			lock.unlock();
			service.acknowledgeNotification();
			Service::Batch batch;
			if (service.take(batch)) {
				return batch;
			}
			lock.lock();
			CHECK(changed.wait_until(lock, deadline, [&] { return count != before; }));
		}
	}
};
struct Control {
	std::mutex mutex;
	std::condition_variable changed;
	bool block = false, entered = false, released = false;
	std::atomic< int > calls{ 0 }, resets{ 0 }, failAt{ 0 };
	std::atomic< std::uint64_t > floor{ 0 };
	bool fullBatch      = false;
	bool invalidSources = false, failBegin = false;
	void pause() {
		std::unique_lock< std::mutex > lock(mutex);
		entered = true;
		changed.notify_all();
		if (block) {
			CHECK(changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; }));
		}
	}
	void wait() {
		std::unique_lock< std::mutex > lock(mutex);
		CHECK(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
	}
	void release() {
		std::lock_guard< std::mutex > lock(mutex);
		released = true;
		changed.notify_all();
	}
};
class Fake final : public Service::Processor {
	Control &m_control;
	Stream::State m_state = Stream::State::Idle;
	std::uint64_t m_frame = 0;

public:
	explicit Fake(Control &control) : m_control(control) {}
	bool begin(std::uint64_t frame, std::uint64_t floor) noexcept override {
		if (m_control.failBegin) {
			m_state = Stream::State::Faulted;
			return false;
		}
		m_control.floor = floor;
		m_frame         = std::max({ frame, floor, m_frame });
		m_state         = Stream::State::Active;
		return true;
	}
	void process(const Stream::Input &input, Stream::Result &result) noexcept override {
		m_control.pause();
		result         = {};
		const int call = ++m_control.calls;
		if (call == m_control.failAt) {
			m_state            = call == 1 ? Stream::State::Bypass : Stream::State::Faulted;
			result.disposition = Stream::Disposition::Rejected;
			result.error       = Stream::Error::Encoder;
			return;
		}
		result.count = m_control.fullBatch ? Stream::MaxOutputPackets : 1;
		for (std::size_t i = 0; i < result.count; ++i) {
			Stream::Packet &packet = result.packets[i];
			packet.frame           = m_frame;
			m_frame += 2;
			packet.bytes = input.bytes;
			std::copy_n(input.payload, input.bytes, packet.payload.begin());
			packet.spans[0]  = { Stream::Origin::Source, input.frame + (m_control.invalidSources ? 1000 : 0), 0, 0,
                                960 };
			packet.spanCount = 1;
		}
		if (input.end) {
			result.packets[result.count - 1].end = Stream::End::InputMarker;
			m_state                              = Stream::State::Ended;
		}
	}
	void finish(Stream::Result &result) noexcept override {
		result                  = {};
		result.count            = 1;
		result.packets[0].frame = m_frame;
		m_frame += 2;
		result.packets[0].end = Stream::End::Technical;
		m_state               = Stream::State::Ended;
	}
	void abort(Stream::Result &result) noexcept override {
		result  = {};
		m_state = Stream::State::Faulted;
	}
	void reset() noexcept override {
		++m_control.resets;
		m_state = Stream::State::Idle;
	}
	Stream::State state() const noexcept override { return m_state; }
	std::uint64_t nextOutputFrame() const noexcept override { return m_frame; }
};
struct Fixture {
	std::array< Control, Service::CodecSlots > controls;
	Events events;
	std::atomic< int > milliseconds{ 0 };
	std::unique_ptr< Service > service;
	explicit Fixture(bool block = false, int fail = 0, bool full = false) {
		controls[0].block     = block;
		controls[0].failAt    = fail;
		controls[0].fullBatch = full;
		Service::Processors codecs;
		for (std::size_t i = 0; i < codecs.size(); ++i) {
			codecs[i] = std::make_unique< Fake >(controls[i]);
		}
		service = std::make_unique< Service >(
			std::move(codecs), [this] { events.wake(); },
			[this] { return Service::Time{} + std::chrono::milliseconds(milliseconds.load()); });
	}
	Service::Batch next() { return events.next(*service); }
};
const std::array< unsigned char, 3 > silence{ { 0xf8, 0xff, 0xfe } }; // Valid mono 20-ms Opus silence.
Service::Input input(std::uint64_t frame, bool end, Service::Identity receiver = {}) {
	Service::Input result;
	result.payload = silence.data();
	result.bytes   = silence.size();
	result.samples = 960;
	result.frame   = frame;
	result.end     = end;
	if (receiver.epoch) {
		result.recipients.identities[0] = receiver;
		result.recipients.count         = 1;
	}
	return result;
}
void accepted(Service &service, Service::Handle handle, const Service::Input &packet) {
	CHECK(service.submit(handle, packet).status == Service::Admission::Accepted);
}
void orderingAndOwnership() {
	Fixture f(true);
	Service &s   = *f.service;
	const auto a = s.connect(1, 10, 7), b = s.connect(2, 11, 7), r = s.connect(3, 12, 7);
	const auto first                     = s.open(a);
	std::array< unsigned char, 3 > owned = silence;
	Service::Input packet                = input(0, true, r);
	packet.payload                       = owned.data();
	const auto receipt                   = s.submit(first, packet);
	CHECK(receipt.status == Service::Admission::Accepted);
	f.controls[0].wait();
	owned.fill(0); // Caller buffer can disappear while the worker is blocked.
	const auto second = s.open(b, true);
	CHECK(second);
	const auto nextReceipt = s.submit(second, input(0, true, r));
	CHECK(nextReceipt.ticket.channelTicket > receipt.ticket.channelTicket);
	Service::Batch batch;
	CHECK(!s.take(batch));
	f.controls[0].release();
	batch = f.next();
	CHECK(batch.completed.generation == first.generation);
	CHECK(batch.terminal);
	CHECK(batch.audio.packets[0].payload[0] == silence[0]);
	CHECK(batch.recipients.count == 1);
	CHECK(batch.sourceCounts[0] == 1);
	CHECK(batch.sources[0][0].channelTicket == receipt.ticket.channelTicket);
	batch = f.next();
	CHECK(batch.completed.generation == second.generation);
	CHECK(batch.bypass);
	CHECK(batch.terminal);
}
void independentChannelsAndCapacity() {
	Fixture f(false, 0, true);
	Service &s   = *f.service;
	const auto a = s.connect(1, 1, 1), b = s.connect(2, 2, 1), c = s.connect(3, 3, 2);
	const auto ha = s.open(a);
	accepted(s, ha, input(0, false));
	CHECK(f.next().audio.count == 10);
	accepted(s, ha, input(2, false)); // Leave a full ten-packet output batch undrained.
	CHECK(s.finish(ha).status == Service::Admission::Accepted);
	const auto hb = s.open(b, true), hc = s.open(c, true);
	accepted(s, hb, input(0, true));
	accepted(s, hc, input(0, true));
	bool sawC = false, aEnded = false;
	for (int i = 0; i < 4; ++i) {
		const auto batch = f.next();
		if (batch.completed.generation == hc.generation) {
			sawC = true;
		}
		if (batch.completed.generation == ha.generation && batch.terminal) {
			aEnded = true;
		}
		if (batch.completed.generation == hb.generation) {
			CHECK(aEnded);
		}
	}
	CHECK(sawC);
	CHECK(f.controls[0].calls == 2);
}
void stalledOutputAndIndependentChannel() {
	Fixture f(false, 0, true);
	Service &s   = *f.service;
	const auto h = s.open(s.connect(1, 1, 1));
	accepted(s, h, input(0, false));
	{
		std::unique_lock< std::mutex > lock(f.events.mutex);
		CHECK(f.events.changed.wait_for(lock, std::chrono::seconds(5), [&] { return f.events.count == 1; }));
	}
	for (int i = 1; i <= 4; ++i) {
		accepted(s, h, input(i * 2, false));
	}
	CHECK(f.controls[0].calls == 1); // Ten reserved output places cannot be overwritten.
	CHECK(s.finish(h).status == Service::Admission::Accepted);
	for (int i = 0; i < 5; ++i) {
		CHECK(f.next().audio.count == 10);
	}
	CHECK(f.next().terminal);
	const auto open = s.open(s.connect(2, 2, 2), true);
	accepted(s, open, input(0, false));
	CHECK(!f.next().terminal);
	const auto other = s.open(s.connect(3, 3, 3), true);
	accepted(s, other, input(0, true));
	CHECK(f.next().completed.generation == other.generation);
	s.cancel(open);
}
void receiverRevocationAndSessionReuse() {
	for (bool disconnect : { false, true }) {
		Fixture f(true);
		Service &s        = *f.service;
		const auto sender = s.connect(1, 1, 1), receiver = s.connect(2, 7, 1);
		const auto h = s.open(sender);
		accepted(s, h, input(0, true, receiver));
		f.controls[0].wait();
		if (disconnect) {
			s.disconnect(receiver);
			CHECK(s.connect(3, 7, 1).epoch);
		} else {
			s.setReceive(receiver, false);
			s.setReceive(receiver, true);
		}
		f.controls[0].release();
		CHECK(f.next().recipients.count == 0);
	}
}
void receiveChangeDoesNotCancelSender() {
	Fixture f(true);
	Service &s        = *f.service;
	const auto sender = s.connect(1, 1, 1), receiver = s.connect(2, 2, 1);
	const auto h = s.open(sender);
	accepted(s, h, input(0, true, receiver));
	f.controls[0].wait();
	s.setReceive(sender, false);
	f.controls[0].release();
	const auto batch = f.next();
	CHECK(batch.completed.generation == h.generation);
	CHECK(batch.recipients.count == 1);
	CHECK(s.statistics().cancelled == 0);
}
void admissionBounds() {
	Fixture f(true);
	Service &s        = *f.service;
	const auto sender = s.connect(1, 1, 1), hIdentity = s.connect(2, 2, 2);
	CHECK(!s.connect(1, 8, 8).epoch);
	const auto h             = s.open(sender);
	Service::Input malformed = input(0, false);
	malformed.samples        = 480;
	CHECK(s.submit(h, malformed).status == Service::Admission::Invalid);
	malformed       = input(0, false);
	malformed.bytes = Stream::MaxInputBytes + 1;
	CHECK(s.submit(h, malformed).status == Service::Admission::Invalid);
	malformed                  = input(0, false);
	malformed.recipients.count = Service::MaxRecipients + 1;
	CHECK(s.submit(h, malformed).status == Service::Admission::Invalid);
	accepted(s, h, input(0, false));
	f.controls[0].wait();
	// Media-time capacity is independent of the packet bound (60+60+20 ms).
	const std::array< unsigned char, 3 > sixty{ { 0x18, 0xff, 0xfe } };
	malformed         = input(2, false);
	malformed.payload = sixty.data();
	malformed.samples = 2880;
	accepted(s, h, malformed);
	malformed.frame = 8;
	accepted(s, h, malformed);
	CHECK(s.submit(h, input(14, false)).status == Service::Admission::InputFull);
	CHECK(s.finish(h).status == Service::Admission::Accepted);
	f.controls[0].release();
	CHECK(f.next().terminal);
	const auto witness = s.open(hIdentity, true);
	accepted(s, witness, input(0, true));
	CHECK(f.next().completed.generation == witness.generation);
}
void receiverEpochs() {
	Fixture f(true);
	Service &s   = *f.service;
	const auto a = s.connect(1, 1, 1), r = s.connect(2, 2, 1), late = s.connect(3, 3, 1);
	const auto h = s.open(a);
	accepted(s, h, input(0, true, r));
	f.controls[0].wait();
	const auto away = s.update(r, 2, true, true);
	const auto back = s.update(away, 1, true, true);
	CHECK(back.epoch != r.epoch);
	CHECK(late.epoch);
	f.controls[0].release();
	CHECK(f.next().recipients.count == 0);
}
void invalidation(int mode) {
	Fixture f(true);
	Service &s          = *f.service;
	auto sender         = s.connect(1, 55, 1);
	const auto receiver = s.connect(2, 3, 1);
	const auto old      = s.open(sender);
	accepted(s, old, input(0, true, receiver));
	f.controls[0].wait();
	if (mode == 0) {
		sender = s.update(sender, 2, true, true);
	}
	if (mode == 1) {
		s.disconnect(sender);
		sender = s.connect(3, 55, 1);
	}
	if (mode == 2) {
		sender = s.update(sender, 1, false, true);
	}
	if (mode == 3) {
		s.invalidateChannel(1);
	}
	if (mode == 4) {
		s.cancel(old);
	}
	CHECK(s.submit(old, input(2, false)).status == Service::Admission::Stale);
	const auto witness = s.connect(9, 9, 9);
	const auto hw      = s.open(witness, true);
	accepted(s, hw, input(0, true));
	f.controls[0].release();
	CHECK(f.next().completed.generation == hw.generation);
	CHECK(s.statistics().staleResults == 1);
	if (mode == 2) {
		CHECK(!s.open(sender));
	}
}
void fullInputAndControls(bool overflow) {
	Fixture f(true);
	Service &s   = *f.service;
	const auto a = s.connect(1, 1, 1);
	const auto h = s.open(a);
	accepted(s, h, input(0, false));
	f.controls[0].wait();
	constexpr int queued = Service::InputSamples / 960;
	for (int i = 1; i <= queued; ++i) {
		accepted(s, h, input(i * 2, false));
	}
	if (overflow) {
		CHECK(s.submit(h, input((queued + 1) * 2, true)).status == Service::Admission::InputFull);
		f.controls[0].release();
		const auto batch = f.next();
		CHECK(batch.terminal);
		CHECK(batch.failure == Service::Failure::InputFull);
		CHECK(batch.audio.count == 0);
		CHECK(s.statistics().inputFull == 1);
		CHECK(f.controls[0].calls == 1);
	} else {
		CHECK(s.finish(h).status == Service::Admission::Accepted);
		CHECK(s.finish(h).status == Service::Admission::Closed);
		f.controls[0].release();
		for (int i = 0; i < queued + 1; ++i) {
			CHECK(!f.next().terminal);
		}
		const auto batch = f.next();
		CHECK(batch.terminal);
		CHECK(batch.completed.technical);
		CHECK(batch.audio.packets[0].end == Stream::End::Technical);
	}
}
void expiry(bool inFlight) {
	Fixture f(true);
	Service &s   = *f.service;
	const auto h = s.open(s.connect(1, 1, 1));
	accepted(s, h, input(0, false));
	f.controls[0].wait();
	if (!inFlight) {
		accepted(s, h, input(2, false));
	}
	f.milliseconds = 41;
	f.controls[0].release();
	const auto batch = f.next();
	CHECK(batch.failure == Service::Failure::Expired);
	CHECK(batch.audio.count == 0);
	CHECK(s.submit(h, input(4, false)).status == Service::Admission::Faulted);
	CHECK(s.finish(h).status == Service::Admission::Accepted);
	CHECK(f.next().terminal);
}
void faultLatch() {
	Fixture f(false, 2);
	Service &s   = *f.service;
	const auto h = s.open(s.connect(1, 1, 1));
	accepted(s, h, input(0, false));
	CHECK(!f.next().bypass);
	accepted(s, h, input(2, false));
	const auto failed = f.next();
	CHECK(failed.failure == Service::Failure::Codec);
	CHECK(failed.codecFailure == Service::CodecFailure::Stream);
	CHECK(failed.audio.error == Stream::Error::Encoder);
	CHECK(s.statistics().codecStream == 1);
	CHECK(!failed.bypass);
	CHECK(s.submit(h, input(4, false)).status == Service::Admission::Faulted);
	CHECK(s.finish(h).status == Service::Admission::Accepted);
	CHECK(f.next().terminal);
	CHECK(f.controls[0].calls == 2);
}
void bypassLatchAndSlots() {
	Fixture f(false, 1);
	Service &s   = *f.service;
	const auto h = s.open(s.connect(1, 1, 1));
	accepted(s, h, input(0, false));
	CHECK(f.next().bypass);
	accepted(s, h, input(2, true));
	CHECK(f.next().bypass);
	CHECK(f.controls[0].calls == 1);
	std::array< Service::Handle, Service::MaxTransmissions > handles;
	// A separate service ensures all sixteen fixed lane slots start free.
	Fixture pool;
	Service &p = *pool.service;
	for (std::size_t i = 0; i < handles.size(); ++i) {
		handles[i] = p.open(p.connect(i + 1, i + 1, i + 1));
		CHECK(handles[i]);
	}
	CHECK(!p.open(p.connect(100, 100, 100)));
	for (std::size_t i = 0; i < handles.size(); ++i) {
		accepted(p, handles[i], input(0, true));
	}
	int bypasses = 0;
	for (std::size_t i = 0; i < handles.size(); ++i) {
		if (pool.next().bypass) {
			++bypasses;
		}
	}
	CHECK(bypasses == 12);
	CHECK(p.statistics().noLane == 1);
}
void mediaClockAndSlotReuse() {
	Fixture f;
	Service &s        = *f.service;
	const auto sender = s.connect(1, 1, 1);
	const auto first  = s.open(sender);
	accepted(s, first, input(100, true));
	const auto second = s.open(sender);
	CHECK(second); // Different reserved codec slot, before dispatch of A.
	accepted(s, second, input(0, true));
	CHECK(f.next().audio.packets[0].frame == 100);
	CHECK(f.next().audio.packets[0].frame >= 102);
	CHECK(f.controls[1].floor >= 102);
	CHECK(s.submit(first, input(104, false)).status != Service::Admission::Accepted);
	const auto third = s.open(sender);
	CHECK(third);
	CHECK(third.lane == first.lane);
	CHECK(third.generation != first.generation);
	accepted(s, third, input(0, true));
	CHECK(f.next().audio.packets[0].frame >= 104);
}
void movePreservesConnectionClock() {
	Fixture f(true);
	Service &s     = *f.service;
	auto sender    = s.connect(1, 1, 1);
	const auto old = s.open(sender);
	accepted(s, old, input(100, false));
	f.controls[0].wait();
	sender          = s.update(sender, 2, true, true);
	sender          = s.update(sender, 1, true, true);
	const auto next = s.open(sender);
	accepted(s, next, input(0, true));
	f.controls[0].release();
	const auto batch = f.next();
	CHECK(batch.completed.generation == next.generation);
	CHECK(batch.audio.packets[0].frame >= 102);
	CHECK(f.controls[1].floor >= 102);
}
void shutdownAndRestart() {
	for (int repetition = 0; repetition < 12; ++repetition) {
		Fixture f(true);
		Service &s   = *f.service;
		const auto h = s.open(s.connect(1, 1, 1));
		accepted(s, h, input(0, true));
		f.controls[0].wait();
		std::thread stopper([&] { s.shutdown(); });
		f.controls[0].release();
		stopper.join();
		Service::Batch batch;
		CHECK(!s.take(batch));
		CHECK(!s.open(s.connect(2, 2, 2)));
		CHECK(s.submit(h, input(2, false)).status == Service::Admission::Stopped);
		for (const auto &control : f.controls) {
			CHECK(control.resets >= 1);
		}
		s.shutdown();
	}
}
void concurrentProducers() {
	Fixture f;
	Service &s        = *f.service;
	const auto sender = s.connect(1, 1, 1);
	const auto h      = s.open(sender, true);
	std::array< Service::Receipt, 4 > receipts;
	std::thread udp([&] {
		receipts[0] = s.submit(h, input(0, false));
		receipts[1] = s.submit(h, input(2, false));
	});
	std::thread tcp([&] {
		receipts[2] = s.submit(h, input(4, false));
		receipts[3] = s.submit(h, input(6, false));
	});
	udp.join();
	tcp.join();
	CHECK(s.finish(h).status == Service::Admission::Accepted);
	for (const auto &receipt : receipts) {
		CHECK(receipt.status == Service::Admission::Accepted);
	}
	std::uint64_t previous = 0;
	for (int i = 0; i < 5; ++i) {
		const auto batch = f.next();
		CHECK(batch.completed.channelTicket > previous);
		previous = batch.completed.channelTicket;
	}
}
std::vector< unsigned char > encode(int samples) {
	int error            = 0;
	OpusEncoder *encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);
	CHECK(error == OPUS_OK);
	std::vector< float > pcm(samples, 0.02f);
	std::vector< unsigned char > payload(1024);
	const int bytes = opus_encode_float(encoder, pcm.data(), samples, payload.data(), payload.size());
	opus_encoder_destroy(encoder);
	CHECK(bytes > 0);
	payload.resize(bytes);
	return payload;
}
void realFaultReserve(bool expiredOutput) {
	Events events;
	std::atomic< int > milliseconds{ 0 };
	Service s(
		funkvoice::profile("cb-am"), [&] { events.wake(); },
		[&] { return Service::Time{} + std::chrono::milliseconds(milliseconds.load()); });
	const auto sender = s.connect(1, 1, 1), receiver = s.connect(2, 2, 1);
	const auto h = s.open(sender);
	accepted(s, h, input(0, false, receiver));
	{
		std::unique_lock< std::mutex > lock(events.mutex);
		CHECK(events.changed.wait_for(lock, std::chrono::seconds(5), [&] { return events.count == 1; }));
	}
	if (expiredOutput) {
		milliseconds = 41;
	} else {
		constexpr int queued = Service::InputSamples / 960;
		for (int i = 1; i <= queued; ++i) {
			accepted(s, h, input(i * 2, false, receiver));
		}
		CHECK(s.submit(h, input((queued + 1) * 2, false, receiver)).status == Service::Admission::InputFull);
	}
	const auto fault = events.next(s);
	CHECK(fault.failure == (expiredOutput ? Service::Failure::Expired : Service::Failure::InputFull));
	CHECK(!fault.bypass);
	CHECK(fault.audio.count == 1);
	CHECK(fault.audio.packets[0].end == Stream::End::Fault);
	CHECK(fault.sourceCounts[0] == 1);
	CHECK(fault.recipients.count == 1);
	CHECK(s.finish(h).status == Service::Admission::Accepted);
	const auto end = events.next(s);
	CHECK(end.terminal);
	CHECK(end.audio.count == 0);
}
// Keep the first result unconsumed, as happens while a TCP readyRead callback
// drains buffered messages. The worker cannot free another input place then.
// Freeze only the injected job clock: this is a capacity test, not an expiry test.
void realBurstBudget() {
	for (bool previousGeneration : { false, true }) {
		for (int samples : { 960, 480, 1920, 2880, 5760 }) {
			Events events;
			Service s(
				funkvoice::profile("cb-am"), [&] { events.wake(); }, [] { return Service::Time{}; });
			const auto sender = s.connect(1, 1, 1), receiver = s.connect(2, 2, 1);
			auto handle                  = s.open(sender);
			const auto initialGeneration = handle.generation;
			const auto payload           = encode(samples);
			auto packet                  = input(0, false, receiver);
			packet.payload               = payload.data();
			packet.bytes                 = payload.size();
			packet.samples               = samples;
			packet.end                   = previousGeneration;
			accepted(s, handle, packet);
			{
				std::unique_lock< std::mutex > lock(events.mutex);
				CHECK(events.changed.wait_for(lock, std::chrono::seconds(5), [&] { return events.count == 1; }));
			}
			if (previousGeneration) {
				handle = s.open(sender);
				CHECK(bool(handle));
			}
			packet.end       = false;
			const int queued = Service::InputSamples / samples;
			for (int i = 1; i <= queued; ++i) {
				packet.frame       = std::uint64_t(i * samples / 480);
				const auto receipt = s.submit(handle, packet);
				if (receipt.status != Service::Admission::Accepted)
					std::cerr << "burst admission failed: samples=" << samples << " queued=" << i
							  << " inputFull=" << s.statistics().inputFull << '\n';
				CHECK(receipt.status == Service::Admission::Accepted);
				CHECK(receipt.queuedPackets == std::uint32_t(i));
				CHECK(receipt.queuedSamples == std::uint32_t(i * samples));
			}
			packet.frame += samples / 480;
			packet.payload = nullptr;
			packet.bytes   = 0;
			packet.samples = 0;
			packet.end     = true;
			accepted(s, handle, packet); // Reserved room for a confirmed empty terminator.
			int error            = OPUS_OK;
			OpusDecoder *decoder = opus_decoder_create(48000, 1, &error);
			CHECK(error == OPUS_OK);
			std::size_t output = 0;
			bool terminal      = false;
			std::uint64_t last = 0;
			for (int i = 0; i < queued + 2; ++i) {
				const auto batch = events.next(s);
				CHECK(batch.failure == Service::Failure::None && !batch.bypass);
				CHECK(batch.recipients.count == 1 && batch.recipients.identities[0] == receiver);
				for (std::size_t p = 0; p < batch.audio.count; ++p) {
					const auto &out = batch.audio.packets[p];
					CHECK(!output || out.frame > last);
					last = out.frame;
					++output;
					float pcm[960];
					CHECK(opus_decode_float(decoder, out.payload.data(), out.bytes, pcm, 960, 0) == 960);
					for (std::size_t j = 0; j < batch.sourceCounts[p]; ++j)
						CHECK(batch.sources[p][j].generation == (i == 0 ? initialGeneration : handle.generation));
					terminal = out.end == Stream::End::InputMarker;
				}
				CHECK(batch.terminal == (i == queued + 1 || (previousGeneration && i == 0)));
				if (batch.terminal)
					CHECK(opus_decoder_ctl(decoder, OPUS_RESET_STATE) == OPUS_OK);
			}
			opus_decoder_destroy(decoder);
			CHECK(output > 0 && terminal);
			CHECK(s.statistics().inputFull == 0 && s.statistics().codecErrors == 0);
			CHECK(s.statistics().peakInputSamples == Service::InputSamples);
		}
	}
}
void faultedBurstRecovery(bool marker) {
	Fixture f(true);
	Service &s        = *f.service;
	const auto sender = s.connect(1, 1, 1), nextSender = s.connect(2, 2, 1);
	const auto old = s.open(sender);
	accepted(s, old, input(0, false));
	f.controls[0].wait();
	constexpr int queued = Service::InputSamples / 960;
	for (int i = 1; i <= queued; ++i)
		accepted(s, old, input(i * 2, false));
	const auto full = s.submit(old, input((queued + 1) * 2, false));
	CHECK(full.status == Service::Admission::InputFull);
	CHECK(full.queuedPackets == queued && full.queuedSamples == Service::InputSamples);
	for (int i = queued + 2; i < queued + 5; ++i)
		CHECK(s.submit(old, input(i * 2, false)).status == Service::Admission::Faulted);
	CHECK(!s.open(nextSender)); // Reporting a fault is not a channel boundary.
	if (marker)
		CHECK(s.submit(old, input(30, true)).status == Service::Admission::Faulted);
	else
		CHECK(s.finish(old, 30).status == Service::Admission::Accepted);
	const auto fresh = s.open(nextSender);
	CHECK(bool(fresh));
	accepted(s, fresh, input(100, true));
	Service::Batch pending;
	CHECK(!s.take(pending)); // No successor can cross the old generation's barrier.
	f.controls[0].release();
	const auto end = f.next();
	CHECK(end.terminal && end.failure == Service::Failure::InputFull && end.audio.count == 0);
	CHECK(end.completed.technical != marker);
	const auto next = f.next();
	CHECK(next.completed.generation == fresh.generation && next.terminal && !next.bypass);
	CHECK(next.failure == Service::Failure::None);
	CHECK(s.statistics().inputFull == 1 && s.statistics().sampleLimitHits == 1);
	CHECK(s.statistics().packetLimitHits == 0 && s.statistics().codecErrors == 0);
}
void realCodecFailureReasons() {
	for (bool malformed : { false, true }) {
		Events events;
		Service s(
			funkvoice::profile("cb-am"), [&] { events.wake(); }, [] { return Service::Time{}; });
		const auto h = s.open(s.connect(1, 1, 1));
		accepted(s, h, input(0, false));
		events.next(s); // Establish real encoded state, independently of output delivery.
		auto packet                   = input(1, false); // Advancing but overlapping source frame -> Ignored.
		const unsigned char invalid[] = { 0xfa, 0xff };  // Header duration is valid; packet_parse fails.
		if (malformed) {
			packet.frame   = 2;
			packet.payload = invalid;
			packet.bytes   = sizeof(invalid);
			packet.samples = 1920;
		}
		accepted(s, h, packet);
		const auto batch = events.next(s);
		CHECK(batch.failure == Service::Failure::Codec && !batch.bypass);
		CHECK(batch.codecFailure == (malformed ? Service::CodecFailure::Stream : Service::CodecFailure::Ignored));
		CHECK(batch.audio.error == (malformed ? Stream::Error::InvalidPayload : Stream::Error::None));
		CHECK(batch.audio.codecError == (malformed ? OPUS_INVALID_PACKET : OPUS_OK));
		CHECK(s.statistics().codecStream == std::uint64_t(malformed));
		CHECK(s.statistics().codecIgnored == std::uint64_t(!malformed));
		CHECK(s.statistics().inputFull == 0 && s.statistics().codecErrors == 1);
		CHECK(s.submit(h, input(10, false)).status == Service::Admission::Faulted);
		CHECK(s.finish(h).status == Service::Admission::Accepted);
		CHECK(events.next(s).terminal);
	}
}
void packetCapacityBound() {
	Fixture f(true);
	Service &s   = *f.service;
	const auto h = s.open(s.connect(1, 1, 1));
	accepted(s, h, input(0, false));
	f.controls[0].wait();
	const auto payload = encode(240);
	auto packet        = input(2, false);
	packet.payload     = payload.data();
	packet.bytes       = payload.size();
	packet.samples     = 240;
	for (std::size_t i = 0; i < Service::InputPackets; ++i) {
		packet.frame = i + 2;
		accepted(s, h, packet);
	}
	packet.frame    = 100;
	const auto full = s.submit(h, packet);
	CHECK(full.status == Service::Admission::InputFull && full.queuedPackets == Service::InputPackets);
	CHECK(full.queuedSamples < Service::InputSamples);
	CHECK(s.statistics().packetLimitHits == 1 && s.statistics().sampleLimitHits == 0);
	CHECK(s.finish(h).status == Service::Admission::Accepted);
	f.controls[0].release();
	CHECK(f.next().terminal && s.statistics().codecErrors == 0);
}
void internalCodecFailureReasons() {
	for (bool begin : { false, true }) {
		Fixture f;
		f.controls[0].failBegin      = begin;
		f.controls[0].invalidSources = !begin;
		Service &s                   = *f.service;
		const auto h                 = s.open(s.connect(1, 1, 1));
		accepted(s, h, input(0, true));
		const auto batch = f.next();
		CHECK(batch.failure == Service::Failure::Codec && batch.audio.count == 0);
		CHECK(batch.codecFailure == (begin ? Service::CodecFailure::Begin : Service::CodecFailure::SourceMap));
		CHECK(s.statistics().codecBegin == std::uint64_t(begin));
		CHECK(s.statistics().codecSourceMap == std::uint64_t(!begin));
	}
}
void realCodec() {
	Events events;
	Service s(
		funkvoice::profile("cb-am"), [&] { events.wake(); }, [] { return Service::Time{}; });
	const auto sender = s.connect(1, 1, 1), receiver = s.connect(2, 2, 1), late = s.connect(3, 3, 1);
	const auto h          = s.open(sender);
	const auto ten        = encode(480);
	Service::Input packet = input(0, false, receiver);
	packet.payload        = ten.data();
	packet.bytes          = ten.size();
	packet.samples        = 480;
	accepted(s, h, packet);
	CHECK(events.next(s).audio.count == 0);
	packet.frame                    = 1;
	packet.end                      = true;
	packet.recipients.identities[1] = late;
	packet.recipients.count         = 2;
	accepted(s, h, packet);
	const auto batch = events.next(s);
	CHECK(batch.terminal);
	CHECK(batch.audio.count >= 2);
	CHECK(batch.recipients.count == 1);
	bool first = false, second = false;
	int error            = 0;
	OpusDecoder *decoder = opus_decoder_create(48000, 1, &error);
	CHECK(error == OPUS_OK);
	for (std::size_t i = 0; i < batch.audio.count; ++i) {
		float pcm[960];
		const auto &out = batch.audio.packets[i];
		CHECK(opus_decode_float(decoder, out.payload.data(), out.bytes, pcm, 960, 0) == 960);
		for (std::size_t j = 0; j < batch.sourceCounts[i]; ++j) {
			first  = first || batch.sources[i][j].sourceFrame == 0;
			second = second || batch.sources[i][j].sourceFrame == 1;
		}
	}
	opus_decoder_destroy(decoder);
	CHECK(first && second);
	CHECK(batch.audio.packets[batch.audio.count - 1].end == Stream::End::InputMarker);
	// A half-frame retained before an unsupported 5-ms packet must survive the
	// complete pre-encode bypass decision, even with a nonzero source start.
	const auto shortPacket  = encode(240);
	const auto bypass       = s.open(sender);
	packet.frame            = 10;
	packet.end              = false;
	packet.recipients.count = 1;
	accepted(s, bypass, packet);
	CHECK(events.next(s).audio.count == 0);
	packet.frame   = 11;
	packet.end     = true;
	packet.payload = shortPacket.data();
	packet.bytes   = shortPacket.size();
	packet.samples = 240;
	accepted(s, bypass, packet);
	const auto original = events.next(s);
	CHECK(original.bypass);
	CHECK(original.audio.count == 2);
	CHECK(original.audio.packets[0].bytes == ten.size());
	CHECK(original.audio.packets[0].frame == 10);
	CHECK(original.audio.packets[1].bytes == shortPacket.size());
}
#ifdef VOICE_TEST_SIMPLEX
void simplexHandoff() {
	Fixture f(true);
	Service &s = *f.service;
	SimplexController controller(300, 500, {}, [] { return 1000; });
	const auto a = s.connect(1, 1, 1), b = s.connect(2, 2, 1);
	SimplexController::Packet packet;
	packet.context = { 1, 1, 1, QStringLiteral("A") };
	packet.last    = true;
	auto guard     = controller.begin(packet);
	CHECK(guard.allowed);
	CHECK(guard.started);
	CHECK(guard.lock.owns_lock());
	const auto ha = s.open(a);
	const auto ra = s.submit(ha, input(0, true));
	f.controls[0].wait();
	Service::Receipt rb;
	std::thread successor([&] {
		SimplexController::Packet next;
		next.context   = { 2, 2, 1, QStringLiteral("B") };
		next.last      = true;
		next.udp       = true;
		auto nextGuard = controller.begin(next);
		CHECK(nextGuard.allowed);
		CHECK(nextGuard.started);
		const auto hb = s.open(b, true);
		CHECK(hb);
		rb = s.submit(hb, input(0, true));
	});
	guard.lock.unlock();
	successor.join();
	CHECK(rb.status == Service::Admission::Accepted);
	CHECK(rb.ticket.channelTicket > ra.ticket.channelTicket);
	f.controls[0].release();
	CHECK(f.next().completed.sender == a);
	CHECK(f.next().completed.sender == b);
	// Real controller serializes duplicate TCP/UDP input before ticket admission.
	const auto c  = s.connect(3, 3, 2);
	const auto hc = s.open(c, true);
	std::atomic< int > allowed{ 0 };
	auto receive = [&](bool udp) {
		SimplexController::Packet next;
		next.context      = { 3, 3, 2, QStringLiteral("C") };
		next.udp          = udp;
		auto transmission = controller.begin(next);
		if (transmission.allowed) {
			++allowed;
			accepted(s, hc, input(0, false));
		}
	};
	std::thread udp(receive, true), tcp(receive, false);
	udp.join();
	tcp.join();
	CHECK(allowed == 1);
	CHECK(f.next().completed.sender == c);
	packet.context = { 3, 3, 2, QStringLiteral("C") };
	packet.payload = false;
	guard          = controller.begin(packet);
	CHECK(guard.allowed);
	Service::Input end;
	end.end = true;
	accepted(s, hc, end);
	guard.lock.unlock();
	CHECK(f.next().terminal);
}
#endif
} // namespace
int main(int argc, char **argv) {
	if (argc == 2 && std::string(argv[1]) == "--burst-only") {
		realBurstBudget();
		return 0;
	}
#ifdef VOICE_TEST_SIMPLEX
	simplexHandoff();
#endif
	orderingAndOwnership();
	independentChannelsAndCapacity();
	receiverEpochs();
	receiverRevocationAndSessionReuse();
	stalledOutputAndIndependentChannel();
	admissionBounds();
	receiveChangeDoesNotCancelSender();
	for (int mode = 0; mode < 5; ++mode) {
		invalidation(mode);
	}
	fullInputAndControls(false);
	fullInputAndControls(true);
	expiry(false);
	expiry(true);
	faultLatch();
	bypassLatchAndSlots();
	mediaClockAndSlotReuse();
	movePreservesConnectionClock();
	shutdownAndRestart();
	concurrentProducers();
	realCodec();
	realBurstBudget();
	faultedBurstRecovery(false);
	faultedBurstRecovery(true);
	realCodecFailureReasons();
	packetCapacityBound();
	internalCodecFailureReasons();
	realFaultReserve(false);
	realFaultReserve(true);
	std::cout << "fixed service bytes=" << Service::fixedStorageBytes() << ", batch bytes=" << sizeof(Service::Batch)
			  << "\n";
	std::cout << "VoiceService: deterministic ordering, lifetime, overload and real Opus tests passed\n";
}
