// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#pragma once
#include <cstddef>
#include <string>

namespace funkvoice {
constexpr std::size_t FrameSamples = 960;
constexpr int SampleRate           = 48000;

struct Parameters {
	bool enabled      = true;
	double highpassHz = 300, lowpassHz = 2800;
	double midHz = 1100, midGainDb = 5, midQ = 0.8;
	double inputGainDb = 8, thresholdDb = -24, ratio = 3, kneeDb = 6;
	double attackMs = 5, releaseMs = 90, makeupDb = 4;
	double saturationDrive = 2.2, outputGainDb = 0;
};

Parameters profile(const std::string &name);
void setParameter(Parameters &parameters, const std::string &key, double value);
void validate(const Parameters &parameters);

class VoiceProcessor {
public:
	explicit VoiceProcessor(const Parameters &parameters);
	void reset() noexcept;
	// In place, at most 20 ms. No allocation, locking, file access or lookahead.
	// Invalid buffers are untouched; non-finite samples become zero and report false.
	bool process(float *samples, std::size_t count) noexcept;
	const Parameters &parameters() const noexcept { return m_parameters; }

private:
	struct Biquad {
		double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
		double run(double sample) noexcept;
		void reset() noexcept { z1 = z2 = 0; }
	};
	static Biquad pass(double hz, bool highpass);
	static Biquad peak(double hz, double q, double gainDb);
	Parameters m_parameters;
	Biquad m_highpass, m_lowpass, m_mid, m_postLowpass;
	double m_envelope = 0, m_attack, m_release, m_inputGain, m_makeup, m_outputGain;
};
} // namespace funkvoice
