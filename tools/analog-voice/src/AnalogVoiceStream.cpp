// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "AnalogVoiceStream.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace funkvoice {
namespace {
	constexpr std::uint64_t MaxFrame = std::numeric_limits< std::uint64_t >::max();
	void checked(int status) {
		if (status != OPUS_OK) {
			throw std::runtime_error(opus_strerror(status));
		}
	}
} // namespace

AnalogVoiceStream::AnalogVoiceStream(const Parameters &parameters) : m_processor(parameters) {
	int status = OPUS_OK;
	m_decoder.reset(opus_decoder_create(SampleRate, 1, &status));
	checked(status);
	m_encoder.reset(opus_encoder_create(SampleRate, 1, OPUS_APPLICATION_VOIP, &status));
	checked(status);
	if (!m_decoder || !m_encoder) {
		throw std::runtime_error("Opus codec allocation failed");
	}
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_BITRATE(40000)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_COMPLEXITY(5)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_DTX(0)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_INBAND_FEC(0)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_GET_LOOKAHEAD(&m_lookahead)));
	// Half-frame inputs plus this delay need at most one EOF packet (ten per call).
	if (m_lookahead < 0 || m_lookahead > static_cast< int >(FrameSamples / 2)) {
		throw std::runtime_error("Unsupported live Opus lookahead");
	}
}

void AnalogVoiceStream::reset() noexcept {
	m_state    = State::Idle;
	m_pcmCount = m_pcmSpanCount = m_delaySpanCount = 0;
	m_hasReserve = m_encoded = m_hasSource = false;
	m_processor.reset();
}

bool AnalogVoiceStream::begin(std::uint64_t sourceFrame, std::uint64_t outputFrameFloor) noexcept {
	if (m_state == State::Active) {
		return false;
	}
	reset();
	if (opus_decoder_ctl(m_decoder.get(), OPUS_RESET_STATE) != OPUS_OK
		|| opus_encoder_ctl(m_encoder.get(), OPUS_RESET_STATE) != OPUS_OK) {
		m_state = State::Bypass;
		return false;
	}
	const std::uint64_t outputFrame = std::max({ m_outputCursor, sourceFrame, outputFrameFloor });
	if (outputFrame > MaxFrame - 2) {
		m_state = State::Bypass;
		return false;
	}
	m_sourceCursor = sourceFrame;
	m_outputCursor = outputFrame;
	if (m_lookahead) {
		m_delaySpans[0]  = { Origin::EncoderDelay, 0, 0, 0, static_cast< std::uint16_t >(m_lookahead) };
		m_delaySpanCount = 1;
	}
	m_state = State::Active;
	return true;
}

bool AnalogVoiceStream::copyRange(const Spans &from, std::size_t fromCount, std::size_t offset, std::size_t count,
								  Spans &to, std::size_t &toCount, std::size_t destination) noexcept {
	for (std::size_t i = 0; i < fromCount; ++i) {
		const Span &span        = from[i];
		const std::size_t first = std::max(offset, std::size_t(span.outputOffset));
		const std::size_t last  = std::min(offset + count, std::size_t(span.outputOffset) + span.samples);
		if (first >= last) {
			continue;
		}
		if (toCount == to.size()) {
			return false;
		}
		Span part = span;
		part.sourceOffset += static_cast< std::uint32_t >(first - span.outputOffset);
		part.outputOffset = static_cast< std::uint16_t >(destination + first - offset);
		part.samples      = static_cast< std::uint16_t >(last - first);
		to[toCount++]     = part;
	}
	return true;
}

bool AnalogVoiceStream::encode(Result &result) noexcept {
	if (m_outputCursor > MaxFrame - 2) {
		result.error = Error::SequenceOverflow;
		return false;
	}
	Packet packet;
	packet.frame = m_outputCursor;
	if (!copyRange(m_delaySpans, m_delaySpanCount, 0, m_lookahead, packet.spans, packet.spanCount, 0)
		|| !copyRange(m_pcmSpans, m_pcmSpanCount, 0, FrameSamples - m_lookahead, packet.spans, packet.spanCount,
					  m_lookahead)) {
		result.error = Error::Capacity;
		return false;
	}
	const int bytes = opus_encode_float(m_encoder.get(), m_pcm.data(), FrameSamples, packet.payload.data(),
										static_cast< opus_int32 >(packet.payload.size()));
	if (bytes < 0) {
		result.error      = Error::Encoder;
		result.codecError = bytes;
		return false;
	}
	m_encoded    = true;
	packet.bytes = static_cast< std::size_t >(bytes);
	if (m_hasReserve) {
		if (result.count == result.packets.size()) {
			result.error = Error::Capacity;
			return false;
		}
		result.packets[result.count++] = m_reserve;
	}
	m_reserve    = packet;
	m_hasReserve = true;
	m_outputCursor += 2;
	m_delaySpanCount = 0;
	if (!copyRange(m_pcmSpans, m_pcmSpanCount, FrameSamples - m_lookahead, m_lookahead, m_delaySpans, m_delaySpanCount,
				   0)) {
		result.error = Error::Capacity;
		return false;
	}
	m_pcmCount = m_pcmSpanCount = 0;
	return true;
}

bool AnalogVoiceStream::append(const float *samples, std::size_t count, Origin origin, std::uint64_t sourceFrame,
							   std::uint32_t sourceOffset, Result &result) noexcept {
	while (count) {
		const std::size_t take = std::min(count, FrameSamples - m_pcmCount);
		if (m_pcmSpanCount == m_pcmSpans.size()) {
			result.error = Error::Capacity;
			return false;
		}
		m_pcmSpans[m_pcmSpanCount++] = { origin, sourceFrame, sourceOffset, static_cast< std::uint16_t >(m_pcmCount),
										 static_cast< std::uint16_t >(take) };
		std::copy_n(samples, take, m_pcm.data() + m_pcmCount);
		m_pcmCount += take;
		if (m_pcmCount == FrameSamples && !encode(result)) {
			return false;
		}
		samples += take;
		count -= take;
		sourceOffset += static_cast< std::uint32_t >(take);
	}
	return true;
}

void AnalogVoiceStream::fail(Error error, int codecError, const Packet &reserve, bool hadReserve,
							 Result &result) noexcept {
	result.count       = 0;
	result.disposition = Disposition::Rejected;
	result.error       = error;
	result.codecError  = codecError;
	m_state            = m_encoded ? State::Faulted : State::Bypass;
	// Results produced by the failing call are not committed. Close its prior reserve.
	if (m_state == State::Faulted && hadReserve) {
		result.packets[0]     = reserve;
		result.packets[0].end = End::Fault;
		result.count          = 1;
	}
	m_hasReserve = false;
	m_pcmCount = m_pcmSpanCount = 0;
}

bool AnalogVoiceStream::close(End end, Result &result) noexcept {
	if (m_hasSource) {
		std::array< float, FrameSamples > zeros{};
		// Only pad at EOF: account for actual encoder delay, then complete the frame.
		std::size_t drain = static_cast< std::size_t >(m_lookahead);
		drain += (FrameSamples - (m_pcmCount + drain) % FrameSamples) % FrameSamples;
		while (drain) {
			const std::size_t take = std::min(drain, FrameSamples);
			if (!append(zeros.data(), take, Origin::EndPadding, 0, 0, result)) {
				return false;
			}
			drain -= take;
		}
	}
	if (m_hasReserve) {
		if (result.count == result.packets.size()) {
			result.error = Error::Capacity;
			return false;
		}
		m_reserve.end                  = end;
		result.packets[result.count++] = m_reserve;
		m_hasReserve                   = false;
	}
	m_state = State::Ended;
	return true;
}

void AnalogVoiceStream::finish(Result &result) noexcept {
	result = Result{};
	if (m_state == State::Active) {
		const Packet reserve  = m_reserve;
		const bool hadReserve = m_hasReserve;
		if (!close(End::Technical, result)) {
			fail(result.error, result.codecError, reserve, hadReserve, result);
		}
	} else {
		result.disposition = Disposition::Ignored;
		result.error       = Error::InvalidState;
	}
}

void AnalogVoiceStream::process(const Input &input, Result &result) noexcept {
	result = Result{};
	if (m_state != State::Active) {
		result.disposition = Disposition::Ignored;
		result.error       = Error::InvalidState;
		return;
	}
	const Packet reserve  = m_reserve;
	const bool hadReserve = m_hasReserve;
	if (input.frame < m_sourceCursor
		&& !(input.bytes == 0 && input.end && m_hasSource && input.frame == m_lastSource)) {
		result.disposition = Disposition::Ignored;
		return;
	}
	if (!input.bytes) {
		if (!input.end || (input.frame != m_sourceCursor && (!m_hasSource || input.frame != m_lastSource))) {
			fail(Error::InvalidPayload, OPUS_OK, reserve, hadReserve, result);
			return;
		}
		if (!close(End::InputMarker, result)) {
			fail(result.error, result.codecError, reserve, hadReserve, result);
		}
		return;
	}
	if (!input.payload || input.bytes > MaxInputBytes) {
		fail(Error::InvalidPayload, OPUS_OK, reserve, hadReserve, result);
		return;
	}
	const int bytes = static_cast< int >(input.bytes);
	const unsigned char *frames[48]{};
	opus_int16 sizes[48]{};
	unsigned char toc = 0;
	int payloadOffset = 0;
	const int parsed  = opus_packet_parse(input.payload, bytes, &toc, frames, sizes, &payloadOffset);
	const int samples = opus_packet_get_nb_samples(input.payload, bytes, SampleRate);
	if (parsed < 0 || samples <= 0 || samples > static_cast< int >(m_decoded.size())) {
		fail(Error::InvalidPayload, parsed < 0 ? parsed : samples, reserve, hadReserve, result);
		return;
	}
	if (opus_packet_get_nb_channels(input.payload) != 1) {
		fail(Error::UnsupportedChannels, OPUS_OK, reserve, hadReserve, result);
		return;
	}
	if (samples % 480 != 0) {
		fail(Error::UnsupportedDuration, OPUS_OK, reserve, hadReserve, result);
		return;
	}
	const std::uint64_t duration = static_cast< std::uint64_t >(samples / 480);
	if (input.frame > MaxFrame - duration) {
		fail(Error::SequenceOverflow, OPUS_OK, reserve, hadReserve, result);
		return;
	}
	const std::uint64_t gap = input.frame - m_sourceCursor;
	// On a long gap, finish any half-frame with bounded PLC before skipping time.
	const std::uint64_t conceal = gap <= 4 ? gap : (m_pcmCount ? 3 : 4);
	for (std::uint64_t i = 0; i < conceal; ++i) {
		const int decoded = opus_decode_float(m_decoder.get(), nullptr, 0, m_decoded.data(), 480, 0);
		if (decoded != 480 || !m_processor.process(m_decoded.data(), 480)
			|| !append(m_decoded.data(), 480, Origin::Concealment, m_sourceCursor + i, 0, result)) {
			fail(result.error != Error::None ? result.error : (decoded != 480 ? Error::Decoder : Error::Processor),
				 decoded < 0 ? decoded : result.codecError, reserve, hadReserve, result);
			return;
		}
	}
	result.concealedFrames = conceal;
	result.skippedFrames   = gap - conceal;
	if (m_outputCursor > MaxFrame - result.skippedFrames) {
		fail(Error::SequenceOverflow, OPUS_OK, reserve, hadReserve, result);
		return;
	}
	m_outputCursor += result.skippedFrames;
	const int decoded = opus_decode_float(m_decoder.get(), input.payload, bytes, m_decoded.data(),
										  static_cast< int >(m_decoded.size()), 0);
	if (decoded != samples) {
		fail(Error::Decoder, decoded, reserve, hadReserve, result);
		return;
	}
	for (std::size_t offset = 0; offset < static_cast< std::size_t >(samples); offset += FrameSamples) {
		const std::size_t count = std::min(FrameSamples, static_cast< std::size_t >(samples) - offset);
		if (!m_processor.process(m_decoded.data() + offset, count)
			|| !append(m_decoded.data() + offset, count, Origin::Source, input.frame,
					   static_cast< std::uint32_t >(offset), result)) {
			fail(result.error == Error::None ? Error::Processor : result.error, result.codecError, reserve, hadReserve,
				 result);
			return;
		}
	}
	m_hasSource    = true;
	m_lastSource   = input.frame;
	m_sourceCursor = input.frame + duration;
	if (input.end && !close(End::InputMarker, result)) {
		fail(result.error, result.codecError, reserve, hadReserve, result);
	}
}
} // namespace funkvoice
