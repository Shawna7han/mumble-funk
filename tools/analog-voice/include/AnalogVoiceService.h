// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#pragma once

#include "AnalogVoiceStream.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace funkvoice {

// An opt-in, server-independent owner of one worker. No sockets or server pointers.
class AnalogVoiceService {
public:
	static constexpr std::size_t CodecSlots       = 4;
	static constexpr std::size_t MaxTransmissions = 16;
	static constexpr std::size_t MaxConnections   = 128;
	static constexpr std::size_t MaxRecipients    = 64;
	static constexpr std::uint32_t InputSamples   = 5760;
	// The existing 120-ms media budget must also fit its smallest supported
	// (10-ms) packets, plus one empty confirmed terminator. This is a bounded
	// burst envelope, not permission to exceed the unchanged 40-ms job age.
	static constexpr std::size_t InputPackets     = InputSamples / (FrameSamples / 2) + 1;
	using Time                                    = std::chrono::steady_clock::time_point;
	using Clock                                   = std::function< Time() >;
	// Must not throw; only enqueue a wakeup, never dispatch inline or wait for the main thread.
	using Wake = std::function< void() >;

	struct Identity {
		std::uint64_t connection = 0, epoch = 0;
		std::uint32_t session = 0, channel = 0;
		std::uint64_t lifetime = 0;
		bool operator==(const Identity &other) const noexcept;
	};
	struct Recipients {
		std::array< Identity, MaxRecipients > identities{};
		std::size_t count = 0;
	};
	struct Handle {
		std::uint64_t generation = 0;
		std::size_t lane         = MaxTransmissions;
		explicit operator bool() const noexcept { return generation != 0; }
	};
	// Copied with source tickets, never borrowed from a network decoder. The
	// audience prevents a later context match from revealing old positions.
	struct Metadata {
		std::array< float, 3 > position{};
		std::array< std::uint64_t, MaxRecipients > positionalConnections{};
		bool positional = false;
	};
	struct Ticket {
		Identity sender;
		std::uint64_t generation = 0, routingGeneration = 0, channelTicket = 0;
		std::uint64_t sourceFrame = 0;
		std::uint32_t samples     = 0;
		bool end = false, technical = false;
		Time accepted{};
		Metadata metadata;
	};
	struct Input {
		const unsigned char *payload = nullptr;
		std::size_t bytes            = 0;
		std::uint64_t frame          = 0;
		// Actual 48-kHz packet duration, including unsupported 2.5/5-ms packets.
		std::uint32_t samples = 0;
		bool end              = false;
		Recipients recipients;
		Metadata metadata;
	};
	enum class Admission { Accepted, Stopped, Stale, Invalid, InputFull, Faulted, Closed };
	enum class Failure { None, InputFull, Expired, Codec, Sequence };
	enum class CodecFailure { None, Begin, Stream, Ignored, SourceMap };
	struct Receipt {
		Admission status = Admission::Invalid;
		Ticket ticket;
		// Snapshot before an overflow faults/clears the queue, otherwise after admission.
		std::uint32_t queuedPackets = 0, queuedSamples = 0;
	};
	struct Batch {
		Handle handle; // Stable lane/generation for bounded dispatcher lifecycle state.
		Ticket completed;
		AnalogVoiceStream::Result audio;
		Recipients recipients;
		// Source tickets for each packet's Source spans, in span order. Technical
		// spans do not invent source events. Metadata survives codec lookahead.
		std::array< std::array< Ticket, AnalogVoiceStream::MaxSpans >, AnalogVoiceStream::MaxOutputPackets > sources{};
		std::array< std::size_t, AnalogVoiceStream::MaxOutputPackets > sourceCounts{};
		Failure failure = Failure::None;
		CodecFailure codecFailure = CodecFailure::None;
		bool bypass = false, terminal = false;
	};
	struct Statistics {
		std::uint64_t accepted = 0, inputFull = 0, expired = 0, codecErrors = 0;
		std::uint64_t staleResults = 0, cancelled = 0, bypassStarts = 0, noLane = 0;
		std::uint64_t openBusy = 0, openInvalid = 0, stale = 0, invalid = 0, closed = 0, stopped = 0, faulted = 0;
		std::uint64_t processedJobs = 0, publishedPackets = 0, takenPackets = 0;
		std::uint64_t packetLimitHits = 0, sampleLimitHits = 0, workerWaits = 0;
		std::uint64_t peakInputPackets = 0, peakInputSamples = 0;
		std::uint64_t maxWorkerStartAgeUs = 0, maxWorkerFinishAgeUs = 0, maxTakeAgeUs = 0;
		std::uint64_t codecBegin = 0, codecStream = 0, codecIgnored = 0, codecSourceMap = 0;
	};

	// Injection boundary for deterministic scheduling/fault tests. Only the worker
	// calls these methods; four instances are constructed before accepting work.
	class Processor {
	public:
		virtual ~Processor()                                                                         = default;
		virtual bool begin(std::uint64_t frame, std::uint64_t floor) noexcept                        = 0;
		virtual void process(const AnalogVoiceStream::Input &, AnalogVoiceStream::Result &) noexcept = 0;
		virtual void finish(AnalogVoiceStream::Result &) noexcept                                    = 0;
		virtual void abort(AnalogVoiceStream::Result &) noexcept                                     = 0;
		virtual void reset() noexcept                                                                = 0;
		virtual AnalogVoiceStream::State state() const noexcept                                      = 0;
		virtual std::uint64_t nextOutputFrame() const noexcept                                       = 0;
	};
	using Processors = std::array< std::unique_ptr< Processor >, CodecSlots >;
	explicit AnalogVoiceService(const Parameters &, Wake wake = {}, Clock clock = {});
	AnalogVoiceService(Processors, Wake wake = {}, Clock clock = {});
	~AnalogVoiceService();
	AnalogVoiceService(const AnalogVoiceService &) = delete;
	AnalogVoiceService &operator=(const AnalogVoiceService &) = delete;

	// Control-plane calls. Every change gets a fresh routing epoch, even a return
	// to the same channel. A reconnect must register again; session is not a key.
	Identity connect(std::uint64_t connection, std::uint32_t session, std::uint32_t channel, bool maySend = true,
					 bool mayReceive = true);
	Identity update(Identity, std::uint32_t channel, bool maySend, bool mayReceive);
	// Receive-only changes must not invalidate this connection's own transmission.
	// Remove it irrevocably from every pending generation's recipient snapshot.
	void setReceive(Identity, bool mayReceive);
	void disconnect(Identity);
	void invalidateChannel(std::uint32_t channel);

	// Caller already decided eligibility. Open/submit/finish must be ordered under
	// the existing Simplex Transmission guard. An old open channel must first close.
	Handle open(Identity sender, bool bypass = false);
	Receipt submit(Handle, const Input &);
	Receipt finish(Handle, std::uint64_t lastSourceFrame = 0,
				   Time endedAt = {}); // Reserved technical end; works with a full data queue.
	void cancel(Handle);    // Reserved cancellation; never queues a fallible event.

	// Main-thread dispatcher: acknowledge once at entry, then take until false.
	// Re-resolve recipients under the server voice lock before network submission.
	void acknowledgeNotification();
	bool take(Batch &);
	Statistics statistics() const;
	// Fixed bookkeeping/queues, excluding codec objects, thread stack and libraries.
	static std::size_t fixedStorageBytes() noexcept;
	void shutdown(); // Owner thread, outside server locks; concurrent producers may exit.

private:
	struct Impl;
	std::unique_ptr< Impl > m_impl;
};
} // namespace funkvoice
