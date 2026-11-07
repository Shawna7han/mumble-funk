// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "VoiceProcessor.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace funkvoice {
namespace {
	constexpr double Pi = 3.14159265358979323846;
	double gain(double db) {
		return std::pow(10.0, db / 20.0);
	}
	void range(double value, double low, double high, const char *name) {
		if (!std::isfinite(value) || value < low || value > high)
			throw std::invalid_argument(std::string("Invalid parameter: ") + name);
	}
} // namespace

Parameters profile(const std::string &name) {
	Parameters p;
	if (name == "cb-am")
		return p;
	if (name == "business-fm") {
		p.lowpassHz       = 3000;
		p.midHz           = 1600;
		p.midGainDb       = 2;
		p.inputGainDb     = 6;
		p.thresholdDb     = -22;
		p.ratio           = 2.3;
		p.attackMs        = 4;
		p.releaseMs       = 70;
		p.makeupDb        = 3;
		p.saturationDrive = 0.6;
		return p;
	}
	if (name == "narrowband") {
		p.highpassHz      = 400;
		p.lowpassHz       = 2500;
		p.midHz           = 1350;
		p.midGainDb       = 7;
		p.midQ            = 1.1;
		p.inputGainDb     = 10;
		p.thresholdDb     = -28;
		p.ratio           = 4;
		p.attackMs        = 2;
		p.releaseMs       = 110;
		p.makeupDb        = 7;
		p.saturationDrive = 3.2;
		return p;
	}
	throw std::invalid_argument("Unknown profile: " + name);
}

void setParameter(Parameters &p, const std::string &key, double value) {
	struct Field {
		const char *name;
		double Parameters::*member;
	};
	static const Field fields[] = { { "highpass_hz", &Parameters::highpassHz },
									{ "lowpass_hz", &Parameters::lowpassHz },
									{ "mid_hz", &Parameters::midHz },
									{ "mid_gain_db", &Parameters::midGainDb },
									{ "mid_q", &Parameters::midQ },
									{ "input_gain_db", &Parameters::inputGainDb },
									{ "threshold_db", &Parameters::thresholdDb },
									{ "ratio", &Parameters::ratio },
									{ "knee_db", &Parameters::kneeDb },
									{ "attack_ms", &Parameters::attackMs },
									{ "release_ms", &Parameters::releaseMs },
									{ "makeup_db", &Parameters::makeupDb },
									{ "saturation_drive", &Parameters::saturationDrive },
									{ "output_gain_db", &Parameters::outputGainDb } };
	for (const Field &field : fields) {
		if (key == field.name) {
			p.*(field.member) = value;
			return;
		}
	}
	throw std::invalid_argument("Unknown filter parameter: " + key);
}

void validate(const Parameters &p) {
	range(p.highpassHz, 50, 5000, "highpass_hz");
	range(p.lowpassHz, 500, 16000, "lowpass_hz");
	if (p.highpassHz >= p.lowpassHz)
		throw std::invalid_argument("highpass_hz must be below lowpass_hz");
	range(p.midHz, p.highpassHz, p.lowpassHz, "mid_hz");
	range(p.midGainDb, -12, 12, "mid_gain_db");
	range(p.midQ, 0.2, 5, "mid_q");
	range(p.inputGainDb, -24, 18, "input_gain_db");
	range(p.thresholdDb, -60, 0, "threshold_db");
	range(p.ratio, 1, 10, "ratio");
	range(p.kneeDb, 0, 24, "knee_db");
	range(p.attackMs, 0.1, 100, "attack_ms");
	range(p.releaseMs, 5, 2000, "release_ms");
	range(p.makeupDb, -12, 18, "makeup_db");
	range(p.saturationDrive, 0, 10, "saturation_drive");
	range(p.outputGainDb, -24, 12, "output_gain_db");
}

double VoiceProcessor::Biquad::run(double x) noexcept {
	const double y = b0 * x + z1;
	z1             = b1 * x - a1 * y + z2;
	z2             = b2 * x - a2 * y;
	// Flush inaudible tails before subnormal arithmetic can slow a silent stream.
	if (std::abs(z1) < 1e-24)
		z1 = 0;
	if (std::abs(z2) < 1e-24)
		z2 = 0;
	return y;
}

VoiceProcessor::Biquad VoiceProcessor::pass(double hz, bool highpass) {
	// Bilinear-transform biquad equations, Butterworth Q=1/sqrt(2).
	// Coefficients use denominator 1+a1*z^-1+a2*z^-2 (DF-II transposed below).
	const double w = 2 * Pi * hz / SampleRate, c = std::cos(w), alpha = std::sin(w) / std::sqrt(2.0);
	const double a0 = 1 + alpha;
	Biquad f;
	f.b0 = (highpass ? 1 + c : 1 - c) / (2 * a0);
	f.b1 = (highpass ? -(1 + c) : 1 - c) / a0;
	f.b2 = f.b0;
	f.a1 = -2 * c / a0;
	f.a2 = (1 - alpha) / a0;
	return f;
}

VoiceProcessor::Biquad VoiceProcessor::peak(double hz, double q, double gainDb) {
	// Parametric peaking EQ, A=10^(gain_dB/40), alpha=sin(w)/(2Q).
	// Independently implemented from the mathematical audio-EQ biquad equations.
	const double w = 2 * Pi * hz / SampleRate, a = std::pow(10.0, gainDb / 40), alpha = std::sin(w) / (2 * q);
	const double a0 = 1 + alpha / a;
	Biquad f;
	f.b0 = (1 + alpha * a) / a0;
	f.b1 = -2 * std::cos(w) / a0;
	f.b2 = (1 - alpha * a) / a0;
	f.a1 = f.b1;
	f.a2 = (1 - alpha / a) / a0;
	return f;
}

VoiceProcessor::VoiceProcessor(const Parameters &p) : m_parameters(p) {
	validate(p);
	m_highpass    = pass(p.highpassHz, true);
	m_lowpass     = pass(p.lowpassHz, false);
	m_mid         = peak(p.midHz, p.midQ, p.midGainDb);
	m_postLowpass = pass(p.lowpassHz, false);
	m_attack      = 1 - std::exp(-1 / (0.001 * p.attackMs * SampleRate));
	m_release     = 1 - std::exp(-1 / (0.001 * p.releaseMs * SampleRate));
	m_inputGain   = gain(p.inputGainDb);
	m_makeup      = gain(p.makeupDb);
	m_outputGain  = gain(p.outputGainDb);
}

void VoiceProcessor::reset() noexcept {
	m_highpass.reset();
	m_lowpass.reset();
	m_mid.reset();
	m_postLowpass.reset();
	m_envelope = 0;
}

bool VoiceProcessor::process(float *samples, std::size_t count) noexcept {
	if (count > FrameSamples || (count && !samples))
		return false;
	bool valid = true;
	for (std::size_t i = 0; i < count; ++i) {
		if (!std::isfinite(samples[i])) {
			samples[i] = 0;
			valid      = false;
		}
		if (!m_parameters.enabled)
			continue;
		// Opus float decoding may overshoot nominal full scale; do not hard-clip it.
		double x           = std::clamp(static_cast< double >(samples[i]), -8.0, 8.0);
		x                  = m_mid.run(m_lowpass.run(m_highpass.run(x))) * m_inputGain;
		const double power = x * x;
		m_envelope += (power - m_envelope) * (power > m_envelope ? m_attack : m_release);
		if (m_envelope < 1e-24)
			m_envelope = 0;
		const double above = 10 * std::log10(std::max(m_envelope, 1e-20)) - m_parameters.thresholdDb;
		double reductionDb = 0;
		const double knee  = m_parameters.kneeDb;
		if (above > knee / 2)
			reductionDb = (1 / m_parameters.ratio - 1) * above;
		else if (knee > 0 && above > -knee / 2)
			reductionDb = (1 / m_parameters.ratio - 1) * (above + knee / 2) * (above + knee / 2) / (2 * knee);
		x *= gain(reductionDb) * m_makeup;
		const double drive = m_parameters.saturationDrive;
		if (drive > 0)
			x = std::tanh(drive * x) / drive;
		x = m_postLowpass.run(x) * m_outputGain;
		// Transparent below 0.80 full scale, smooth asymptotic safety ceiling 0.98.
		if (std::abs(x) > 0.8)
			x = std::copysign(0.8 + 0.18 * std::tanh((std::abs(x) - 0.8) / 0.18), x);
		samples[i] = static_cast< float >(x);
	}
	return valid;
}
} // namespace funkvoice
