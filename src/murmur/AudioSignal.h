// Copyright The Mumble-Funk Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license in the LICENSE file.
#ifndef MUMBLE_MURMUR_AUDIOSIGNAL_H_
#define MUMBLE_MURMUR_AUDIOSIGNAL_H_
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <vector>
// Small, bounded signal assets. Decode/resample/encode only at startup or CLI validation.
// Future signals share this loader/encoder and supply their own fallback waveform.
class AudioSignal {
public:
	enum class LengthPolicy { Busy, ShortSignal, RadioNoise };
	// RadioNoise: exact 48-kHz PCM, 20 ms..60 s, <=8 MiB; no resampling or tail.
	// Busy accepts 20..1250 ms, with the existing 128-KiB file bound. Optional short cues accept >=1 ms;
	// both paths retain all other bounds, the lookahead tail and attenuation.
	static std::vector< float > loadWav(const QString &path, LengthPolicy policy = LengthPolicy::Busy);
	static std::vector< float > syntheticBusy();
	// Short signal files use the actual Opus lookahead, rather than a full 20-ms tail.
	static std::vector< QByteArray > encodeWavSignal(const QString &path, int volume, int boostDb = 0);
	// Startup-only level preparation for sequential effects on ONE worker encoder.
	// Removes the loader's flush tail; the worker flushes only at the final end.
	static std::vector<float> streamWavSignal(const QString &path, int volume, int boostDb = 0);
	static std::vector< QByteArray > encode(const std::vector< float > &pcm, int volume, int boostDb = 0);
};
#endif
