#pragma once
// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "../TestSimplexIntegration/Harness.h"
#include "AnalogVoiceStream.h"
#include <cmath>
#include <memory>
#ifdef MUMBLE_TEST_VOICE_GATE_AVAILABLE
#	include <csignal>
#	include <sys/syscall.h>
#	include <fcntl.h>
#	include <poll.h>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

using namespace SimplexTest;
using Stream = funkvoice::AnalogVoiceStream;
namespace {
struct B4Fixture : Fixture {
	bool start(const QString &setting, int timeout = 2500, int retry = 3000, bool diagnostics = false,
			   const QByteArray &extra = {}) {
		// These fixtures test transcoding immediately after login. The separate
		// bandwidth test covers rejection; do not depend on login taking long enough.
		return Fixture::start(setting, timeout, retry, diagnostics, "bandwidth=10000000\n" + extra);
	}
};
inline QByteArray opus(int samples) {
	int error = 0;
	std::unique_ptr< OpusEncoder, decltype(&opus_encoder_destroy) > encoder(
		opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error), opus_encoder_destroy);
	if (!encoder || error)
		return {};
	opus_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(24000));
	std::vector< float > pcm(static_cast< std::size_t >(samples));
	for (std::size_t i = 0; i < pcm.size(); ++i)
		pcm[i] = 0.2f * std::sin(static_cast< float >(i) * 0.1f);
	QByteArray result(1024, '\0');
	// A real 120-ms packet formed from two independently encoded 60-ms frames.
	if (samples == 5760) {
		std::unique_ptr< OpusRepacketizer, decltype(&opus_repacketizer_destroy) > rep(opus_repacketizer_create(),
																					  opus_repacketizer_destroy);
		QByteArray a(1024, '\0'), b(1024, '\0');
		const int na =
			opus_encode_float(encoder.get(), pcm.data(), 2880, reinterpret_cast< unsigned char * >(a.data()), 1024);
		const int nb = opus_encode_float(encoder.get(), pcm.data() + 2880, 2880,
										 reinterpret_cast< unsigned char * >(b.data()), 1024);
		if (na < 0 || nb < 0 || opus_repacketizer_cat(rep.get(), reinterpret_cast< unsigned char * >(a.data()), na)
			|| opus_repacketizer_cat(rep.get(), reinterpret_cast< unsigned char * >(b.data()), nb))
			return {};
		const int n = opus_repacketizer_out(rep.get(), reinterpret_cast< unsigned char * >(result.data()), 1024);
		if (n < 0)
			return {};
		result.resize(n);
	} else {
		const int n = opus_encode_float(encoder.get(), pcm.data(), samples,
										reinterpret_cast< unsigned char * >(result.data()), 1024);
		if (n < 0)
			return {};
		result.resize(n);
	}
	return result;
}
inline bool sendAudio(Client &client, const QByteArray &payload, std::uint64_t frame, bool last, bool udp = false,
			   float x = 1.25f) {
	Mumble::Protocol::AudioData audio;
	audio.payload                = { reinterpret_cast< const unsigned char * >(payload.constData()),
                      static_cast< std::size_t >(payload.size()) };
	audio.frameNumber            = frame;
	audio.isLastFrame            = last;
	audio.containsPositionalData = true;
	audio.position               = { x, 2.5f, -3.0f };
	Mumble::Protocol::UDPAudioEncoder< Mumble::Protocol::Role::Client > encoder(client.version);
	const auto bytes = encoder.encodeAudioPacket(audio);
	const QByteArray wire(reinterpret_cast< const char * >(bytes.data()), static_cast< int >(bytes.size()));
	if (!udp)
		return client.sendRaw(Type::UDPTunnel, wire);
	if (client.udp.state() == QAbstractSocket::UnconnectedState
		&& !client.udp.bind(QHostAddress(QHostAddress::LocalHost), 0))
		return false;
	return client.sendEncrypted(wire);
}
struct Packet {
	QByteArray payload;
	std::uint64_t frame  = 0;
	unsigned int session = 0;
	bool last = false, positional = false;
	std::array< float, 3 > position{};
	float volume = 0;
};
inline bool receiveAudio(Client &client, Packet &packet, bool udp = false, int timeout = 2000) {
	QByteArray wire;
	if (!udp) {
		if (!client.receive(Type::UDPTunnel, wire, {}, timeout))
			return false;
	} else {
		QElapsedTimer timer;
		timer.start();
		while (!client.udp.hasPendingDatagrams() && timer.elapsed() < timeout)
			client.udp.waitForReadyRead(5);
		if (!client.udp.hasPendingDatagrams())
			return false;
		QByteArray encrypted(static_cast< int >(client.udp.pendingDatagramSize()), '\0');
		if (client.udp.readDatagram(encrypted.data(), encrypted.size()) != encrypted.size())
			return false;
		if (encrypted.size() < 4)
			return false;
		wire.resize(encrypted.size() - 4);
		if (!client.crypt.decrypt(reinterpret_cast< const unsigned char * >(encrypted.constData()),
								  reinterpret_cast< unsigned char * >(wire.data()),
								  static_cast< unsigned int >(encrypted.size())))
			return false;
	}
	Mumble::Protocol::UDPDecoder< Mumble::Protocol::Role::Client > decoder(client.version);
	if (!decoder.decode(
			{ reinterpret_cast< const unsigned char * >(wire.constData()), static_cast< std::size_t >(wire.size()) })
		|| decoder.getMessageType() != Mumble::Protocol::UDPMessageType::Audio)
		return false;
	const auto audio = decoder.getAudioData();
	packet           = { QByteArray(reinterpret_cast< const char * >(audio.payload.data()),
                          static_cast< int >(audio.payload.size())),
               audio.frameNumber,
               audio.senderSession,
               audio.isLastFrame,
               audio.containsPositionalData,
               audio.position,
               audio.volumeAdjustment.factor };
	return true;
}
inline bool setDeaf(Client &client, bool deaf) {
	MumbleProto::UserState state;
	state.set_self_deaf(deaf);
	state.set_self_mute(deaf);
	return client.send(Type::UserState, state) && client.barrier();
}
} // namespace
