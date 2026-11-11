// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#include "SimplexController.h"
#include <algorithm>
#include <chrono>
#include <utility>

std::int64_t SimplexController::monotonicUs() {
	return std::chrono::duration_cast< std::chrono::microseconds >(std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

SimplexController::SimplexController(int timeout, int retry, Sink sink, Clock clock)
	: m_timeoutUs(std::max(50, std::min(60000, timeout)) * 1000LL),
	  m_retryUs(std::max< std::int64_t >(m_timeoutUs, std::max(50, std::min(60000, retry)) * 1000LL)),
	  m_sink(std::move(sink)), m_clock(clock ? std::move(clock) : Clock(monotonicUs)) {
}

int SimplexController::timeoutMs() const {
	return static_cast< int >(m_timeoutUs / 1000);
}
int SimplexController::retryMs() const {
	return static_cast< int >(m_retryUs / 1000);
}

void SimplexController::event(const User &user, const char *name, const QString &reason, std::int64_t now,
							  std::uint64_t packets) {
	if (m_sink) {
		Event record;
		record.context = user.context;
		record.event   = QString::fromLatin1(name);
		record.reason  = reason;
		record.timeUs  = now;
		record.frame   = user.highestFrame;
		record.packets = packets;
		record.udp     = user.udp;
		m_sink(record); // Sink must be nonblocking; never call back into the controller.
	}
}

void SimplexController::release(User &user, const QString &reason, std::int64_t now) {
	if (user.phase != Phase::Owning)
		return;
	auto owner = m_owners.find(user.context.channel);
	if (owner != m_owners.end() && owner->second == user.context.connection)
		m_owners.erase(owner);
	event(user, "released", reason, now, user.accepted);
	user.phase  = Phase::Blocked;
	user.denied = 0;
}

void SimplexController::block(User &user, const QString &reason, std::int64_t now) {
	release(user, reason, now);
	if (user.phase != Phase::Blocked || user.denied == 0) {
		user.phase  = Phase::Blocked;
		user.denied = 1;
		event(user, "denied", reason, now, user.denied);
	} else {
		++user.denied;
	}
}

void SimplexController::finishBlocked(User &user, const QString &reason, std::int64_t now) {
	if (user.phase == Phase::Blocked && user.denied)
		event(user, "denied_end", reason, now, user.denied);
	user.phase            = Phase::Idle;
	user.denied           = 0;
	user.feedbackReported = false;
}

void SimplexController::rejectFeedback(User &user, const Packet &packet, Transmission &result) {
	// Markers alone and forbidden/muted paths do not start an audible attempt.
	if (packet.payload && packet.eligible && packet.target == 0 && !user.feedbackReported) {
		user.feedbackReported = true;
		result.feedback       = true;
	}
}

void SimplexController::expireChannel(unsigned int channel, std::int64_t now) {
	auto owner = m_owners.find(channel);
	if (owner == m_owners.end())
		return;
	User &user = m_users.at(owner->second);
	if (now - user.lastAccepted >= m_timeoutUs)
		release(user, QStringLiteral("timeout_suspected"), now);
}

SimplexController::Transmission SimplexController::begin(const Packet &packet) {
	Transmission result{ std::unique_lock< std::mutex >(m_mutex), false };
	const auto now = m_clock(); // Sample after acquiring the lock, for both UDP and TCP.
	if (!packet.context.connection || (!packet.payload && !packet.last))
		return result;
	expireChannel(packet.context.channel, now);
	User &user = m_users[packet.context.connection];
	if (user.seen && user.phase != Phase::Idle && user.context.channel != packet.context.channel) {
		block(user, QStringLiteral("channel_changed"), now);
	}
	const bool quiet = !user.seen || now - user.lastSeen >= m_retryUs;
	if (quiet) {
		release(user, QStringLiteral("timeout_suspected"), now);
		finishBlocked(user, QStringLiteral("quiet_rearm"), now);
		user.seen = false; // Sequence resets are only trusted after a quiet interval.
	}
	user.context     = packet.context;
	user.udp         = packet.udp;
	const bool fresh = !user.seen || packet.frame > user.highestFrame;
	// Some clients send an empty terminator at the last data frame's sequence.
	const bool endAtSameFrame =
		user.seen && packet.frame == user.highestFrame && packet.last && !packet.payload && user.phase != Phase::Idle;
	user.lastSeen = now; // Even rejected/stale packets keep a rejected attempt latched.
	if (!fresh && !endAtSameFrame) {
		if (user.phase == Phase::Idle)
			block(user, QStringLiteral("sequence_not_advanced"), now);
		else if (user.phase == Phase::Blocked)
			++user.denied;
		rejectFeedback(user, packet, result);
		return result; // Never renew a lease or act on a stale terminator.
	}
	user.seen         = true;
	user.highestFrame = packet.frame;
	if (!packet.eligible || packet.target != 0) {
		block(user, packet.target ? QStringLiteral("unsupported_target") : QStringLiteral("not_eligible"), now);
		// A marker on a forbidden path is not a trusted boundary for normal speech.
		return result;
	}
	if (user.phase == Phase::Blocked) {
		block(user, QStringLiteral("retry_required"), now);
		rejectFeedback(user, packet, result);
		if (packet.last)
			finishBlocked(user, QStringLiteral("marker_confirmed"), now);
		return result;
	}
	if (user.phase == Phase::Idle) {
		if (!packet.payload)
			return result; // An end-only packet never claims a channel.
		if (m_owners.count(user.context.channel)) {
			block(user, QStringLiteral("busy"), now);
			rejectFeedback(user, packet, result);
			if (packet.last)
				finishBlocked(user, QStringLiteral("marker_confirmed"), now);
			return result;
		}
		m_owners[user.context.channel] = user.context.connection;
		user.phase                     = Phase::Owning;
		user.accepted                  = 0;
		event(user, "granted", QStringLiteral("first_accepted_packet"), now, 0);
	}
	user.lastAccepted = now;
	++user.accepted;
	result.allowed = true;
	if (packet.last) {
		release(user, QStringLiteral("marker_confirmed"), now);
		user.phase            = Phase::Idle;
		user.feedbackReported = false;
	}
	return result;
}

void SimplexController::expire() {
	std::lock_guard< std::mutex > lock(m_mutex);
	const auto now = m_clock();
	for (auto it = m_owners.begin(); it != m_owners.end();) {
		const auto channel = (it++)->first;
		expireChannel(channel, now);
	}
}

void SimplexController::invalidate(std::uint64_t connection, const QString &reason) {
	std::lock_guard< std::mutex > lock(m_mutex);
	auto user = m_users.find(connection);
	if (user != m_users.end() && user->second.phase != Phase::Idle) {
		const auto now = m_clock();
		release(user->second, reason, now);
		user->second.lastSeen = now;
	}
}

void SimplexController::disconnect(std::uint64_t connection) {
	std::lock_guard< std::mutex > lock(m_mutex);
	auto user = m_users.find(connection);
	if (user == m_users.end())
		return;
	const auto now = m_clock();
	release(user->second, QStringLiteral("disconnected"), now);
	finishBlocked(user->second, QStringLiteral("disconnected"), now);
	m_users.erase(user);
}

void SimplexController::shutdown() {
	std::lock_guard< std::mutex > lock(m_mutex);
	const auto now = m_clock();
	for (auto &user : m_users) {
		release(user.second, QStringLiteral("server_stop"), now);
		finishBlocked(user.second, QStringLiteral("server_stop"), now);
	}
	m_users.clear();
}
