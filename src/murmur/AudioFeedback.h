// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#ifndef MUMBLE_MURMUR_AUDIOFEEDBACK_H_
#define MUMBLE_MURMUR_AUDIOFEEDBACK_H_
#include "SquelchPlan.h"
#include "RadioNoise.h"
#include <array>
#include <set>
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

// Producers only submit identity snapshots. Clips encode at startup, noise on workers.
// tick/cancel and delivery run on the server's Qt main thread, never the UDP thread.
class AudioFeedback {
public:
	enum class Signal { Busy, SquelchOpen, SquelchClose, Noise };
	// Lifecycle/recipient snapshots are separate from clips. Future continuous
	// effects can replace this bounded clip renderer, without changing Simplex.
	struct Recipient {
		std::uint64_t connection;
		unsigned int session;
		unsigned int channel;
	};
	using Delivery = std::function< bool(const Recipient &, const QByteArray &, std::uint64_t, bool) >;
	static constexpr std::size_t MaxPending    = 128;
	static constexpr std::size_t MaxRecipients = 64;
	explicit AudioFeedback(int volume, const QString &busyFile = {}, int boostDb = 0);
	QString warning() const { return m_warning; }
	bool customBusy() const { return m_customBusy; }
	bool request(Signal signal, Recipient recipient, std::int64_t nowUs);
	using CueGate = std::function<bool(const SquelchPlan::Cue &, bool starting)>;
	// Separate lifecycle validity from transient output pressure. Continuous Noise
	// retains its 1024-byte budget; finite cues retain their existing 64-KiB budget.
	using Capacity = std::function<bool(const Recipient &, bool continuous)>;
	void configureSquelch(const QString &openFile, const QString &closeFile, int openVolume, int closeVolume,
		bool openEnabled = true, bool closeEnabled = true);
	bool requestSquelch(const SquelchPlan::Cue &cue);
	bool configureNoise(const QString &file, int volume); // Startup only.
	bool requestNoise(SquelchPlan::Cue cue); // Bounded, try-lock producer queue.
	void tick(std::int64_t nowUs, const Delivery &deliver, const CueGate &gate = {}, const Capacity &capacity = {});
	static constexpr int RetryIntervalMs = 5;
	// Main-thread timer scheduling only. Zero preserves the legacy non-Noise timer.
	int nextTickDelayMs(std::int64_t nowUs) const;
	void cancel(std::uint64_t connection);
	const std::vector< QByteArray > &clip(Signal signal) const;

private:
	friend struct AudioFeedbackTestAccess; // Deterministic producer-starvation tests only.
	struct Request {
		Signal signal;
		Recipient recipient;
		std::int64_t time;
		SquelchPlan::Cue cue{};
	};
	struct Playback {
		Request request;
		std::int64_t started;
		std::int64_t lastActivity;
		std::uint64_t nextFrame = 0;
		std::size_t index       = 0;
		bool active             = true;
		std::int64_t lastBusy = -1000000;
		std::int64_t availableAt = 0;
		bool finishEarly = false;
	};
	std::vector< QByteArray > m_busy, m_open, m_close;
	RadioNoise::PCM m_openPCM, m_closePCM;
	QString m_warning;
	int m_boostDb = 0; // Startup-only effect gain; shared by all pre-encoded clips.
	bool m_customBusy = false;
	bool m_squelchConfigured = false;
	std::mutex m_mutex;
	std::deque< Request > m_pending, m_cues;
	std::map< std::uint64_t, Playback > m_playback;
    struct NoiseState {
        SquelchPlan::Cue cue; std::size_t nextSample; int slot = -1; std::size_t start = 0;
        bool opened = false, finishing = false, hasClose = false, closeStarted = false, closeReserved = false;
        SquelchPlan::Cue close{};
        RadioNoise::Packet reserve{};
        bool hasReserve = false;
        std::int64_t due = 0, lastPacket = 0;
    };
    struct NoiseSlot { std::unique_ptr<RadioNoise> producer; std::uint64_t connection = 0, transmission = 0; };
    std::int64_t m_lastNoiseTick = 0;
    RadioNoise::PCM m_noisePCM;
    std::mt19937 m_random;
    std::array<NoiseSlot, RadioNoise::MaxInstances> m_noiseSlots;
    std::map<std::uint64_t, NoiseState> m_noise;
    std::deque<SquelchPlan::Cue> m_noiseRequests;
    struct Terminal { Recipient recipient; QByteArray opus; std::uint64_t frame; };
    std::deque<Terminal> m_terminals; // At most one reserved end per cancelled recipient (bounded).
    void tickRadio(std::int64_t now, const Delivery &deliver, const CueGate &gate, const Capacity &capacity,
        const std::deque<Request> &pending, const std::deque<Request> &cues,
        const std::deque<SquelchPlan::Cue> &requests);
};
#endif
