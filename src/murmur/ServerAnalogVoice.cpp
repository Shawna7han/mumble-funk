// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "Channel.h"
#include "Meta.h"
#include "Server.h"
#include "ServerUser.h"

#include <QtCore/QMetaObject>
#include <QtCore/QTimer>

#include <algorithm>
#include <stdexcept>

namespace {
using Service = funkvoice::AnalogVoiceService;
using Stream  = funkvoice::AnalogVoiceStream;
// Virtual servers are constructed/destroyed on the main thread. Bound the
// process-wide opt-in cost as well as each individual service's fixed pools.
unsigned int analogServers              = 0;
constexpr unsigned int MaxAnalogServers = 4;
} // namespace

void Server::initializeAnalogVoice() {
	const QString enabled = getConf("analogvoice", Meta::mp.analogVoice).toString().trimmed().toLower();
	if (enabled == QLatin1String("false") || enabled == QLatin1String("0"))
		return;
	try {
		if (enabled != QLatin1String("true") && enabled != QLatin1String("1"))
			throw std::invalid_argument("analogvoice must be true or false");
		if (!m_simplex)
			throw std::invalid_argument("analogvoice requires simplex=true");
		const QString name = getConf("analogvoiceprofile", Meta::mp.analogVoiceProfile).toString();
		if (name != QLatin1String("cb-am") && name != QLatin1String("business-fm")
			&& name != QLatin1String("narrowband"))
			throw std::invalid_argument("unknown analogvoiceprofile (cb-am, business-fm, narrowband)");
		if (analogServers == MaxAnalogServers)
			throw std::runtime_error("process limit of four analog voice servers reached");
		m_analogVoice = std::make_unique< Service >(funkvoice::profile(name.toStdString()), [this] {
			QMetaObject::invokeMethod(
				this, [this] { deliverAnalogVoice(); }, Qt::QueuedConnection);
		});
		++analogServers;
		log(QStringLiteral("Experimental analog voice enabled: %1; one worker, four codecs, fixed storage=%2 bytes; "
						   "ordered Squelch/Noise lifecycle; restart required")
				.arg(name)
				.arg(static_cast< qulonglong >(Service::fixedStorageBytes())));
	} catch (const std::exception &error) {
		log(QStringLiteral("Analog voice disabled: %1").arg(QString::fromUtf8(error.what())));
	}
}

void Server::shutdownAnalogVoice() {
	// UDP and expiry timer are already stopped; QObject remains alive until join.
	if (m_analogVoice) {
		m_analogVoice->shutdown();
		const auto stats = m_analogVoice->statistics();
		log(QStringLiteral(
				"Analog voice stopped: accepted=%1 inputFull=%2 expired=%3 codecErrors=%4 noLane=%5 bypassStarts=%6")
				.arg(stats.accepted)
				.arg(stats.inputFull)
				.arg(stats.expired)
				.arg(stats.codecErrors)
				.arg(stats.noLane)
				.arg(stats.bypassStarts));
		m_analogVoice.reset();
		--analogServers;
	}
}

void Server::syncAnalogVoice(ServerUser &user) {
	if (!m_analogVoice || !user.cChannel)
		return;
	const bool send    = !user.bMute && !user.bSuppress && !user.bSelfMute;
	const bool receive = !user.bDeaf && !user.bSelfDeaf;
	const auto channel = static_cast< std::uint32_t >(user.cChannel->iId);
	if (!user.analogIdentity.epoch) {
		user.analogIdentity = m_analogVoice->connect(user.diagnosticConnection, user.uiSession, channel, send, receive);
	} else if (user.analogIdentity.channel != channel || user.analogMaySend != send) {
		user.analogIdentity = m_analogVoice->update(user.analogIdentity, channel, send, receive);
	} else {
		m_analogVoice->setReceive(user.analogIdentity, receive);
	}
	user.analogMaySend = send;
}

void Server::releaseAnalogVoice(const SimplexController::Event &event) {
	// Called under Simplex only: never acquire the voice lock or dereference users.
	if (event.event != QLatin1String("released") || event.reason == QLatin1String("marker_confirmed"))
		return;
	for (auto &entry : m_analogTransmissions) {
		if (entry.connection != event.context.connection)
			continue;
		if (event.reason == QLatin1String("timeout_suspected"))
			m_analogVoice->finish(entry.handle, event.frame, Service::Time(std::chrono::microseconds(event.timeUs)));
		else {
			m_analogVoice->cancel(entry.handle);
            if (m_squelchPlan) m_squelchPlan->invalidate(event.context.connection);
        }
		entry = {};
	}
}

void Server::submitAnalogVoice(ServerUser &user, const Mumble::Protocol::AudioData &audio,
							   AudioReceiverBuffer &receivers, bool started) {
	// Voice read lock + the still-live Transmission guard serialize this handoff.
	AnalogTransmission *entry = nullptr;
	for (auto &candidate : m_analogTransmissions)
		if (candidate.connection == user.diagnosticConnection)
			entry = &candidate;
	if (started) {
		if (entry) {
			m_analogVoice->cancel(entry->handle);
			*entry = {};
		}
		const auto handle = m_analogVoice->open(user.analogIdentity);
		if (!handle)
			return; // No ticket lane: never bypass the channel barrier.
		entry  = &m_analogTransmissions[handle.lane];
		*entry = { user.diagnosticConnection, handle };
	}
	if (!entry)
		return;
	Service::Input input;
	input.payload = audio.payload.data();
	input.bytes   = audio.payload.size();
	input.frame   = audio.frameNumber;
	input.end     = audio.isLastFrame;
	const int samples =
		input.bytes ? opus_packet_get_nb_samples(input.payload, static_cast< opus_int32 >(input.bytes), 48000) : 0;
	if (audio.usedCodec != Mumble::Protocol::AudioCodec::Opus || samples < 0) {
		m_analogVoice->cancel(entry->handle);
		*entry = {};
		return;
	}
	input.samples               = static_cast< std::uint32_t >(samples);
	input.metadata.position     = audio.position;
	input.metadata.positional   = audio.containsPositionalData;
	std::size_t positionalCount = 0;
	for (bool positional : { true, false }) {
		for (const auto &receiver : receivers.getReceivers(positional)) {
			const auto &identity = receiver.getReceiver().analogIdentity;
			if (!identity.epoch)
				continue;
			if (input.recipients.count == Service::MaxRecipients) {
				m_analogVoice->cancel(entry->handle);
				*entry = {};
				return; // Refuse the generation rather than silently selecting a subset.
			}
			input.recipients.identities[input.recipients.count++] = identity;
			if (positional)
				input.metadata.positionalConnections[positionalCount++] = identity.connection;
		}
	}
	const auto receipt = m_analogVoice->submit(entry->handle, input);
	if (receipt.status == Service::Admission::Invalid || receipt.status == Service::Admission::Stale
		|| receipt.status == Service::Admission::Stopped || receipt.status == Service::Admission::Closed) {
		m_analogVoice->cancel(entry->handle);
		*entry = {};
	} else if (input.end) {
		// InputFull/Faulted also reserve the end. Never manufacture a Simplex release.
		*entry = {};
	}
}

void Server::deliverAnalogVoice() {
	Q_ASSERT(QThread::currentThread() == thread());
	if (!m_analogVoice)
		return;
	if (!qrwlVoiceThread.tryLockForWrite()) {
		// Keep the service notification pending until we can consume it. Exactly
		// one context-bound retry, with no popped/lost data or terminal batches.
		QTimer::singleShot(1, this, [this] { deliverAnalogVoice(); });
		return;
	}
	m_analogVoice->acknowledgeNotification();
	Service::Batch batch;
	AudioReceiverBuffer receivers;
	Mumble::Protocol::UDPAudioEncoder< Mumble::Protocol::Role::Server > encoder;
	while (m_analogVoice->take(batch)) {
		auto *sender = qhUsers.value(batch.completed.sender.session, nullptr);
		if (!sender || !(sender->analogIdentity == batch.completed.sender)
			|| sender->sState != ServerUser::Authenticated || !sender->cChannel || sender->bMute || sender->bSuppress
			|| sender->bSelfMute || !ChanACL::hasPermission(sender, sender->cChannel, ChanACL::Speak, nullptr))
			continue;
		for (std::size_t i = 0; i < batch.audio.count; ++i) {
			const auto &packet = batch.audio.packets[i];
			Mumble::Protocol::AudioData audio;
			audio.senderSession = sender->uiSession;
			audio.frameNumber   = packet.frame;
			audio.payload       = { packet.payload.data(), packet.bytes };
			audio.isLastFrame   = packet.end != Stream::End::None;
			// Never borrow metadata from the completed (possibly later) input.
			const Service::Metadata *metadata = batch.sourceCounts[i] ? &batch.sources[i][0].metadata : nullptr;
			if (metadata) {
				audio.position               = metadata->position;
				audio.containsPositionalData = metadata->positional;
			}
			receivers.clear();
			for (std::size_t r = 0; r < batch.recipients.count; ++r) {
				const auto &identity = batch.recipients.identities[r];
				auto *receiver       = qhUsers.value(identity.session, nullptr);
				if (!receiver || !(receiver->analogIdentity == identity)
					|| receiver->sState != ServerUser::Authenticated || receiver->cChannel != sender->cChannel)
					continue;
				const bool positional = metadata && metadata->positional
										&& std::find(metadata->positionalConnections.begin(),
													 metadata->positionalConnections.end(), identity.connection)
											   != metadata->positionalConnections.end();
				receivers.addReceiver(*sender, *receiver, Mumble::Protocol::AudioContext::NORMAL, positional);
			}
			Service::Recipients delivered;
            sendVoicePacket(audio, receivers, encoder, true, &delivered);
            if (m_squelchPlan)
                requestAnalogCues(m_analogLifecycle.delivered(*m_squelchPlan, batch, i, delivered));
		}
        if (m_squelchPlan)
            requestAnalogCues(m_analogLifecycle.complete(*m_squelchPlan, batch), batch.completed.technical);
	}
	qrwlVoiceThread.unlock();
}

void Server::requestAnalogCues(const std::vector<SquelchPlan::Cue> &cues, bool timeout) {
    for (const auto &cue : cues) {
        if (cue.kind == SquelchPlan::Kind::Open) {
            if (m_noiseEnabled) m_audioFeedback->requestNoise(cue);
            if (!m_squelchOpenEnabled) {
                m_squelchPlan->consume(cue, cue.sender, cue.recipient, true);
                continue;
            }
        } else if (!m_squelchCloseEnabled || (timeout && !m_squelchTimeoutCloseEnabled)) {
            continue;
        }
        m_audioFeedback->requestSquelch(cue);
    }
}
