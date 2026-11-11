// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "AnalogVoiceService.h"

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace funkvoice {
namespace {
	using Stream  = AnalogVoiceStream;
	using Service = AnalogVoiceService;
	class LiveProcessor final : public Service::Processor {
	public:
		explicit LiveProcessor(const Parameters &parameters) : m_stream(parameters) {}
		bool begin(std::uint64_t frame, std::uint64_t floor) noexcept override { return m_stream.begin(frame, floor); }
		void process(const Stream::Input &input, Stream::Result &result) noexcept override {
			m_stream.process(input, result);
		}
		void finish(Stream::Result &result) noexcept override { m_stream.finish(result); }
		void reset() noexcept override { m_stream.reset(); }
		void abort(Stream::Result &result) noexcept override { m_stream.abort(result); }
		Stream::State state() const noexcept override { return m_stream.state(); }
		std::uint64_t nextOutputFrame() const noexcept override { return m_stream.nextOutputFrame(); }

	private:
		Stream m_stream;
	};
	Service::Processors processors(const Parameters &parameters) {
		Service::Processors result;
		for (auto &processor : result) {
			processor = std::make_unique< LiveProcessor >(parameters);
		}
		return result;
	}
	bool contains(const Service::Recipients &set, const Service::Identity &identity) {
		return std::find(set.identities.begin(), set.identities.begin() + set.count, identity)
			   != set.identities.begin() + set.count;
	}
	std::uint64_t ageUs(Service::Time now, Service::Time accepted) {
		return static_cast<std::uint64_t>(std::max<std::int64_t>(0,
			std::chrono::duration_cast<std::chrono::microseconds>(now - accepted).count()));
	}
	void intersect(Service::Recipients &set, const Service::Recipients &other) {
		std::size_t count = 0;
		for (std::size_t i = 0; i < set.count; ++i) {
			if (contains(other, set.identities[i])) {
				set.identities[count++] = set.identities[i];
			}
		}
		set.count = count;
	}
} // namespace

bool AnalogVoiceService::Identity::operator==(const Identity &other) const noexcept {
	return connection == other.connection && lifetime == other.lifetime && epoch == other.epoch
		   && session == other.session && channel == other.channel;
}

struct AnalogVoiceService::Impl {
	struct Connection {
		Identity identity;
		bool send = false, receive = false;
		std::uint64_t floor = 0;
	};
	struct Job {
		Ticket ticket;
		std::array< unsigned char, Stream::MaxInputBytes > payload{};
		std::size_t bytes = 0;
		Recipients recipients;
	};
	struct Lane {
		Handle handle;
		Identity sender;
		std::size_t slot    = CodecSlots;
		std::uint64_t order = 0;
		std::array< Job, InputPackets > queue{};
		std::size_t head = 0, count = 0;
		std::uint32_t samples = 0;
		Job end;
		bool closing = false, cancelled = false, begun = false;
		bool bypass = false, ready = false, terminalTaken = false, faultReported = false;
		Failure failure = Failure::None;
		Recipients recipients;
		bool hasRecipients = false, encoded = false;
		Batch output;
		// At most one half-frame + lookahead + reserve survives a call. Eight
		// most recent source records cover this even for 10-ms input packets.
		std::array< Ticket, 8 > history{};
		std::size_t historyNext = 0;
		// Pre-encode fallback retains only the existing half/lookahead/reserve
		// window. Input burst capacity must not enlarge this separate buffer.
		std::array< Job, 4 > originals{};
		std::size_t originalCount = 0;
	};
	mutable std::mutex mutex;
	std::condition_variable changed;
	Processors codecs;
	Wake wake;
	Clock clock;
	std::array< Connection, MaxConnections > connections{};
	std::array< Lane, MaxTransmissions > lanes{};
	std::array< bool, CodecSlots > occupied{};
	Statistics stats;
	std::uint64_t sequence = 0;
	std::size_t nextLane = 0, nextDelivery = 0;
	bool stopping = false, notified = false;
	std::thread worker;

	Impl(Processors values, Wake notification, Clock time)
		: codecs(std::move(values)), wake(std::move(notification)), clock(std::move(time)) {
		for (const auto &codec : codecs) {
			if (!codec) {
				throw std::invalid_argument("four audio processors required");
			}
		}
		if (!clock) {
			clock = [] { return std::chrono::steady_clock::now(); };
		}
		worker = std::thread([this] { run(); });
	}
	std::uint64_t serial() {
		// Exhaustion refuses new identities/tickets rather than wrapping epochs.
		if (sequence == std::numeric_limits< std::uint64_t >::max()) {
			return 0;
		}
		return ++sequence;
	}
	Connection *connection(Identity identity) {
		for (auto &entry : connections) {
			if (identity.epoch && entry.identity == identity) {
				return &entry;
			}
		}
		return nullptr;
	}
	Lane *lane(Handle handle) {
		if (!handle || handle.lane >= lanes.size()) {
			return nullptr;
		}
		Lane &entry = lanes[handle.lane];
		return entry.handle.generation == handle.generation && !entry.cancelled ? &entry : nullptr;
	}
	void abort(Lane &entry) {
		if (!entry.cancelled) {
			++stats.cancelled;
		}
		entry.cancelled = true;
		entry.ready     = false;
		entry.count     = 0;
		entry.samples   = 0;
		changed.notify_one();
	}
	void invalidate(Identity identity) {
		for (auto &entry : lanes) {
			if (entry.handle && entry.sender == identity) {
				abort(entry);
			}
		}
	}
	bool first(const Lane &entry) const {
		for (const auto &previous : lanes) {
			if (previous.handle && previous.sender.channel == entry.sender.channel && previous.order < entry.order) {
				return false;
			}
		}
		return true;
	}
	bool deliverable() const {
		for (const auto &entry : lanes) {
			if (entry.handle && entry.ready && !entry.cancelled && first(entry)) {
				return true;
			}
		}
		return false;
	}
	void fault(Lane &entry, Failure reason) {
		if (entry.failure == Failure::None) {
			entry.failure = reason;
		}
		entry.count         = 0;
		entry.samples       = 0;
		entry.originalCount = 0;
		entry.ready         = false;
		changed.notify_one();
	}
	Ticket ticket(Lane &entry, std::uint64_t frame, std::uint32_t samples, bool end, bool technical) {
		return { entry.sender,
				 entry.handle.generation,
				 entry.sender.epoch,
				 serial(),
				 frame,
				 samples,
				 end,
				 technical,
				 clock(),
				 {} };
	}
	Receipt reject(Admission status, const Ticket &value = {}) {
		switch (status) {
			case Admission::Stopped: ++stats.stopped; break;
			case Admission::Stale: ++stats.stale; break;
			case Admission::Invalid: ++stats.invalid; break;
			case Admission::Closed: ++stats.closed; break;
			case Admission::Faulted: ++stats.faulted; break;
			default: break;
		}
		return {status, value};
	}
	// Called with the mutex held. Result capacity belongs exclusively to this
	// worker from selection until publication; producers can only invalidate it.
	bool workAvailable(const Lane &entry) const {
		if (!entry.handle) {
			return false;
		}
		if (entry.cancelled || entry.terminalTaken) {
			return true;
		}
		for (const auto &previous : lanes) {
			if (previous.handle && previous.sender.connection == entry.sender.connection
				&& previous.order < entry.order) {
				return false;
			}
		}
		return !entry.ready
			   && ((entry.failure != Failure::None && (!entry.faultReported || entry.closing)) || entry.count
				   || entry.closing);
	}
	void raw(Batch &batch, const Job &job) {
		if (batch.audio.count == Stream::MaxOutputPackets) {
			return;
		}
		const std::size_t index = batch.audio.count++;
		Stream::Packet &packet  = batch.audio.packets[index];
		std::copy_n(job.payload.begin(), job.bytes, packet.payload.begin());
		packet.bytes = job.bytes;
		packet.frame = job.ticket.sourceFrame;
		packet.end   = job.ticket.end ? (job.ticket.technical ? Stream::End::Technical : Stream::End::InputMarker)
									: Stream::End::None;
		batch.sources[index][0]   = job.ticket;
		batch.sourceCounts[index] = job.ticket.technical ? 0 : 1;
	}
	void mapSources(Lane &entry, Batch &batch) {
		for (std::size_t i = 0; i < batch.audio.count; ++i) {
			const Stream::Packet &packet = batch.audio.packets[i];
			for (std::size_t j = 0; j < packet.spanCount; ++j) {
				if (packet.spans[j].origin != Stream::Origin::Source) {
					continue;
				}
				bool found = false;
				for (const auto &source : entry.history) {
					if (source.channelTicket && source.sourceFrame == packet.spans[j].sourceFrame) {
						batch.sources[i][batch.sourceCounts[i]++] = source;
						found                                     = true;
						break;
					}
				}
				if (!found) {
					batch.audio.count = 0;
					batch.failure     = Failure::Sequence;
					batch.codecFailure = CodecFailure::SourceMap;
					return;
				}
			}
		}
	}
	void run() {
		std::unique_lock< std::mutex > lock(mutex);
		while (!stopping) {
			if (!notified && deliverable()) {
				notified = true;
				lock.unlock();
				if (wake) {
					wake();
				}
				lock.lock();
				continue;
			}
			std::size_t selected = MaxTransmissions;
			for (std::size_t offset = 0; offset < lanes.size(); ++offset) {
				const std::size_t index = (nextLane + offset) % lanes.size();
				if (workAvailable(lanes[index])) {
					selected = index;
					break;
				}
			}
			if (selected == MaxTransmissions) {
				++stats.workerWaits;
				changed.wait(lock);
				continue;
			}
			nextLane    = (selected + 1) % lanes.size();
			Lane &entry = lanes[selected];
			if (entry.cancelled || entry.terminalTaken) {
				const std::size_t slot = entry.slot;
				lock.unlock();
				if (slot != CodecSlots) {
					codecs[slot]->reset();
				}
				lock.lock();
				if (slot != CodecSlots) {
					occupied[slot] = false;
				}
				entry = Lane{};
				continue;
			}
			if (entry.failure != Failure::None) {
				Batch output;
				const bool recoverReserve = !entry.faultReported && entry.slot != CodecSlots;
				const std::size_t slot    = entry.slot;
				lock.unlock();
				if (recoverReserve) {
					codecs[slot]->abort(output.audio);
				}
				lock.lock();
				if (stopping || entry.cancelled) {
					continue;
				}
				output.completed = entry.end.ticket;
				output.failure   = entry.failure;
				output.terminal  = entry.closing;
				mapSources(entry, output);
				entry.output        = std::move(output);
				stats.publishedPackets += entry.output.audio.count;
				entry.ready         = true;
				entry.faultReported = true;
				continue;
			}
			Job job;
			if (entry.count) {
				job        = entry.queue[entry.head];
				entry.head = (entry.head + 1) % InputPackets;
				--entry.count;
				entry.samples -= job.ticket.samples;
			} else {
				job = entry.end;
			}
			const auto startedAt = clock();
			stats.maxWorkerStartAgeUs = std::max(stats.maxWorkerStartAgeUs, ageUs(startedAt, job.ticket.accepted));
			if (startedAt - job.ticket.accepted > std::chrono::milliseconds(40)) {
				++stats.expired;
				if (!entry.closing) {
					entry.end = job;
				}
				fault(entry, Failure::Expired);
				continue;
			}
			const bool bypass         = entry.bypass;
			const bool begin          = !entry.begun;
			entry.begun               = true;
			Connection *owner         = connection(entry.sender);
			const std::uint64_t floor = owner ? owner->floor : 0;
			Batch output;
			output.completed       = job.ticket;
			output.terminal        = job.ticket.end;
			output.bypass          = bypass;
			const std::size_t slot = entry.slot;
			const std::uint64_t lastRawFrame =
				entry.historyNext ? entry.history[(entry.historyNext - 1) % entry.history.size()].sourceFrame
								  : job.ticket.sourceFrame;
			lock.unlock();
			Stream::State state     = Stream::State::Idle;
			std::uint64_t nextFrame = floor;
			bool encoded            = false;
			if (bypass) {
				if (job.ticket.technical) {
					job.ticket.sourceFrame       = lastRawFrame;
					output.completed.sourceFrame = lastRawFrame;
				}
				raw(output, job);
				const std::uint64_t duration = (job.ticket.samples + 479) / 480;
				if (job.ticket.sourceFrame <= std::numeric_limits< std::uint64_t >::max() - duration) {
					nextFrame = std::max(nextFrame, job.ticket.sourceFrame + duration);
				} else {
					output.failure = Failure::Sequence;
				}
			} else {
				Processor &codec = *codecs[slot];
				if (begin && !codec.begin(job.ticket.sourceFrame, floor)) {
					output.failure = Failure::Codec;
					output.codecFailure = CodecFailure::Begin;
				} else {
					const std::uint64_t before = codec.nextOutputFrame();
					if (job.ticket.technical) {
						codec.finish(output.audio);
					} else {
						codec.process({ job.payload.data(), job.bytes, job.ticket.sourceFrame, job.ticket.end },
									  output.audio);
					}
					encoded = codec.nextOutputFrame() > before;
				}
				state     = codec.state();
				nextFrame = codec.nextOutputFrame();
			}
			lock.lock();
			++stats.processedJobs;
			owner = nullptr;
			for (auto &candidate : connections) {
				if (candidate.identity.lifetime == entry.sender.lifetime && candidate.identity.epoch) {
					owner = &candidate;
					break;
				}
			}
			entry.encoded = entry.encoded || encoded;
			if (owner) {
				owner->floor = std::max(owner->floor, nextFrame);
			}
			if (job.bytes && output.audio.disposition == Stream::Disposition::Accepted) {
				entry.history[entry.historyNext++ % entry.history.size()] = job.ticket;
			}
			const auto finishedAt = clock();
			stats.maxWorkerFinishAgeUs = std::max(stats.maxWorkerFinishAgeUs, ageUs(finishedAt, job.ticket.accepted));
			if (stopping || entry.cancelled || entry.failure != Failure::None) {
				++stats.staleResults;
				continue;
			}
			if (finishedAt - job.ticket.accepted > std::chrono::milliseconds(40)) {
				++stats.expired;
				if (!entry.closing) {
					entry.end = job;
				}
				fault(entry, Failure::Expired);
				continue;
			}
			if (!bypass && state == Stream::State::Bypass) {
				output.audio  = Stream::Result{};
				output.bypass = entry.bypass = true;
				++stats.bypassStarts;
				for (std::size_t i = 0; i < entry.originalCount; ++i) {
					raw(output, entry.originals[i]);
				}
				raw(output, job);
				entry.originalCount = 0;
			} else if (!bypass) {
				mapSources(entry, output);
				if (state == Stream::State::Faulted || output.failure != Failure::None
					|| output.audio.disposition == Stream::Disposition::Ignored) {
					++stats.codecErrors;
					if (output.codecFailure == CodecFailure::None)
						output.codecFailure = output.audio.disposition == Stream::Disposition::Ignored
							? CodecFailure::Ignored : CodecFailure::Stream;
					switch (output.codecFailure) {
						case CodecFailure::Begin: ++stats.codecBegin; break;
						case CodecFailure::Stream: ++stats.codecStream; break;
						case CodecFailure::Ignored: ++stats.codecIgnored; break;
						case CodecFailure::SourceMap: ++stats.codecSourceMap; break;
						case CodecFailure::None: break;
					}
					fault(entry, Failure::Codec);
					output.failure      = Failure::Codec;
					entry.faultReported = true;
				} else if (!entry.encoded && output.audio.count == 0 && job.bytes
						   && entry.originalCount < entry.originals.size()) {
					entry.originals[entry.originalCount++] = job;
				} else {
					entry.originalCount = 0;
				}
			}
			if (output.failure != Failure::None && entry.failure == Failure::None) {
				fault(entry, output.failure);
				output.audio.count  = 0;
				entry.faultReported = true;
			}
			if (output.bypass && owner) {
				for (std::size_t i = 0; i < output.audio.count; ++i) {
					const Ticket &source         = output.sources[i][0];
					const std::uint64_t duration = (source.samples + 479) / 480;
					if (source.sourceFrame <= std::numeric_limits< std::uint64_t >::max() - duration) {
						owner->floor = std::max(owner->floor, source.sourceFrame + duration);
					}
				}
			}
			entry.output = std::move(output);
			stats.publishedPackets += entry.output.audio.count;
			entry.ready  = true;
		}
		lock.unlock();
		for (auto &codec : codecs) {
			codec->reset();
		}
	}
};

AnalogVoiceService::AnalogVoiceService(const Parameters &parameters, Wake wake, Clock clock)
	: AnalogVoiceService(processors(parameters), std::move(wake), std::move(clock)) {
}
AnalogVoiceService::AnalogVoiceService(Processors codecs, Wake wake, Clock clock)
	: m_impl(std::make_unique< Impl >(std::move(codecs), std::move(wake), std::move(clock))) {
}
AnalogVoiceService::~AnalogVoiceService() {
	shutdown();
}

AnalogVoiceService::Identity AnalogVoiceService::connect(std::uint64_t connection, std::uint32_t session,
														 std::uint32_t channel, bool send, bool receive) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	if (impl.stopping || !connection) {
		return {};
	}
	for (const auto &entry : impl.connections) {
		if (entry.identity.connection == connection) {
			return {};
		}
	}
	for (auto &entry : impl.connections) {
		if (!entry.identity.epoch) {
			const std::uint64_t epoch = impl.serial();
			if (!epoch) {
				return {};
			}
			entry = { { connection, epoch, session, channel, epoch }, send, receive, 0 };
			return entry.identity;
		}
	}
	return {};
}
AnalogVoiceService::Identity AnalogVoiceService::update(Identity identity, std::uint32_t channel, bool send,
														bool receive) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	Impl::Connection *entry = impl.connection(identity);
	if (impl.stopping || !entry) {
		return {};
	}
	impl.invalidate(identity);
	const std::uint64_t epoch = impl.serial();
	entry->identity.epoch     = epoch;
	entry->identity.channel   = channel;
	entry->send               = send;
	entry->receive            = receive;
	return entry->identity;
}
void AnalogVoiceService::setReceive(Identity identity, bool receive) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	Impl::Connection *owner = impl.connection(identity);
	if (impl.stopping || !owner || owner->receive == receive) {
		return;
	}
	owner->receive = receive;
	for (auto &entry : impl.lanes) {
		std::size_t count = 0;
		for (std::size_t i = 0; i < entry.recipients.count; ++i) {
			if (!(entry.recipients.identities[i] == identity)) {
				entry.recipients.identities[count++] = entry.recipients.identities[i];
			}
		}
		entry.recipients.count = count;
	}
}
void AnalogVoiceService::disconnect(Identity identity) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	if (Impl::Connection *entry = impl.connection(identity)) {
		impl.invalidate(identity);
		*entry = Impl::Connection{};
	}
}
void AnalogVoiceService::invalidateChannel(std::uint32_t channel) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	for (auto &entry : impl.lanes) {
		if (entry.handle && entry.sender.channel == channel) {
			impl.abort(entry);
		}
	}
}
AnalogVoiceService::Handle AnalogVoiceService::open(Identity sender, bool bypass) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	Impl::Connection *owner = impl.connection(sender);
	if (impl.stopping || !owner || !owner->send) {
		++impl.stats.openInvalid;
		return {};
	}
	for (const auto &entry : impl.lanes) {
		if (entry.handle && !entry.cancelled && !entry.closing && entry.sender.channel == sender.channel) {
			++impl.stats.openBusy;
			return {};
		}
	}
	for (std::size_t index = 0; index < impl.lanes.size(); ++index) {
		Impl::Lane &entry = impl.lanes[index];
		if (entry.handle) {
			continue;
		}
		const std::uint64_t generation = impl.serial();
		if (!generation) {
			return {};
		}
		entry.handle                       = { generation, index };
		entry.order                        = generation;
		entry.sender                       = sender;
		entry.end.ticket.sender            = sender;
		entry.end.ticket.generation        = generation;
		entry.end.ticket.routingGeneration = sender.epoch;
		if (!bypass) {
			for (std::size_t slot = 0; slot < CodecSlots; ++slot) {
				if (!impl.occupied[slot]) {
					impl.occupied[slot] = true;
					entry.slot          = slot;
					break;
				}
			}
		}
		entry.bypass = entry.slot == CodecSlots;
		if (entry.bypass) {
			++impl.stats.bypassStarts;
		}
		return entry.handle;
	}
	++impl.stats.noLane;
	return {};
}
AnalogVoiceService::Receipt AnalogVoiceService::submit(Handle handle, const Input &input) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	if (impl.stopping) {
		return impl.reject(Admission::Stopped);
	}
	Impl::Lane *entry = impl.lane(handle);
	if (!entry) {
		return impl.reject(Admission::Stale);
	}
	if (entry->closing) {
		return impl.reject(Admission::Closed);
	}
	if (input.bytes > Stream::MaxInputBytes || (input.bytes && !input.payload) || input.recipients.count > MaxRecipients
		|| (!input.bytes && !input.end) || (input.bytes && (!input.samples || input.samples > InputSamples))
		|| (!input.bytes && input.samples)) {
		return impl.reject(Admission::Invalid);
	}
	if (input.bytes
		&& opus_packet_get_nb_samples(input.payload, static_cast< opus_int32 >(input.bytes), 48000)
			   != static_cast< int >(input.samples)) {
		return impl.reject(Admission::Invalid);
	}
	Impl::Job job;
	job.ticket = impl.ticket(*entry, input.frame, input.samples, input.end, false);
	job.ticket.metadata = input.metadata;
	if (!job.ticket.channelTicket) {
		impl.fault(*entry, Failure::Sequence);
		return impl.reject(Admission::Faulted);
	}
	if (input.end) {
		entry->closing = true;
		entry->end     = job;
	}
	if (entry->failure != Failure::None) {
		impl.changed.notify_one();
		return impl.reject(Admission::Faulted, job.ticket);
	}
	if (entry->count == InputPackets || entry->samples + input.samples > InputSamples) {
		++impl.stats.inputFull;
		impl.stats.packetLimitHits += entry->count == InputPackets;
		impl.stats.sampleLimitHits += entry->samples + input.samples > InputSamples;
		const auto queuedPackets = static_cast<std::uint32_t>(entry->count);
		const auto queuedSamples = entry->samples;
		if (!entry->closing) {
			entry->end = job;
		}
		impl.fault(*entry, Failure::InputFull);
		return { Admission::InputFull, job.ticket, queuedPackets, queuedSamples };
	}
	job.bytes      = input.bytes;
	job.recipients = input.recipients;
	if (input.bytes) {
		std::copy_n(input.payload, input.bytes, job.payload.begin());
	}
	if (!entry->hasRecipients) {
		entry->recipients    = input.recipients;
		entry->hasRecipients = true;
	} else {
		intersect(entry->recipients, input.recipients);
	}
	entry->queue[(entry->head + entry->count) % InputPackets] = job;
	++entry->count;
	entry->samples += input.samples;
	impl.stats.peakInputPackets = std::max<std::uint64_t>(impl.stats.peakInputPackets, entry->count);
	impl.stats.peakInputSamples = std::max<std::uint64_t>(impl.stats.peakInputSamples, entry->samples);
	++impl.stats.accepted;
	impl.changed.notify_one();
	return { Admission::Accepted, job.ticket, static_cast<std::uint32_t>(entry->count), entry->samples };
}
AnalogVoiceService::Receipt AnalogVoiceService::finish(Handle handle, std::uint64_t lastSourceFrame, Time endedAt) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	if (impl.stopping) {
		return impl.reject(Admission::Stopped);
	}
	Impl::Lane *entry = impl.lane(handle);
	if (!entry) {
		return impl.reject(Admission::Stale);
	}
	if (entry->closing) {
		return impl.reject(Admission::Closed, entry->end.ticket);
	}
	entry->end.ticket = impl.ticket(*entry, lastSourceFrame, 0, true, true);
	if (endedAt != Time{})
		entry->end.ticket.accepted = endedAt;
	entry->closing    = true;
	if (!entry->end.ticket.channelTicket) {
		impl.fault(*entry, Failure::Sequence);
		return impl.reject(Admission::Faulted, entry->end.ticket);
	}
	impl.changed.notify_one();
	return { Admission::Accepted, entry->end.ticket };
}
void AnalogVoiceService::cancel(Handle handle) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	if (Impl::Lane *entry = impl.lane(handle)) {
		impl.abort(*entry);
	}
}
void AnalogVoiceService::acknowledgeNotification() {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	impl.notified = false;
	impl.changed.notify_one();
}
bool AnalogVoiceService::take(Batch &batch) {
	Impl &impl = *m_impl;
	std::lock_guard< std::mutex > lock(impl.mutex);
	if (impl.stopping) {
		return false;
	}
	for (std::size_t offset = 0; offset < impl.lanes.size(); ++offset) {
		const std::size_t index = (impl.nextDelivery + offset) % impl.lanes.size();
		Impl::Lane &entry       = impl.lanes[index];
		if (!entry.handle || !entry.ready || entry.cancelled || !impl.first(entry)) {
			continue;
		}
		Impl::Connection *sender = impl.connection(entry.sender);
		if (!sender || !sender->send) {
			impl.abort(entry);
			continue;
		}
		const auto takenAt = impl.clock();
		if (entry.output.failure == Failure::None)
			impl.stats.maxTakeAgeUs = std::max(impl.stats.maxTakeAgeUs, ageUs(takenAt, entry.output.completed.accepted));
		if (entry.output.failure == Failure::None
			&& takenAt - entry.output.completed.accepted > std::chrono::milliseconds(40)) {
			++impl.stats.expired;
			if (!entry.closing) {
				entry.end.ticket = entry.output.completed;
			}
			impl.fault(entry, Failure::Expired);
			continue;
		}
		batch            = entry.output;
		impl.stats.takenPackets += batch.audio.count;
		batch.handle     = entry.handle;
		batch.recipients = {};
		for (std::size_t i = 0; i < entry.recipients.count; ++i) {
			Impl::Connection *recipient = impl.connection(entry.recipients.identities[i]);
			if (recipient && recipient->receive && recipient->identity.channel == entry.sender.channel) {
				batch.recipients.identities[batch.recipients.count++] = recipient->identity;
			}
		}
		impl.nextDelivery   = (index + 1) % impl.lanes.size();
		entry.ready         = false;
		entry.terminalTaken = batch.terminal;
		impl.changed.notify_one();
		return true;
	}
	return false;
}
AnalogVoiceService::Statistics AnalogVoiceService::statistics() const {
	std::lock_guard< std::mutex > lock(m_impl->mutex);
	return m_impl->stats;
}
std::size_t AnalogVoiceService::fixedStorageBytes() noexcept {
	return sizeof(Impl);
}
void AnalogVoiceService::shutdown() {
	Impl &impl = *m_impl;
	{
		std::lock_guard< std::mutex > lock(impl.mutex);
		impl.stopping = true;
		impl.changed.notify_one();
	}
	if (impl.worker.joinable()) {
		impl.worker.join();
	}
}
} // namespace funkvoice
