// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#include "AudioSignal.h"
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <opus.h>
#include <stdexcept>

std::vector< float > AudioSignal::loadWav(const QString &path, LengthPolicy policy) {
	const bool noise = policy == LengthPolicy::RadioNoise;
	const int fileLimit = noise ? 8 * 1024 * 1024 : 131072;
	const auto fail = [policy, noise] {
		if (noise) throw std::runtime_error("Expected radio noise PCM WAV: mono, 16-bit LE, 48000 Hz, 20 ms..60 s, <=8 MiB");
		throw std::runtime_error(
			policy == LengthPolicy::Busy
				? "Expected PCM WAV: mono, signed 16-bit LE, 8000/16000/24000/48000 Hz, 20-1250 ms, at most 128 KiB"
				: "Expected PCM WAV: mono, signed 16-bit LE, 8000/16000/24000/48000 Hz, 1-1000 ms, at most 128 KiB");
	};
	QFile file(path);
	if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > fileLimit)
		fail();
	const QByteArray bytes = file.read(fileLimit + 1);
	if (bytes.size() < 44 || bytes.size() > fileLimit || bytes.mid(0, 4) != "RIFF" || bytes.mid(8, 4) != "WAVE")
		fail();
	auto u32 = [&](int pos) { return qFromLittleEndian< quint32 >(bytes.constData() + pos); };
	auto u16 = [&](int pos) { return qFromLittleEndian< quint16 >(bytes.constData() + pos); };
	if (u32(4) != static_cast< quint32 >(bytes.size() - 8))
		fail();
	int fmt = -1, data = -1;
	quint32 size = 0;
	for (int pos = 12; pos < bytes.size();) {
		if (bytes.size() - pos < 8)
			fail();
		const auto length = u32(pos + 4);
		if (length > static_cast< quint32 >(bytes.size() - pos - 8))
			fail();
		const auto name = bytes.mid(pos, 4);
		if (name == "fmt ") {
			if (fmt >= 0 || length != 16)
				fail();
			fmt = pos + 8;
		}
		if (name == "data") {
			if (data >= 0)
				fail();
			data = pos + 8;
			size = length;
		}
		const auto step = 8 + length + (length & 1);
		if (step > static_cast< quint32 >(bytes.size() - pos))
			fail();
		pos += static_cast< int >(step);
	}
	if (fmt < 0 || data < 0 || u16(fmt) != 1 || u16(fmt + 2) != 1 || u16(fmt + 14) != 16 || u16(fmt + 12) != 2
		|| size % 2)
		fail();
	const auto rate = u32(fmt + 4);
	if ((rate != 8000 && rate != 16000 && rate != 24000 && rate != 48000) || u32(fmt + 8) != rate * 2)
		fail();
	if (noise && rate != 48000) fail();
	const auto maxFrames = noise ? rate * 60 : (policy == LengthPolicy::Busy ? rate * 5 / 4 : rate);
	if (size < rate / (policy == LengthPolicy::ShortSignal ? 1000 : 50) * 2 || size > maxFrames * 2)
		fail();
	const auto frames = size / 2;
	const auto output = static_cast< std::size_t >(frames) * 48000 / rate;
	std::vector< float > pcm(output + (noise ? 0 : 960), 0); // Loops must not acquire a silent tail.
	for (std::size_t i = 0; i < output; ++i) {
		const double source = static_cast< double >(i) * rate / 48000.0;
		const auto first    = static_cast< quint32 >(source);
		const auto second   = std::min(first + 1, frames - 1);
		auto sample         = [&](quint32 index) {
            return static_cast< float >(
                       qFromLittleEndian< qint16 >(bytes.constData() + data + static_cast< int >(index * 2)))
                   / 32768.0f;
		};
		const float fraction = static_cast< float >(source - first);
		pcm[i]               = sample(first) * (1 - fraction) + sample(second) * fraction;
	}
	return pcm;
}

std::vector< float > AudioSignal::syntheticBusy() {
	// Original generic sine waveform; no samples, recordings or device-specific cadence.
	// At 48000 Hz: x[n] = 0.2 * envelope[n] * sin(2*pi*600*n/48000).
	// Each 4800-sample burst has a 240-sample (5 ms) linear attack/release;
	// the gap and tail are zero-valued samples. This code has no audio-asset dependency.
	std::vector< float > pcm(12480, 0); // 100 ms tone, 40 ms gap, 100 ms tone, 20 ms tail.
	for (std::size_t sample = 0; sample < pcm.size(); ++sample) {
		const int at    = static_cast< int >(sample);
		const int local = at < 4800 ? at : at - 6720;
		if ((at < 4800 || at >= 6720) && local >= 0 && local < 4800) {
			const double ramp = std::min(1.0, std::min(local, 4799 - local) / 240.0);
			pcm[sample]       = static_cast< float >(0.2 * ramp * std::sin(2.0 * 3.141592653589793 * 600 * at / 48000));
		}
	}
	return pcm;
}

static std::vector< QByteArray > encodePCM(const std::vector< float > &pcm, int volume, bool signalTail, int boostDb, float *preparedGain = nullptr) {
	if (boostDb < 0 || boostDb > 12)
		throw std::runtime_error("Feedback boost must be an integer from 0 to 12 dB");
	// Busy: at most 60000 source samples (1250 ms at 48 kHz) + 960 tail.
	// Short cues retain 48000 + 960 (1000 ms); final Opus frame pads with zeros.
	const std::size_t maxSamples = signalTail ? 48960 : 60960;
	if (pcm.empty() || pcm.size() > maxSamples)
		throw std::runtime_error("Feedback PCM length outside limits");
	int error = OPUS_OK;
	std::unique_ptr< OpusEncoder, decltype(&opus_encoder_destroy) > encoder(
		opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &error), opus_encoder_destroy);
	if (!encoder || error != OPUS_OK || opus_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(24000)) != OPUS_OK)
		throw std::runtime_error("Cannot initialize feedback Opus encoder");
	std::size_t length = pcm.size();
	if (signalTail) {
		int lookahead = 0;
		if (pcm.size() < 960 || opus_encoder_ctl(encoder.get(), OPUS_GET_LOOKAHEAD(&lookahead)) != OPUS_OK
			|| lookahead < 0 || lookahead > 960)
			throw std::runtime_error("Unexpected Opus lookahead");
		length = pcm.size() - 960 + static_cast<std::size_t>(lookahead);
	}
	float peak = 0;
	for (float sample : pcm) {
		if (!std::isfinite(sample) || std::abs(sample) > 1)
			throw std::runtime_error("Feedback PCM level outside limits");
		peak = std::max(peak, std::abs(sample));
	}
	// Only attenuate: cap the PCM peak at 0.2 (-14 dBFS), then apply 0..100% volume.
	const float legacyGain =
		(peak > 0.2f ? 0.2f / peak : 1.0f) * static_cast< float >(std::max(0, std::min(100, volume))) / 100;
	// Preserve the exact legacy arithmetic at 0 dB. Additional amplitude factor is
	// 10^(dB/20), bounded by 10^(12/20). Thus peak <= 0.796215 before Opus.
	// No normalization upward, clipping, duration changes or speech processing.
	float gain = boostDb == 0 ? legacyGain : legacyGain * static_cast<float>(std::pow(10.0, boostDb / 20.0));
	// Opus can overshoot its input peak (verified with a full-scale square WAV).
	// Bounded offline verification, never a real-time limiter: uniformly lower
	// the whole clip only if its decoded peak exceeds 0.98. Re-encode from reset
	// state, aiming for 0.95 to leave margin for codec quantization. Never clip
	// samples or change packet counts. Legacy 0 dB bypasses this new path exactly.
	for (int attempt = 0; attempt < 8; ++attempt) {
		if (attempt && opus_encoder_ctl(encoder.get(), OPUS_RESET_STATE) != OPUS_OK)
			throw std::runtime_error("Cannot reset feedback Opus encoder");
		std::vector<QByteArray> result;
		for (std::size_t offset = 0; offset < length; offset += 960) {
			std::array<float, 960> frame{};
			for (std::size_t i = 0; i < 960 && offset + i < pcm.size(); ++i)
				frame[i] = pcm[offset + i] * gain;
			std::array<unsigned char, 512> bytes{};
			const int size = opus_encode_float(encoder.get(), frame.data(), 960, bytes.data(),
				static_cast<opus_int32>(bytes.size()));
			if (size < 0) throw std::runtime_error("Cannot encode feedback Opus audio");
			result.emplace_back(reinterpret_cast<const char *>(bytes.data()), size);
		}
		if (boostDb == 0) { if (preparedGain) *preparedGain = gain; return result; }
		std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)> decoder(
			opus_decoder_create(48000, 1, &error), opus_decoder_destroy);
		if (!decoder || error != OPUS_OK) throw std::runtime_error("Cannot verify feedback Opus audio");
		float decodedPeak = 0;
		for (const auto &packet : result) {
			std::array<float, 960> decoded{};
			if (opus_decode_float(decoder.get(), reinterpret_cast<const unsigned char *>(packet.constData()),
				packet.size(), decoded.data(), 960, 0) != 960)
				throw std::runtime_error("Cannot decode feedback Opus audio");
			for (float sample : decoded) {
				if (!std::isfinite(sample)) throw std::runtime_error("Non-finite decoded feedback audio");
				decodedPeak = std::max(decodedPeak, std::abs(sample));
			}
		}
		if (decodedPeak <= 0.98f) { if (preparedGain) *preparedGain = gain; return result; }
		gain *= 0.95f / decodedPeak;
	}
	throw std::runtime_error("Cannot encode feedback within safe decoded level");
}

std::vector<QByteArray> AudioSignal::encode(const std::vector<float> &pcm, int volume, int boostDb) {
	return encodePCM(pcm, volume, false, boostDb); // Busy waveform and tail are unchanged.
}
std::vector<QByteArray> AudioSignal::encodeWavSignal(const QString &path, int volume, int boostDb) {
	return encodePCM(loadWav(path, LengthPolicy::ShortSignal), volume, true, boostDb);
}
std::vector<float> AudioSignal::streamWavSignal(const QString &path, int volume, int boostDb) {
	auto pcm = loadWav(path, LengthPolicy::ShortSignal);
	float gain = 0;
	encodePCM(pcm, volume, true, boostDb, &gain); // Same bounded startup level verification.
	pcm.resize(pcm.size() - 960);
	for (auto &sample : pcm) sample *= gain;
	return pcm;
}
