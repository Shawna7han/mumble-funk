// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#pragma once
#include "MumbleProtocol.h"
#include <QtCore/QtEndian>
namespace Mumble {
namespace Protocol {
	// Caller supplies the existing six-byte TCP header area; no alignment required.
	inline void writeUDPTunnelHeader(unsigned char *bytes, quint32 payloadBytes) {
		qToBigEndian(static_cast< quint16 >(TCPMessageType::UDPTunnel), bytes);
		qToBigEndian(payloadBytes, bytes + 2);
	}
} // namespace Protocol
} // namespace Mumble
