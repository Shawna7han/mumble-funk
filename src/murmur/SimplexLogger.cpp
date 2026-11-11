// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#include "SimplexLogger.h"
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUuid>
#include <algorithm>

SimplexLogger::SimplexLogger(Config config) : m_config(std::move(config)) {
	m_config.maxBytes = std::max< std::int64_t >(4096, m_config.maxBytes);
	m_queue.reserve(4096);
	m_writer = std::thread(&SimplexLogger::run, this);
}

SimplexLogger::~SimplexLogger() {
	m_accepting = false;
	{
		std::lock_guard< std::mutex > lock(m_mutex);
		m_stopping = true;
	}
	m_ready.notify_one();
	m_writer.join();
}

void SimplexLogger::submit(const SimplexController::Event &event) {
	if (!m_accepting)
		return;
	if ((event.event == QLatin1String("granted") && !m_config.grants)
		|| (event.event.startsWith(QLatin1String("denied")) && !m_config.denials)
		|| (event.event == QLatin1String("released") && !m_config.releases))
		return;
	std::unique_lock< std::mutex > lock(m_mutex, std::try_to_lock);
	if (!lock.owns_lock() || m_queue.size() == 4096) {
		++m_dropped;
		return;
	}
	if (m_stopping)
		return;
	m_queue.push_back(event);
	lock.unlock();
	m_ready.notify_one();
}

void SimplexLogger::run() {
	QFile file(m_config.path);
	const QString run = QUuid::createUuid().toString(QUuid::WithoutBraces);
	auto open         = [&]() {
        return file.open(QIODevice::WriteOnly | QIODevice::Append)
               && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
	};
	auto fail = [&]() {
		m_accepting = false;
		qWarning("Simplex diagnostics: cannot write %s; simplex enforcement stays enabled", qPrintable(m_config.path));
	};
	if (!open()) {
		fail();
		return;
	}
	qint64 bytes = file.size();
	auto write   = [&](QJsonObject object) {
        object.insert(QStringLiteral("run"), run);
        object.insert(QStringLiteral("server"), m_config.server);
        const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if (bytes && bytes + line.size() > m_config.maxBytes) {
            file.close();
            const QString backup = m_config.path + QStringLiteral(".1");
            if ((QFile::exists(backup) && !QFile::remove(backup)) || !QFile::rename(m_config.path, backup) || !open())
                return false;
            bytes = 0;
        }
        if (file.write(line) != line.size())
            return false;
        bytes += line.size();
        return true;
	};
	QJsonObject started;
	started.insert(QStringLiteral("event"), QStringLiteral("simplex_log_started"));
	started.insert(QStringLiteral("utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	started.insert(QStringLiteral("time_us"), QString::number(SimplexController::monotonicUs()));
	if (!write(started) || !file.flush()) {
		fail();
		return;
	}
	std::vector< SimplexController::Event > batch;
	batch.reserve(4096);
	std::uint64_t reported = 0;
	for (;;) {
		bool stopping;
		{
			std::unique_lock< std::mutex > lock(m_mutex);
			m_ready.wait_for(lock, std::chrono::milliseconds(100), [&] { return m_stopping || !m_queue.empty(); });
			batch.swap(m_queue);
			stopping = m_stopping;
		}
		for (const auto &event : batch) {
			QJsonObject object;
			object.insert(QStringLiteral("event"), event.event);
			object.insert(QStringLiteral("reason"), event.reason);
			object.insert(QStringLiteral("time_us"), QString::number(event.timeUs));
			object.insert(QStringLiteral("connection"), QString::number(event.context.connection));
			object.insert(QStringLiteral("session"), static_cast< int >(event.context.session));
			object.insert(QStringLiteral("channel"), static_cast< int >(event.context.channel));
			object.insert(QStringLiteral("user"), event.context.user);
			object.insert(QStringLiteral("frame"), QString::number(event.frame));
			object.insert(QStringLiteral("packets"), QString::number(event.packets));
			object.insert(QStringLiteral("transport"), event.udp ? QStringLiteral("udp") : QStringLiteral("tcp"));
			if (event.event.startsWith(QLatin1String("analog_"))) {
				if (event.event != QLatin1String("analog_submit"))
					object.insert(QStringLiteral("transport"), QStringLiteral("server"));
				object.insert(QStringLiteral("generation"), QString::number(event.analogGeneration));
				object.insert(QStringLiteral("ticket"), QString::number(event.analogTicket));
				object.insert(QStringLiteral("routing_epoch"), QString::number(event.analogEpoch));
				object.insert(QStringLiteral("samples"), int(event.analogSamples));
				object.insert(QStringLiteral("output_packets"), int(event.analogOutputs));
				object.insert(QStringLiteral("recipients"), int(event.analogRecipients));
				object.insert(QStringLiteral("local_handoffs"), int(event.analogHandoffs));
				if (event.analogQueueKnown) {
					object.insert(QStringLiteral("queued_packets"), int(event.analogQueuedPackets));
					object.insert(QStringLiteral("queued_samples"), int(event.analogQueuedSamples));
				}
				if (!event.analogCodecStage.isEmpty()) {
					object.insert(QStringLiteral("codec_stage"), event.analogCodecStage);
					object.insert(QStringLiteral("stream_error"), event.analogStreamError);
					object.insert(QStringLiteral("codec_error"), event.analogCodecError);
				}
				if (event.event == QLatin1String("analog_handoff"))
					object.insert(QStringLiteral("output_frame"), QString::number(event.analogOutputFrame));
			}
			if (!write(object)) {
				fail();
				return;
			}
		}
		batch.clear();
		const auto dropped = m_dropped.load();
		if (dropped != reported || stopping) {
			QJsonObject status;
			status.insert(QStringLiteral("event"),
						  stopping ? QStringLiteral("simplex_log_stopped") : QStringLiteral("diagnostic_loss"));
			status.insert(QStringLiteral("dropped_total"), QString::number(dropped));
			if (!write(status)) {
				fail();
				return;
			}
			reported = dropped;
		}
		if (!file.flush()) {
			fail();
			return;
		}
		if (stopping)
			return;
	}
}
