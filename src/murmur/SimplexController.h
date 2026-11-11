// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#ifndef MUMBLE_MURMUR_SIMPLEXCONTROLLER_H_
#define MUMBLE_MURMUR_SIMPLEXCONTROLLER_H_

#include <QtCore/QString>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>

class SimplexController {
public:
	struct Context {
		std::uint64_t connection = 0;
		unsigned int session     = 0;
		unsigned int channel     = 0;
		QString user;
	};
	struct Packet {
		Context context;
		std::uint64_t frame = 0;
		unsigned int target = 0;
		bool payload        = true;
		bool last           = false;
		bool eligible       = true;
		bool udp            = false;
	};
	struct Event {
		Context context;
		QString event;
		QString reason;
		std::int64_t timeUs   = 0;
		std::uint64_t frame   = 0;
		std::uint64_t packets = 0;
		bool udp              = false;
		// Optional Analog Voice pipeline metadata, serialized only for analog_* events.
		std::uint64_t analogGeneration = 0, analogTicket = 0, analogEpoch = 0;
		std::uint64_t analogOutputFrame = 0;
		std::uint32_t analogSamples = 0;
		std::size_t analogOutputs = 0, analogRecipients = 0, analogHandoffs = 0;
		std::uint32_t analogQueuedPackets = 0, analogQueuedSamples = 0;
		bool analogQueueKnown = false;
		QString analogCodecStage, analogStreamError;
		int analogCodecError = 0;
	};
	using Clock = std::function< std::int64_t() >;
	using Sink  = std::function< void(const Event &) >;
	// The caller retains this guard until forwarding finishes. Thus a terminator
	// cannot grant the next sender while the previous packet is still being sent.
	struct Transmission {
		std::unique_lock< std::mutex > lock;
		bool allowed = false;
		// Accepted start, independent of diagnostic sinks and source frame resets.
		bool started = false;
		// Direct forwarding decision, independent of diagnostic event filtering.
		bool feedback = false;
	};
	SimplexController(int timeoutMs, int retryMs, Sink sink = {}, Clock clock = {});
	Transmission begin(const Packet &packet);
	void expire();
	void invalidate(std::uint64_t connection, const QString &reason);
	void disconnect(std::uint64_t connection);
	void shutdown();
	int timeoutMs() const;
	int retryMs() const;
	static std::int64_t monotonicUs();

private:
	enum class Phase { Idle, Owning, Blocked };
	struct User {
		Context context;
		Phase phase                = Phase::Idle;
		std::int64_t lastSeen      = 0;
		std::int64_t lastAccepted  = 0;
		std::uint64_t highestFrame = 0;
		std::uint64_t accepted     = 0;
		std::uint64_t denied       = 0;
		bool seen                  = false;
		bool feedbackReported      = false;
		bool udp                   = false;
	};
	std::mutex m_mutex;
	std::map< std::uint64_t, User > m_users;
	std::map< unsigned int, std::uint64_t > m_owners;
	std::int64_t m_timeoutUs;
	std::int64_t m_retryUs;
	Sink m_sink;
	Clock m_clock;
	void event(const User &user, const char *name, const QString &reason, std::int64_t now, std::uint64_t packets);
	void release(User &user, const QString &reason, std::int64_t now);
	void block(User &user, const QString &reason, std::int64_t now);
	void finishBlocked(User &user, const QString &reason, std::int64_t now);
	void rejectFeedback(User &user, const Packet &packet, Transmission &result);
	void expireChannel(unsigned int channel, std::int64_t now);
};
#endif
