// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#pragma once

#include "VoiceProcessor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <opus.h>

namespace funkvoice {

// One instance per stream, called serially. Construction alone allocates codecs.
class AnalogVoiceStream {
public:
	static constexpr std::size_t MaxInputBytes    = 1024;
	static constexpr std::size_t MaxPacketBytes   = 1275;
	static constexpr std::size_t MaxOutputPackets = 10;
	static constexpr std::size_t MaxSpans         = 8;
	enum class State { Idle, Active, Ended, Bypass, Faulted };
	enum class Error {
		None,
		InvalidState,
		InvalidPayload,
		UnsupportedChannels,
		UnsupportedDuration,
		SequenceOverflow,
		Decoder,
		Processor,
		Encoder,
		Capacity,
		Aborted
	};
	enum class Disposition { Accepted, Ignored, Rejected };
	enum class End { None, InputMarker, Technical, Fault };
	enum class Origin { Source, Concealment, EncoderDelay, EndPadding };

	// Nominal decoded timeline, including the new encoder's lookahead.
	// Codec/filter memory is not bounded by these spans: reset at routing epochs.
	// Source offsets refer to the decoded input packet; no client pre-skip is known.
	struct Span {
		Origin origin              = Origin::Source;
		std::uint64_t sourceFrame  = 0;
		std::uint32_t sourceOffset = 0;
		std::uint16_t outputOffset = 0;
		std::uint16_t samples      = 0;
	};
	struct Packet {
		std::array< unsigned char, MaxPacketBytes > payload{};
		std::size_t bytes   = 0;
		std::uint64_t frame = 0;
		End end             = End::None;
		std::array< Span, MaxSpans > spans{};
		std::size_t spanCount = 0;
	};
	struct Input {
		const unsigned char *payload = nullptr;
		std::size_t bytes            = 0;
		std::uint64_t frame          = 0;
		bool end                     = false;
	};
	// Caller-owned bounded batch. Consume before the next call; no internal queue.
	struct Result {
		Disposition disposition       = Disposition::Accepted;
		Error error                   = Error::None;
		int codecError                = OPUS_OK;
		std::uint64_t concealedFrames = 0;
		std::uint64_t skippedFrames   = 0;
		std::array< Packet, MaxOutputPackets > packets{};
		std::size_t count = 0;
	};

	explicit AnalogVoiceStream(const Parameters &parameters);
	// Only between transmissions. Preserve the output clock across starts/aborts.
	// A later codec pool can also supply the connection's persisted output floor.
	bool begin(std::uint64_t sourceFrame, std::uint64_t outputFrameFloor = 0) noexcept;
	void reset() noexcept;
	void process(const Input &input, Result &result) noexcept;
	// Controller may request a technical close; it is not a physical PTT release.
	void finish(Result &result) noexcept;
	// External overload/expiry: return only an existing reserve as Fault, once.
	// No decode, DSP, encode, padding or confirmed source end.
	void abort(Result &result) noexcept;
	State state() const noexcept { return m_state; }
	std::uint64_t nextSourceFrame() const noexcept { return m_sourceCursor; }
	std::uint64_t nextOutputFrame() const noexcept { return m_outputCursor; }
	int lookahead() const noexcept { return m_lookahead; }

private:
	struct EncoderDeleter {
		void operator()(OpusEncoder *p) const noexcept { opus_encoder_destroy(p); }
	};
	struct DecoderDeleter {
		void operator()(OpusDecoder *p) const noexcept { opus_decoder_destroy(p); }
	};
	using Spans = std::array< Span, MaxSpans >;
	bool append(const float *samples, std::size_t count, Origin origin, std::uint64_t sourceFrame,
				std::uint32_t sourceOffset, Result &result) noexcept;
	bool encode(Result &result) noexcept;
	bool close(End end, Result &result) noexcept;
	void fail(Error error, int codecError, const Packet &reserve, bool hadReserve, Result &result) noexcept;
	static bool copyRange(const Spans &from, std::size_t fromCount, std::size_t offset, std::size_t count, Spans &to,
						  std::size_t &toCount, std::size_t destination) noexcept;
	VoiceProcessor m_processor;
	std::unique_ptr< OpusEncoder, EncoderDeleter > m_encoder;
	std::unique_ptr< OpusDecoder, DecoderDeleter > m_decoder;
	std::array< float, 5760 > m_decoded{};
	std::array< float, FrameSamples > m_pcm{};
	Spans m_pcmSpans{}, m_delaySpans{};
	std::size_t m_pcmCount = 0, m_pcmSpanCount = 0, m_delaySpanCount = 0;
	Packet m_reserve{};
	bool m_hasReserve = false, m_encoded = false, m_hasSource = false;
	std::uint64_t m_sourceCursor = 0, m_outputCursor = 0, m_lastSource = 0;
	int m_lookahead = 0;
	State m_state   = State::Idle;
};
} // namespace funkvoice
