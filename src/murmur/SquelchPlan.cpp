// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#include "SquelchPlan.h"
#include <algorithm>
#include <chrono>
#include <limits>

constexpr std::size_t SquelchPlan::MaxChannels;
constexpr std::size_t SquelchPlan::MaxRecipients;
constexpr std::int64_t SquelchPlan::MaxCueAgeUs;
constexpr std::int64_t SquelchPlan::MaxCloseCueAgeUs;

SquelchPlan::SquelchPlan(bool enabled, Clock clock) : m_enabled(enabled), m_clock(std::move(clock)) {
	if (!m_clock) m_clock = [] {
		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	};
}

std::vector<SquelchPlan::Cue> SquelchPlan::observe(Identity sender, bool allowed, std::uint64_t frame,
	bool payload, bool last, bool validOpus, const std::vector<Identity> &eligibleRecipients) {
	std::vector<Cue> cues;
	if (!m_enabled || !allowed || !sender.connection || (payload && !validOpus) || (!payload && !last))
		return cues;
	std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
	if (!lock) return cues;
	const auto now = m_clock();
	auto it = m_channels.find(sender.channel);
	const bool newStream = it == m_channels.end() || !(it->second.sender == sender) || it->second.ended;
	if (newStream) {
		if (!payload || m_nextGeneration == std::numeric_limits<std::uint64_t>::max())
			return cues;
		if (it == m_channels.end() && m_channels.size() >= MaxChannels)
			return cues; // Bounded metadata; ordinary speech is outside this component.
		Stream stream{sender, m_nextGeneration++, frame, false, {}};
		for (const auto &recipient : eligibleRecipients) {
			if (!recipient.connection || recipient.connection == sender.connection || recipient.session == sender.session
				|| recipient.channel != sender.channel)
				continue;
			if (std::any_of(stream.recipients.begin(), stream.recipients.end(), [&](const Recipient &r) {
				return r.identity.connection == recipient.connection || r.identity.session == recipient.session;
			}))
				continue;
			stream.recipients.push_back({recipient, false, false});
			if (stream.recipients.size() == MaxRecipients)
				break;
		}
		if (it == m_channels.end())
			it = m_channels.emplace(sender.channel, std::move(stream)).first;
		else
			it->second = std::move(stream);
		for (const auto &recipient : it->second.recipients)
			cues.push_back({Kind::Open, sender, recipient.identity, it->second.generation, now});
	} else {
		if (frame < it->second.highestFrame || (frame == it->second.highestFrame && payload))
			return cues;
		it->second.highestFrame = frame;
	}
	if (last) {
		it->second.ended = true;
		for (const auto &recipient : it->second.recipients)
			cues.push_back({Kind::Close, sender, recipient.identity, it->second.generation, now});
	}
	return cues;
}

std::vector<SquelchPlan::Cue> SquelchPlan::timeout(Identity sender, std::uint64_t lastAcceptedFrame) {
	std::vector<Cue> cues;
	if (!m_enabled) return cues;
	// Same short metadata lock as the previous timeout invalidation. No user
	// pointers, controller calls, file IO, audio encoding or delivery under it.
	std::lock_guard<std::mutex> lock(m_mutex);
	auto stream = m_channels.find(sender.channel);
	if (stream == m_channels.end() || !(stream->second.sender == sender) || stream->second.ended)
		return cues;
	if (stream->second.highestFrame != lastAcceptedFrame) {
		// An accepted packet was not observed as valid Opus: omit the effect and
		// retire this generation rather than attaching it to a later stream.
		m_channels.erase(stream);
		return cues;
	}
	stream->second.ended = true;
	const auto now = m_clock();
	for (const auto &recipient : stream->second.recipients)
		cues.push_back({Kind::Close, sender, recipient.identity, stream->second.generation, now});
	return cues;
}

bool SquelchPlan::consume(const Cue &cue, Identity currentSender, Identity currentRecipient, bool stillEligible) {
	if (!m_enabled || !stillEligible || !(cue.sender == currentSender) || !(cue.recipient == currentRecipient))
		return false;
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto now = m_clock();
	if (now < cue.createdUs || now - cue.createdUs > (cue.kind == Kind::Close ? MaxCloseCueAgeUs : MaxCueAgeUs))
		return false;
	auto stream = m_channels.find(cue.sender.channel);
	if (stream == m_channels.end() || stream->second.generation != cue.transmission
		|| !(stream->second.sender == cue.sender))
		return false;
	for (auto &recipient : stream->second.recipients) {
		if (!(recipient.identity == cue.recipient))
			continue;
		if (cue.kind == Kind::Noise || cue.kind == Kind::NoiseEnd) return false;
		if (cue.kind == Kind::Open && !recipient.opened) {
			recipient.opened = true;
			return true;
		}
		if (cue.kind == Kind::Close && stream->second.ended && recipient.opened && !recipient.closed) {
			recipient.closed = true;
			return true;
		}
		return false;
	}
	return false;
}

void SquelchPlan::invalidate(std::uint64_t connection) {
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto it = m_channels.begin(); it != m_channels.end();) {
		if (it->second.sender.connection == connection) {
			it = m_channels.erase(it);
		} else {
			auto &recipients = it->second.recipients;
			recipients.erase(std::remove_if(recipients.begin(), recipients.end(), [connection](const Recipient &r) {
				return r.identity.connection == connection;
			}), recipients.end());
			++it;
		}
	}
}

void SquelchPlan::clear() {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_channels.clear(); // Generation never resets: pre-clear cues remain invalid.
}

std::size_t SquelchPlan::size() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_channels.size();
}

bool SquelchPlan::current(const Cue &cue) const {
	std::lock_guard<std::mutex> lock(m_mutex);
	auto stream = m_channels.find(cue.sender.channel);
	if (stream == m_channels.end() || stream->second.generation != cue.transmission
		|| !(stream->second.sender == cue.sender)) return false;
	for (const auto &recipient : stream->second.recipients)
		if (recipient.identity == cue.recipient) {
			if (cue.kind == Kind::Noise) return !stream->second.ended;
			if (cue.kind == Kind::NoiseEnd) return true;
			return recipient.opened && (cue.kind == Kind::Open || (stream->second.ended && recipient.closed));
		}
	return false;
}
