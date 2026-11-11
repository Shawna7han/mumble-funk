// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#pragma once
#include "VoiceProcessor.h"
#include <array>
#include <memory>
#include <opus.h>

namespace funkvoice {
struct OpusSettings {
	int bitrate    = 40000;
	int complexity = 10;
	bool vbr       = true;
	int bandwidth  = OPUS_AUTO;
};
void validate(const OpusSettings &settings);
class OpusCodec {
public:
	explicit OpusCodec(const OpusSettings &settings);
	int process(const float *input, std::size_t samples, float *output);
	int decode(const unsigned char *packet, int bytes, float *output);
	void reset();
	int lookahead() const noexcept { return m_lookahead; }
	static std::size_t stateBytes();

private:
	std::unique_ptr< OpusEncoder, decltype(&opus_encoder_destroy) > m_encoder{ nullptr, opus_encoder_destroy };
	std::unique_ptr< OpusDecoder, decltype(&opus_decoder_destroy) > m_decoder{ nullptr, opus_decoder_destroy };
	std::array< unsigned char, 1275 > m_packet{};
	int m_lookahead = 0;
};
} // namespace funkvoice
