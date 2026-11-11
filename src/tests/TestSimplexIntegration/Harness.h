// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#ifndef SIMPLEX_TEST_HARNESS_H_
#define SIMPLEX_TEST_HARNESS_H_
#include "Mumble.pb.h"
#include "MumbleProtocol.h"
#include "crypto/CryptStateOCB2.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEndian>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QUdpSocket>
#include <QtTest/QtTest>
#include <functional>

namespace SimplexTest {
using Type = Mumble::Protocol::TCPMessageType;
const QByteArray audioContent("SIMPLEX_OPAQUE_AUDIO_CONTENT");

class ServerProcess : public QProcess {
public:
	~ServerProcess() override { stop(); }
	void stop() {
		if (state() != NotRunning) {
			terminate();
			if (!waitForFinished(3000)) {
				kill();
				waitForFinished(3000);
			}
		}
	}
};

struct Received {
	unsigned int session;
	std::uint64_t frame;
	bool last;
	QByteArray payload;
};

class Client {
public:
	using Message = QPair< Type, QByteArray >;
	QSslSocket socket;
	QUdpSocket udp;
	CryptStateOCB2 crypt;
	unsigned int session    = 0;
	quint16 port            = 0;
	std::uint64_t nextFrame = 0, lastSent = 0;
	Version::full_t version = Version::fromComponents(1, 2, 5);
	bool sendRaw(Type type, const QByteArray &payload) {
		QByteArray bytes(6, '\0');
		qToBigEndian< quint16 >(static_cast< quint16 >(type), bytes.data());
		qToBigEndian< quint32 >(static_cast< quint32 >(payload.size()), bytes.data() + 2);
		bytes += payload;
		return socket.write(bytes) == bytes.size() && socket.flush();
	}
	bool send(Type type, const google::protobuf::Message &message) {
		return sendRaw(type, QByteArray::fromStdString(message.SerializeAsString()));
	}
	bool receive(Type type, QByteArray &data, std::function< bool(const QByteArray &) > predicate = {},
				 int timeout = 4000, QList< Message > *preceding = nullptr) {
		QElapsedTimer timer;
		timer.start();
		do {
			pump();
			for (int i = 0; i < inbox.size(); ++i) {
				if (inbox.at(i).first == type && (!predicate || predicate(inbox.at(i).second))) {
					// Optional wire-order snapshot up to this exact acknowledgement.
					if (preceding) {
						while (i > 0) {
							preceding->append(inbox.takeFirst());
							--i;
						}
					}
					data = inbox.takeAt(i).second;
					return true;
				}
			}
			socket.waitForReadyRead(5);
		} while (timer.elapsed() < timeout);
		return false;
	}
	void discard(Type type, const std::function< bool(const QByteArray &) > &predicate) {
		pump();
		for (int i = 0; i < inbox.size();) {
			if (inbox.at(i).first == type && predicate(inbox.at(i).second))
				inbox.removeAt(i);
			else
				++i;
		}
	}
	bool barrier(QList< Message > *preceding = nullptr) {
		MumbleProto::Ping ping;
		ping.set_timestamp(++m_barrier);
		QByteArray data;
		return send(Type::Ping, ping) && receive(Type::Ping, data, [&](const QByteArray &bytes) {
				   MumbleProto::Ping reply;
				   return reply.ParseFromArray(bytes.data(), bytes.size()) && reply.timestamp() == m_barrier;
			   }, 4000, preceding);
	}
	bool login(quint16 serverPort, const std::string &name, bool modern = false) {
		port    = serverPort;
		version = modern ? Version::fromComponents(1, 5, 0) : Version::fromComponents(1, 2, 5);
		QObject::connect(&socket, QOverload< const QList< QSslError > & >::of(&QSslSocket::sslErrors),
						 [this] { socket.ignoreSslErrors(); });
		socket.connectToHostEncrypted(QStringLiteral("127.0.0.1"), port);
		if (!socket.waitForEncrypted(5000))
			return false;
		MumbleProto::Version v;
		v.set_version_v1(modern ? 0x10500 : 0x10205);
		MumbleProto::Authenticate auth;
		auth.set_username(name);
		auth.set_opus(true);
		if (name == "SuperUser")
			auth.set_password("simplex-test-admin");
		QByteArray data;
		if (!send(Type::Version, v) || !send(Type::Authenticate, auth) || !receive(Type::ServerSync, data))
			return false;
		MumbleProto::ServerSync sync;
		if (!sync.ParseFromArray(data.data(), data.size()))
			return false;
		session = sync.session();
		return crypt.isValid();
	}
	bool speak(bool overUDP = false, bool last = false, unsigned int target = 0, bool payload = true) {
		return sendFrame(nextFrame++, overUDP, last, target, payload);
	}
	bool sendFrame(std::uint64_t frame, bool overUDP, bool last, unsigned int target = 0, bool payload = true) {
		lastSent = frame;
		Mumble::Protocol::AudioData audio;
		audio.frameNumber     = frame;
		audio.isLastFrame     = last;
		audio.targetOrContext = target;
		if (payload)
			audio.payload = gsl::span< const Mumble::Protocol::byte >(
				reinterpret_cast< const Mumble::Protocol::byte * >(audioContent.constData()),
				static_cast< std::size_t >(audioContent.size()));
		Mumble::Protocol::UDPAudioEncoder< Mumble::Protocol::Role::Client > encoder(version);
		const auto encoded = encoder.encodeAudioPacket(audio);
		const QByteArray bytes(reinterpret_cast< const char * >(encoded.data()), static_cast< int >(encoded.size()));
		if (!overUDP)
			return sendRaw(Type::UDPTunnel, bytes);
		if (udp.state() == QAbstractSocket::UnconnectedState) {
			if (!udp.bind(QHostAddress(QHostAddress::LocalHost), 0))
				return false;
			// Match the authenticated connectivity ping; unrelated datagrams cannot
			// satisfy this fence (as in a normal client).
			if (!udpBarrier())
				return false;
		}
		return sendEncrypted(bytes);
	}
	bool sendEncrypted(const QByteArray &bytes) {
		QByteArray encrypted(bytes.size() + 4, '\0');
		return crypt.encrypt(reinterpret_cast< const unsigned char * >(bytes.constData()),
							 reinterpret_cast< unsigned char * >(encrypted.data()),
							 static_cast< unsigned int >(bytes.size()))
			   && udp.writeDatagram(encrypted, QHostAddress::LocalHost, port) == encrypted.size();
	}
	bool udpBarrier(std::uint64_t timestamp = 0) {
		Mumble::Protocol::UDPPingEncoder< Mumble::Protocol::Role::Client > encoder(version);
		Mumble::Protocol::PingData ping;
		ping.timestamp   = timestamp ? timestamp : ++m_barrier;
		const auto bytes = encoder.encodePingPacket(ping);
		if (!sendEncrypted(
				QByteArray(reinterpret_cast< const char * >(bytes.data()), static_cast< int >(bytes.size()))))
			return false;
		QElapsedTimer timer;
		timer.start();
		do {
			if (!udp.hasPendingDatagrams())
				udp.waitForReadyRead(10);
			while (udp.hasPendingDatagrams()) {
				QByteArray encrypted(static_cast< int >(udp.pendingDatagramSize()), '\0');
				if (udp.readDatagram(encrypted.data(), encrypted.size()) != encrypted.size() || encrypted.size() < 5)
					return false;
				QByteArray plain(encrypted.size() - 4, '\0');
				if (!crypt.decrypt(reinterpret_cast< const unsigned char * >(encrypted.constData()),
								   reinterpret_cast< unsigned char * >(plain.data()),
								   static_cast< unsigned int >(encrypted.size())))
					continue; // Discard duplicates or unauthenticated datagrams; still require the matching ping.
				Mumble::Protocol::UDPDecoder< Mumble::Protocol::Role::Client > decoder(version);
				if (decoder.decode(gsl::span< const Mumble::Protocol::byte >(
						reinterpret_cast< const Mumble::Protocol::byte * >(plain.constData()),
						static_cast< std::size_t >(plain.size())))
					&& decoder.getMessageType() == Mumble::Protocol::UDPMessageType::Ping
					&& decoder.getPingData().timestamp == ping.timestamp)
					return true;
			}
		} while (timer.elapsed() < 2000);
		return false;
	}
	QList< Received > audio(int milliseconds = 80, int minimumPackets = 0) {
		QList< Received > result;
		QElapsedTimer timer;
		timer.start();
		do {
			pump();
			for (int i = 0; i < inbox.size();) {
				if (inbox.at(i).first != Type::UDPTunnel) {
					++i;
					continue;
				}
				const auto bytes = inbox.takeAt(i).second;
				Mumble::Protocol::UDPDecoder< Mumble::Protocol::Role::Client > decoder(version);
				if (decoder.decode(gsl::span< const Mumble::Protocol::byte >(
						reinterpret_cast< const Mumble::Protocol::byte * >(bytes.constData()),
						static_cast< std::size_t >(bytes.size())))) {
					const auto p = decoder.getAudioData();
					result.append({ p.senderSession, p.frameNumber, p.isLastFrame,
									QByteArray(reinterpret_cast< const char * >(p.payload.data()),
											   static_cast< int >(p.payload.size())) });
				}
			}
			socket.waitForReadyRead(5);
		} while (timer.elapsed() < milliseconds || (result.size() < minimumPackets && timer.elapsed() < 2000));
		return result;
	}
	bool expect(const Client &sender, bool yes = true, bool last = false, bool payload = true) {
		const auto packets = audio(80, yes ? 1 : 0);
		if (!yes)
			return packets.isEmpty();
		return packets.size() == 1 && packets[0].session == sender.session && packets[0].frame == sender.lastSent
			   && packets[0].last == last && packets[0].payload == (payload ? audioContent : QByteArray());
	}
	bool move(unsigned int channel) {
		MumbleProto::UserState state;
		state.set_session(session);
		state.set_channel_id(channel);
		QByteArray data;
		return send(Type::UserState, state) && receive(Type::UserState, data, [&](const QByteArray &bytes) {
				   MumbleProto::UserState reply;
				   return reply.ParseFromArray(bytes.data(), bytes.size()) && reply.session() == session
						  && reply.has_channel_id() && reply.channel_id() == channel;
			   });
	}

private:
	QByteArray pending;
	QList< QPair< Type, QByteArray > > inbox;
	std::uint64_t m_barrier = 0;
	void pump() {
		pending += socket.readAll();
		while (pending.size() >= 6) {
			const auto length = qFromBigEndian< quint32 >(pending.constData() + 2);
			if (length > 1000000 || pending.size() < static_cast< int >(6 + length))
				return;
			const auto type = static_cast< Type >(qFromBigEndian< quint16 >(pending.constData()));
			const auto data = pending.mid(6, static_cast< int >(length));
			pending.remove(0, static_cast< int >(6 + length));
			if (type == Type::CryptSetup) {
				MumbleProto::CryptSetup setup;
				if (setup.ParseFromArray(data.data(), data.size()))
					crypt.setKey(setup.key(), setup.client_nonce(), setup.server_nonce());
			}
			inbox.append(qMakePair(type, data));
		}
	}
};

struct Fixture {
	QTemporaryDir directory;
	ServerProcess server;
	quint16 port = 0;
	QString config;
	bool start(const QString &setting, int timeout = 2500, int retry = 3000, bool diagnostics = true,
			   const QByteArray &extra = {}) {
		QTcpServer reservation;
		if (!reservation.listen(QHostAddress::LocalHost, 0))
			return false;
		port = reservation.serverPort();
		reservation.close();
		config = directory.filePath("server.ini");
		QFile file(config);
		if (!file.open(QIODevice::WriteOnly))
			return false;
		QByteArray ini =
			"host=127.0.0.1\nport=" + QByteArray::number(port)
			+ "\ndatabase=" + directory.filePath("state.sqlite").toUtf8()
			+ "\nlogfile=\nice=\nbonjour=false\nusers=20\nbandwidth=72000\nkdfiterations=1000\n"
			+ "simplextimeout=" + QByteArray::number(timeout) + "\nsimplexretrytimeout=" + QByteArray::number(retry)
			+ "\nsimplexdiagnostics=" + (diagnostics ? "true" : "false")
			+ "\nsimplexlogfile=" + directory.filePath("simplex").toUtf8()
			+ "\nvoicediagnostics=true\nvoicediagnosticsfile=" + directory.filePath("voice").toUtf8() + "\n";
		if (!setting.isEmpty())
			ini += "simplex=" + setting.toUtf8() + '\n';
		ini += extra;
		if (file.write(ini) != ini.size())
			return false;
		file.close();
		QProcess password;
		password.start(QStringLiteral(MUMBLE_SERVER_PATH), { "-ini", config, "-supw", "simplex-test-admin" });
		if (!password.waitForFinished(10000) || password.exitCode())
			return false;
		return restart();
	}
	bool restart() {
		server.stop();
		server.setProcessChannelMode(QProcess::MergedChannels);
		server.start(QStringLiteral(MUMBLE_SERVER_PATH), { "-ini", config, "-fg" });
		if (!server.waitForStarted())
			return false;
		QElapsedTimer timer;
		timer.start();
		QByteArray startup;
		while (!startup.contains("Server listening on") && timer.elapsed() < 10000) {
			server.waitForReadyRead(20);
			startup += server.readAll();
		}
		return startup.contains("Server listening on");
	}
	QList< QJsonObject > events(const QString &prefix) {
		QList< QJsonObject > result;
		QFile file(directory.filePath(prefix + ".1.jsonl"));
		if (file.open(QIODevice::ReadOnly))
			while (!file.atEnd()) {
				const auto line = file.readLine();
				if (line.endsWith('\n'))
					result.append(QJsonDocument::fromJson(line).object());
			}
		return result;
	}
	int count(const QString &name, const QString &reason = {}) {
		int n = 0;
		for (const auto &e : events("simplex"))
			if (e.value("event") == name && (reason.isEmpty() || e.value("reason") == reason))
				++n;
		return n;
	}
};

inline unsigned int channel(Client &admin, const char *name) {
	MumbleProto::ChannelState state;
	state.set_parent(0);
	state.set_name(name);
	QByteArray data;
	if (!admin.send(Type::ChannelState, state)
		|| !admin.receive(Type::ChannelState, data, [&](const QByteArray &bytes) {
			   MumbleProto::ChannelState reply;
			   return reply.ParseFromArray(bytes.data(), bytes.size()) && reply.name() == name;
		   }))
		return 0;
	state.ParseFromArray(data.data(), data.size());
	return state.channel_id();
}
} // namespace SimplexTest
#endif
