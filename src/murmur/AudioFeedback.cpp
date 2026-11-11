// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#include "AudioFeedback.h"
#include "AudioSignal.h"
#include <algorithm>
#include <exception>
#include <set>
#include <stdexcept>

AudioFeedback::AudioFeedback(int volume, const QString &busyFile, int boostDb) : m_boostDb(boostDb) {
	std::vector< float > pcm;
	if (!busyFile.isEmpty()) {
		try {
			pcm          = AudioSignal::loadWav(busyFile);
			m_customBusy = true;
		} catch (const std::exception &error) {
			m_warning = QString::fromUtf8(error.what());
		}
	}
	if (pcm.empty())
		pcm = AudioSignal::syntheticBusy();
	m_busy = AudioSignal::encode(pcm, volume, m_boostDb);
}

const std::vector< QByteArray > &AudioFeedback::clip(Signal signal) const {
	if (signal == Signal::SquelchOpen) return m_open;
	if (signal == Signal::SquelchClose) return m_close;
	return m_busy;
}

bool AudioFeedback::request(Signal signal, Recipient recipient, std::int64_t now) {
	std::unique_lock< std::mutex > lock(m_mutex, std::try_to_lock);
	if (signal != Signal::Busy || !lock || m_pending.size() >= MaxPending || !recipient.connection)
		return false; // Deliberately omit feedback under overload; never delay speech.
	m_pending.push_back({ signal, recipient, now });
	return true;
}

void AudioFeedback::configureSquelch(const QString &openFile, const QString &closeFile, int openVolume, int closeVolume, bool openEnabled, bool closeEnabled) {
	// All file access and Opus encoding happen before the voice thread starts.
	auto open = openEnabled ? AudioSignal::encodeWavSignal(openFile, openVolume, m_boostDb) : std::vector<QByteArray>{};
	auto close = closeEnabled ? AudioSignal::encodeWavSignal(closeFile, closeVolume, m_boostDb) : std::vector<QByteArray>{};
	auto openPCM = openEnabled ? std::make_shared<const std::vector<float>>(AudioSignal::streamWavSignal(openFile, openVolume, m_boostDb)) : RadioNoise::PCM{};
	auto closePCM = closeEnabled ? std::make_shared<const std::vector<float>>(AudioSignal::streamWavSignal(closeFile, closeVolume, m_boostDb)) : RadioNoise::PCM{};
	m_openPCM = std::move(openPCM); m_closePCM = std::move(closePCM);
	m_open = std::move(open); m_close = std::move(close); m_squelchConfigured = true;
}
bool AudioFeedback::requestSquelch(const SquelchPlan::Cue &cue) {
	std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
	const auto signal = cue.kind == SquelchPlan::Kind::Open ? Signal::SquelchOpen : Signal::SquelchClose;
	if (!lock || m_cues.size() >= MaxPending || clip(signal).empty()) return false;
	m_cues.push_back({cue.kind == SquelchPlan::Kind::Open ? Signal::SquelchOpen : Signal::SquelchClose,
		{cue.recipient.connection, cue.recipient.session, cue.recipient.channel}, cue.createdUs, cue});
	return true;
}

bool AudioFeedback::configureNoise(const QString &file, int volume) {
    if (volume < 0 || volume > 100) throw std::invalid_argument("Noise volume must be 0..100");
    if (file.isEmpty() || volume == 0) return false;
    auto pcm = RadioNoise::load(file);
    std::array<NoiseSlot, RadioNoise::MaxInstances> slots;
    for (auto &slot : slots) slot.producer.reset(new RadioNoise(pcm, volume, 0, true, m_openPCM, m_closePCM));
    m_random.seed(std::random_device{}());
    m_noisePCM = std::move(pcm); m_noiseSlots = std::move(slots);
    return true;
}
bool AudioFeedback::requestNoise(SquelchPlan::Cue cue) {
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!m_noisePCM || !lock || m_noiseRequests.size() >= MaxPending) return false;
    cue.kind = SquelchPlan::Kind::Noise; m_noiseRequests.push_back(cue); return true;
}
// One private sequential Open/Noise/Close stream per recipient. The fixed pool
// encodes all three phases with one encoder; speech never enters this renderer.
void AudioFeedback::tickRadio(std::int64_t now, const Delivery &deliver, const CueGate &gate,
    const Capacity &capacity, const std::deque<Request> &pending, const std::deque<Request> &cues,
    const std::deque<SquelchPlan::Cue> &requests) {
    const auto valid = [&](SquelchPlan::Cue cue) {
        cue.kind = SquelchPlan::Kind::NoiseEnd; // Same generation, including a legitimate ended transmission.
        return gate && gate(cue, false);
    };
    const auto room = [&](const Recipient &r, bool continuous) { return !capacity || capacity(r, continuous); };
    for (const auto &end : m_terminals) {
        if (room(end.recipient, true)) deliver(end.recipient, end.opus, end.frame, true);
    }
    m_terminals.clear(); // Best effort on cancellation, never retain work for an invalid session.
    for (auto it = m_noise.begin(); it != m_noise.end();) {
        if (valid(it->second.cue)) { ++it; continue; }
        auto play = m_playback.find(it->first);
        if (play != m_playback.end() && play->second.request.signal == Signal::Noise) {
            auto &p = play->second; const auto &s = it->second;
            if (p.active && p.index && s.hasReserve && s.reserve.segment != RadioNoise::Segment::Close
                && room(p.request.recipient, true)) {
                deliver(p.request.recipient, s.reserve.opus, p.nextFrame, true); p.nextFrame += 2;
            }
            p.active = false; p.availableAt = now + 60000; p.lastActivity = now;
        }
        it = m_noise.erase(it);
    }
    // Retire unused workers with try-lock only; construction/joining stay at startup/shutdown.
    for (std::size_t i = 0; i < m_noiseSlots.size(); ++i) {
        auto &slot = m_noiseSlots[i];
        if (!slot.connection) continue;
        auto state = m_noise.find(slot.connection);
        if (state != m_noise.end() && state->second.slot == int(i)
            && state->second.cue.transmission == slot.transmission) continue;
        if (slot.producer->tryPause()) slot.connection = 0;
    }
    for (auto it = m_playback.begin(); it != m_playback.end();) {
        if (!it->second.active && !m_noise.count(it->first) && now - it->second.lastActivity >= 60000000)
            it = m_playback.erase(it);
        else ++it;
    }
    for (auto cue : requests) {
        if (!valid(cue) || now < cue.createdUs || now - cue.createdUs > SquelchPlan::MaxCueAgeUs) continue;
        auto old = m_noise.find(cue.recipient.connection);
        if (old != m_noise.end()) {
            if (old->second.cue.transmission == cue.transmission) continue;
            // A new generation never inherits old PCM, Close or encoder state.
            // The old generation is retired below before admitting this one.
            continue;
        }
        if (m_noise.size() >= MaxRecipients) continue;
        std::size_t start = 0; bool found = false;
        for (const auto &entry : m_noise) if (entry.second.cue.transmission == cue.transmission) {
            start = entry.second.start; found = true; break;
        }
        if (!found) start = RadioNoise::chooseStart(m_noisePCM->size(), m_random);
        m_noise.emplace(cue.recipient.connection, NoiseState{cue, start, -1, start});
    }
    // Capture Close before testing whether Noise is still active. Previously an
    // ended Noise cue was invalidated here before Close could take over.
    for (const auto &request : cues) {
        if (request.signal != Signal::SquelchClose) continue;
        auto state = m_noise.find(request.recipient.connection);
        if (state == m_noise.end() || state->second.cue.transmission != request.cue.transmission
            || state->second.hasClose || now < request.time || now - request.time > SquelchPlan::MaxCloseCueAgeUs
            || !valid(request.cue)) continue;
        state->second.close = request.cue;
        state->second.hasClose = true;
    }
    std::deque<Request> deferredBusy;
    std::set<std::uint64_t> busyWaiting;
    for (const auto &request : pending) {
        if (now < request.time || now - request.time > 300000) continue;
        auto play = m_playback.find(request.recipient.connection);
        if (play != m_playback.end() && ((play->second.active && play->second.request.signal == Signal::Busy)
            || now - play->second.lastBusy < 1000000)) continue;
        auto state = m_noise.find(request.recipient.connection);
        if (state != m_noise.end() && state->second.slot >= 0) {
            // Finish on the next RESERVED frame, independently of producer availability.
            if (state->second.hasReserve && play != m_playback.end()) {
                auto &p = play->second;
                if (!room(p.request.recipient, true)) {
                    deferredBusy.push_back(request); busyWaiting.insert(request.recipient.connection); continue;
                }
                if (valid(state->second.cue)) {
                    deliver(p.request.recipient, state->second.reserve.opus, p.nextFrame, true);
                    state->second.nextSample = state->second.reserve.nextSample;
                }
                p.nextFrame += 2; p.active = false; p.availableAt = now + 60000; p.lastActivity = now;
            }
            if (play != m_playback.end()) play->second.active = false;
            state->second.hasReserve = false;
            // Retain the random cursor for resumption, but never resume the old encoder segment.
            auto &slot = m_noiseSlots[std::size_t(state->second.slot)];
            if (!slot.producer->tryPause()) {
                deferredBusy.push_back(request); busyWaiting.insert(request.recipient.connection); continue;
            }
            slot.connection = 0; state->second.slot = -1;
            state->second.finishing = false;
        }
        if (play != m_playback.end() && (play->second.active || now < play->second.availableAt)) {
            deferredBusy.push_back(request); busyWaiting.insert(request.recipient.connection); continue;
        }
        if (play == m_playback.end() && m_playback.size() >= MaxRecipients) continue;
        const auto sequence = play == m_playback.end() ? 0 : play->second.nextFrame + 100;
        m_playback[request.recipient.connection] = {request, now, now, sequence, 0, true, now, 0};
    }
    if (!deferredBusy.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = deferredBusy.rbegin(); it != deferredBusy.rend() && m_pending.size() < MaxPending; ++it)
            m_pending.push_front(*it);
    }
    // Busy remains the original pre-encoded clip, cooldown and independent stream.
    for (auto &entry : m_playback) {
        auto &p = entry.second;
        if (!p.active || p.request.signal != Signal::Busy) continue;
        if (now - p.started > std::int64_t(p.index) * 20000 + 100000) { p.active = false; continue; }
        if (now - p.started < std::int64_t(p.index) * 20000) continue;
        const bool last = p.index + 1 == m_busy.size();
        if (!deliver(p.request.recipient, m_busy.at(p.index), p.nextFrame, last) || last) {
            p.active = false; p.availableAt = now + 60000;
        }
        ++p.index; p.nextFrame += 2; p.lastActivity = now;
    }
    for (auto it = m_noise.begin(); it != m_noise.end();) {
        auto &s = it->second;
        auto play = m_playback.find(it->first);
        // Identity, generation and permission failures cancel rather than sending
        // stale effects to a moved/disconnected/reused session.
        if (!valid(s.cue)) { if (play != m_playback.end() && play->second.request.signal == Signal::Noise) play->second.active = false;
            it = m_noise.erase(it); continue; }
        if (busyWaiting.count(it->first) || (play != m_playback.end() && play->second.active && play->second.request.signal == Signal::Busy)) {
            ++it; continue;
        }
        if (s.slot < 0) {
            const bool active = gate(s.cue, false);
            // Never restart optional Noise after the transmission has ended or
            // play delayed Open after Busy. Existing Close is omitted in this case.
            if (s.opened && !active) { it = m_noise.erase(it); continue; }
            if (!s.opened && now - s.cue.createdUs > SquelchPlan::MaxCueAgeUs) { it = m_noise.erase(it); continue; }
            if (play != m_playback.end() && now < play->second.availableAt) { ++it; continue; }
            if (play == m_playback.end() && m_playback.size() >= MaxRecipients) { it = m_noise.erase(it); continue; }
            bool started = false;
            for (std::size_t i = 0; i < m_noiseSlots.size(); ++i) {
                auto &slot = m_noiseSlots[i];
                if (slot.connection || !slot.producer->tryRestart(s.nextSample, !s.opened)) continue;
                auto open = s.cue; open.kind = SquelchPlan::Kind::Open;
                if (!s.opened && m_openPCM && !gate(open, true)) { slot.producer->tryPause(); break; }
                const auto sequence = play == m_playback.end() ? 0 : play->second.nextFrame + 100;
                const auto lastBusy = play == m_playback.end() ? -1000000 : play->second.lastBusy;
                const Request r{Signal::Noise, {s.cue.recipient.connection, s.cue.recipient.session, s.cue.recipient.channel}, now, s.cue};
                m_playback[it->first] = {r, now, now, sequence, 0, true, lastBusy, 0};
                play = m_playback.find(it->first); s.due = s.lastPacket = now;
                s.opened = true; s.slot = int(i); slot.connection = it->first; slot.transmission = s.cue.transmission;
                started = true; break;
            }
            if (!started) { ++it; continue; }
        }
        auto &p = play->second;
        auto &producer = *m_noiseSlots[std::size_t(s.slot)].producer;
        if (!s.finishing && s.hasClose) {
            if (now - s.close.createdUs > SquelchPlan::MaxCloseCueAgeUs || (!s.closeReserved && !gate(s.close, true))) {
                s.hasClose = false;
            } else {
                s.closeReserved = true;
                if (producer.tryFinish(true)) s.finishing = true;
                // Retry a contended command without blocking retirement of the reserve.
            }
        }
        if (!s.finishing && !gate(s.cue, false)) {
            if (!s.hasClose && producer.tryFinish(false)) s.finishing = true;
        }
        if (now < s.due) { ++it; continue; }
        RadioNoise::Packet next;
        if (!s.hasReserve) {
            if (!producer.take(s.reserve)) {
                if (now - s.lastPacket > SquelchPlan::MaxCloseCueAgeUs) { p.active = false; it = m_noise.erase(it); }
                else ++it;
                continue;
            }
            s.hasReserve = true;
        }
        const bool continuous = s.reserve.segment == RadioNoise::Segment::Noise || s.reserve.segment == RadioNoise::Segment::End;
        if (!room(p.request.recipient, continuous)) {
            // Hold at most one reserve + the existing 3-packet producer queue.
            // Check BEFORE taking another packet: pressure cannot skip encoded audio.
            if (now - s.lastPacket > SquelchPlan::MaxCloseCueAgeUs) { p.active = false; it = m_noise.erase(it); }
            else ++it;
            continue;
        }
        bool terminal = s.reserve.last;
        // One unsent packet is always kept as a terminator reserve. Producer
        // starvation cannot force an empty end marker or a duplicated Opus frame.
        const bool haveNext = !terminal && producer.take(next);
        const bool expiredClose = s.hasClose && !s.closeStarted
            && s.reserve.segment != RadioNoise::Segment::Close
            && now + 20000 - s.close.createdUs > SquelchPlan::MaxCloseCueAgeUs;
        const bool stalled = now - s.lastPacket > 100000 && (!s.hasClose || s.closeStarted);
        if (expiredClose || stalled) terminal = true;
        if (!terminal && !haveNext) { ++it; continue; }
        if (s.reserve.segment == RadioNoise::Segment::Close && !s.closeStarted) {
            if (now - s.close.createdUs > SquelchPlan::MaxCloseCueAgeUs) { p.active = false; it = m_noise.erase(it); continue; }
            s.closeStarted = true;
        }
        const bool sent = deliver(p.request.recipient, s.reserve.opus, p.nextFrame, terminal);
        s.nextSample = s.reserve.nextSample;
        // Advance the media clock, not callback time: 21/19-ms timer jitter
        // must not turn every second callback into a skipped 20-ms frame.
        p.nextFrame += 2; ++p.index; p.lastActivity = now; s.lastPacket = now; s.due += 20000;
        if (!sent || terminal) {
            p.active = false; p.availableAt = now + 60000;
            it = m_noise.erase(it); continue;
        }
        s.reserve = std::move(next); s.hasReserve = true;
        ++it;
    }
}

int AudioFeedback::nextTickDelayMs(std::int64_t now) const {
    if (!m_noisePCM) return 0;
    std::int64_t delay = 20000;
    const auto consider = [&](std::int64_t due) {
        // One frame per recipient/callback. Recover recent delay with separate
        // >=5-ms retry callbacks, never an unbounded in-callback catch-up loop.
        delay = std::min(delay, due > now ? due - now : std::int64_t(RetryIntervalMs) * 1000);
    };
    for (const auto &entry : m_noise) {
        const auto play = m_playback.find(entry.first);
        if (play != m_playback.end()) {
            if (play->second.active && play->second.request.signal == Signal::Busy) continue;
            if (entry.second.slot < 0 && play->second.availableAt > now) { consider(play->second.availableAt); continue; }
        }
        consider(entry.second.due);
    }
    for (const auto &entry : m_playback) {
        const auto &p = entry.second;
        if (p.active && p.request.signal == Signal::Busy)
            consider(p.started + std::int64_t(p.index) * 20000);
    }
    return static_cast<int>((delay + 999) / 1000); // Round up: never intentionally wake before due.
}

void AudioFeedback::tick(std::int64_t now, const Delivery &deliver, const CueGate &gate, const Capacity &capacity) {
	std::deque< Request > pending, cues;
	std::deque<SquelchPlan::Cue> noiseRequests;
	{
		std::lock_guard< std::mutex > lock(m_mutex);
		pending.swap(m_pending);
		cues.swap(m_cues);
		noiseRequests.swap(m_noiseRequests);
	}
	if (m_noisePCM) {
        m_lastNoiseTick = now;
        tickRadio(now, deliver, gate, capacity, pending, cues, noiseRequests);
        return;
    }
	// Keep cooldown/sequence state bounded, including disconnected/idle clients.
	for (auto it = m_playback.begin(); it != m_playback.end();) {
		// Noise can run longer than the idle retention period. Retain its
		// sequence and retirement gap after its final packet, just as for a
		// recent short clip; never expire it merely because it started long ago.
		if (it->second.active && it->second.request.signal == Signal::Noise)
			it->second.lastActivity = now;
		if (!it->second.active && now - it->second.lastActivity >= 60000000)
			it = m_playback.erase(it);
		else
			++it;
	}
	std::deque<Request> deferredBusy;
	std::set<std::uint64_t> busyWaiting;
	for (const auto &request : pending) {
		if (now - request.time > 300000 || now < request.time)
			continue;
		auto it = m_playback.find(request.recipient.connection);
		if (it != m_playback.end()) {
			if ((it->second.active && it->second.request.signal == Signal::Busy) || now - it->second.lastBusy < 1000000)
				continue;
			if (it->second.request.signal != Signal::Busy && (it->second.active || now < it->second.availableAt)) {
				// End an optional effect at its next packet boundary, then let the
				// client retire that stream. Never splice Busy into an unterminated clip.
				it->second.finishEarly = true;
				deferredBusy.push_back(request); busyWaiting.insert(request.recipient.connection);
				continue;
			}
			const auto sequence = it->second.nextFrame + 100; // Gap between independent jitter-buffer streams.
			it->second          = { request, now, now, sequence, 0, true, now, 0 };
		} else if (m_playback.size() < MaxRecipients) {
			m_playback.emplace(request.recipient.connection, Playback{ request, now, now, 0, 0, true, now, 0 });
		}
	}
	std::deque<Request> deferred;
	std::set<std::uint64_t> waiting = busyWaiting;
	for (const auto &request : cues) {
		waiting.insert(request.recipient.connection);
		if (busyWaiting.count(request.recipient.connection) || !gate || now < request.time || now - request.time > (request.signal == Signal::SquelchClose ? SquelchPlan::MaxCloseCueAgeUs : SquelchPlan::MaxCueAgeUs)) continue;
		auto it = m_playback.find(request.recipient.connection);
		if (it != m_playback.end() && it->second.active && it->second.request.signal == Signal::Busy)
			continue; // Busy wins; optional Squelch does not queue behind it.
		if (it != m_playback.end() && (it->second.active || now < it->second.availableAt)) {
			if (it->second.active && it->second.request.signal == Signal::Noise) it->second.finishEarly = true;
			deferred.push_back(request); continue;
		}
		if (it == m_playback.end() && m_playback.size() >= MaxRecipients) continue;
		if (!gate(request.cue, true)) continue;
		if (it == m_playback.end()) {
			m_playback.emplace(request.recipient.connection, Playback{request, now, now, 0, 0, true});
		} else {
			const auto lastBusy = it->second.lastBusy;
			const auto sequence = it->second.nextFrame + 100;
			it->second = {request, now, now, sequence, 0, true, lastBusy, 0};
		}
	}
	if (!deferredBusy.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		while (!deferredBusy.empty() && m_pending.size() < MaxPending) {
			m_pending.push_front(deferredBusy.back()); deferredBusy.pop_back();
		}
	}
	if (!deferred.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		// Older Open/Close requests precede newly enqueued ones, with a hard cap.
		while (!deferred.empty() && m_cues.size() < MaxPending) {
			m_cues.push_front(deferred.back()); deferred.pop_back();
		}
	}

	for (auto &entry : m_playback) {
		auto &play = entry.second;
		if (!play.active)
			continue;
		if (play.request.signal != Signal::Busy && (!gate || !gate(play.request.cue, false))) {
			play.active = false; if (play.request.signal == Signal::Noise) play.availableAt = now + 60000; continue;
		}
		// Do not burst/catch up after a stalled event loop: abandon obsolete tones.
		if (now - play.started > static_cast< std::int64_t >(play.index) * 20000 + 100000) {
			play.active = false;
            if (play.request.signal == Signal::Noise) play.availableAt = now + 60000;
			continue;
		}
		if (now - play.started < static_cast< std::int64_t >(play.index) * 20000)
			continue;
		QByteArray payload;
        bool last = play.finishEarly;
        if (play.request.signal == Signal::Noise) {
            auto state = m_noise.find(entry.first);
            if (state == m_noise.end() || state->second.slot < 0) { play.active = false; continue; }
            RadioNoise::Packet packet;
            if (!m_noiseSlots[std::size_t(state->second.slot)].producer->take(packet)) continue;
            state->second.nextSample = (packet.firstSample + RadioNoise::FrameSamples) % m_noisePCM->size();
            payload = std::move(packet.opus); // Producer frame never becomes an outgoing sequence.
        } else {
            const auto &packets = clip(play.request.signal);
            last = last || play.index + 1 == packets.size(); payload = packets.at(play.index);
        }
		if (!deliver(play.request.recipient, payload, play.nextFrame, last) || last)
			play.active = false;
		if (!play.active) play.availableAt = now + 60000; // Packet duration + small playback retirement gap.
		++play.index;
		play.nextFrame += 2; // Mumble sequence units are 10 ms, Opus packets here are 20 ms.
	}
}

void AudioFeedback::cancel(std::uint64_t connection) {
	std::lock_guard< std::mutex > lock(m_mutex);
	m_pending.erase(
		std::remove_if(m_pending.begin(), m_pending.end(),
					   [connection](const Request &request) { return request.recipient.connection == connection; }),
		m_pending.end());
	m_cues.erase(std::remove_if(m_cues.begin(), m_cues.end(), [connection](const Request &r) {
		return r.recipient.connection == connection || r.cue.sender.connection == connection;
	}), m_cues.end());
	m_noiseRequests.erase(std::remove_if(m_noiseRequests.begin(), m_noiseRequests.end(), [connection](const SquelchPlan::Cue &cue) {
        return cue.sender.connection == connection || cue.recipient.connection == connection;
    }), m_noiseRequests.end());
    for (auto it = m_noise.begin(); it != m_noise.end();) {
        if (it->first == connection || it->second.cue.sender.connection == connection) {
            auto play = m_playback.find(it->first);
            if (play != m_playback.end() && play->second.request.signal == Signal::Noise) {
                const auto &s = it->second;
                if (play->second.active && play->second.index && s.hasReserve
                    && s.reserve.segment != RadioNoise::Segment::Close && m_terminals.size() < MaxRecipients) {
                    m_terminals.push_back({play->second.request.recipient, s.reserve.opus, play->second.nextFrame});
                    play->second.nextFrame += 2;
                }
                play->second.active = false; play->second.availableAt = m_lastNoiseTick + 60000;
            }
            it = m_noise.erase(it);
        } else ++it;
    }
	if (!m_squelchConfigured && !m_noisePCM) {
		m_playback.erase(connection); // Preserve the original Busy-only behaviour.
	} else {
		auto it = m_playback.find(connection);
		if (it != m_playback.end()) {
			// A channel move is not a new session: retain its output sequence space.
			it->second.active = false;
			it->second.lastBusy = -1000000;
            if (m_noisePCM) it->second.availableAt = m_lastNoiseTick + 60000;
		}
	}
}
