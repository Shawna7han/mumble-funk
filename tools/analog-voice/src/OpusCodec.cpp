// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "OpusCodec.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace funkvoice {
namespace {
	void checked(int status) {
		if (status != OPUS_OK)
			throw std::runtime_error(std::string("Opus: ") + opus_strerror(status));
	}
} // namespace
void validate(const OpusSettings &s) {
	if (s.bitrate < 6000 || s.bitrate > 128000 || s.complexity < 0 || s.complexity > 10
		|| (s.bandwidth != OPUS_AUTO && s.bandwidth != OPUS_BANDWIDTH_NARROWBAND
			&& s.bandwidth != OPUS_BANDWIDTH_WIDEBAND && s.bandwidth != OPUS_BANDWIDTH_FULLBAND))
		throw std::invalid_argument("Invalid Opus bitrate, complexity or bandwidth");
}
OpusCodec::OpusCodec(const OpusSettings &s) {
	validate(s);
	int status = OPUS_OK;
	m_encoder.reset(opus_encoder_create(SampleRate, 1, OPUS_APPLICATION_VOIP, &status));
	checked(status);
	if (!m_encoder)
		throw std::runtime_error("Opus encoder allocation failed");
	m_decoder.reset(opus_decoder_create(SampleRate, 1, &status));
	checked(status);
	if (!m_decoder)
		throw std::runtime_error("Opus decoder allocation failed");
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_BITRATE(s.bitrate)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_COMPLEXITY(s.complexity)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_VBR(s.vbr ? 1 : 0)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_BANDWIDTH(s.bandwidth)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_DTX(0)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_INBAND_FEC(0)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_SET_PACKET_LOSS_PERC(0)));
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_GET_LOOKAHEAD(&m_lookahead)));
	if (m_lookahead < 0 || m_lookahead > static_cast< int >(FrameSamples))
		throw std::runtime_error("Unsupported Opus lookahead for 20ms adapter");
}
int OpusCodec::decode(const unsigned char *packet, int bytes, float *output) {
	if (!packet || !output || bytes < 1 || bytes > static_cast< int >(m_packet.size()))
		throw std::invalid_argument("Invalid Opus packet buffer");
	if (opus_packet_get_nb_samples(packet, bytes, SampleRate) != static_cast< int >(FrameSamples))
		throw std::runtime_error("Opus packet is not one 20ms frame");
	const int count = opus_decode_float(m_decoder.get(), packet, bytes, output, FrameSamples, 0);
	if (count < 0)
		checked(count);
	if (count != static_cast< int >(FrameSamples))
		throw std::runtime_error("Unexpected Opus frame length");
	return count;
}
int OpusCodec::process(const float *input, std::size_t samples, float *output) {
	if (!input || !output || samples != FrameSamples)
		throw std::invalid_argument("Opus requires 960 samples");
	for (std::size_t i = 0; i < samples; ++i)
		if (!std::isfinite(input[i]) || std::abs(input[i]) > 8)
			throw std::invalid_argument("Invalid Opus PCM sample");
	const int bytes = opus_encode_float(m_encoder.get(), input, FrameSamples, m_packet.data(), m_packet.size());
	if (bytes < 0)
		checked(bytes);
	decode(m_packet.data(), bytes, output);
	return bytes;
}
void OpusCodec::reset() {
	checked(opus_encoder_ctl(m_encoder.get(), OPUS_RESET_STATE));
	checked(opus_decoder_ctl(m_decoder.get(), OPUS_RESET_STATE));
}
std::size_t OpusCodec::stateBytes() {
	return static_cast< std::size_t >(opus_encoder_get_size(1)) + opus_decoder_get_size(1);
}
} // namespace funkvoice
