// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#ifndef MUMBLE_MURMUR_SQUELCHPLAN_H_
#define MUMBLE_MURMUR_SQUELCHPLAN_H_

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

// Transmission lifecycle and recipient model; independent of signal synthesis/delivery.
// It plans optional cues, never grants speech, stores audio or sends packets.
// The caller must supply the actual Simplex decision and eligible receiver set.
// A future dispatcher must hold the server voice lock while revalidating identity,
// membership and permissions AND dispatching; consume alone is not an audio API.
class SquelchPlan {
public:
	struct Identity {
		std::uint64_t connection;
		unsigned int session;
		unsigned int channel;
		bool operator==(const Identity &other) const {
			return connection == other.connection && session == other.session && channel == other.channel;
		}
	};
	enum class Kind { Open, Close, Noise, NoiseEnd };
	struct Cue {
		Kind kind;
		Identity sender;
		Identity recipient;
		std::uint64_t transmission;
		std::int64_t createdUs;
	};
	static constexpr std::size_t MaxChannels = 128;
	static constexpr std::size_t MaxRecipients = 64;
	using Clock = std::function<std::int64_t()>;
	static constexpr std::int64_t MaxCueAgeUs = 100000;
	// A short transmission can finish before Open playback + retirement gap.
	static constexpr std::int64_t MaxCloseCueAgeUs = 300000;
	explicit SquelchPlan(bool enabled = false, Clock clock = {});
	// validOpus refers to a validated nonempty payload. Empty markers cannot start
	// a stream. Caller-accepted payloads stay untouched even when planning fails.
	std::vector<Cue> observe(Identity sender, bool allowed, std::uint64_t frame,
		bool payload, bool last, bool validOpus, const std::vector<Identity> &eligibleRecipients);
	// Called only for an actual Simplex timeout release, never to grant speech or
	// manufacture an end marker. Capture the existing generation before a new
	// owner can start. Repeated/confirmed ends and unobserved payloads yield no cue.
	std::vector<Cue> timeout(Identity sender, std::uint64_t lastAcceptedFrame);
	// One-shot reservation immediately before a FUTURE dispatcher sends a cue.
	// If Open was omitted, Close cannot be consumed. False = omit optional effect.
	bool consume(const Cue &cue, Identity currentSender, Identity currentRecipient, bool stillEligible);
	// Active playback validation; no cue-age limit once dispatch has started.
	bool current(const Cue &cue) const;
	// Call on channel leave (including out-and-back), permission loss, disconnect,
	// or timeout with the experimental fallback off. These cancel optional cues.
	void invalidate(std::uint64_t connection);
	void clear();
	std::size_t size() const;

private:
	struct Recipient {
		Identity identity;
		bool opened = false;
		bool closed = false;
	};
	struct Stream {
		Identity sender;
		std::uint64_t generation;
		std::uint64_t highestFrame;
		bool ended = false;
		std::vector<Recipient> recipients;
	};
	bool m_enabled;
	Clock m_clock;
	mutable std::mutex m_mutex;
	std::map<unsigned int, Stream> m_channels;
	std::uint64_t m_nextGeneration = 1;
};
#endif
