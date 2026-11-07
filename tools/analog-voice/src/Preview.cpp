// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "Preview.h"
#include "WaveIO.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>

namespace funkvoice {
namespace {
	using Block    = std::array< float, FrameSamples >;
	using Consumer = std::function< void(float *, std::size_t) >;
	// File I/O and callback construction belong to the offline adapter, never the DSP.
	int firstPass(const std::filesystem::path &input, const PreviewSettings &settings, const Consumer &consume) {
		WaveReader reader(input);
		OpusCodec codec(settings.first);
		Block source{}, decoded{}, assembled{};
		std::uint64_t remaining = reader.samples();
		std::size_t skip = codec.lookahead(), fill = 0, packets = 0;
		const double headroom = std::pow(10.0, -settings.headroomDb / 20);
		while (remaining) {
			if (++packets > (reader.samples() + FrameSamples - 1) / FrameSamples + 2)
				throw std::runtime_error("Opus input drain stalled");
			source.fill(0);
			const std::size_t n = reader.read(source.data(), source.size());
			for (std::size_t i = 0; i < n; ++i)
				source[i] *= static_cast< float >(headroom);
			codec.process(source.data(), source.size(), decoded.data());
			const std::size_t discarded = std::min(skip, FrameSamples);
			skip -= discarded;
			const std::size_t available =
				static_cast< std::size_t >(std::min< std::uint64_t >(FrameSamples - discarded, remaining));
			for (std::size_t i = 0; i < available; ++i) {
				assembled[fill++] = decoded[discarded + i];
				if (fill == FrameSamples) {
					consume(assembled.data(), fill);
					fill = 0;
				}
			}
			remaining -= available;
		}
		if (fill)
			consume(assembled.data(), fill);
		return codec.lookahead();
	}
	double matchGain(const std::filesystem::path &input, const Parameters &parameters,
					 const PreviewSettings &settings) {
		VoiceProcessor processor(parameters);
		double reference = 0, filtered = 0, peak = 0;
		firstPass(input, settings, [&](float *block, std::size_t count) {
			double before = 0;
			for (std::size_t i = 0; i < count; ++i)
				before += double(block[i]) * block[i];
			if (!processor.process(block, count))
				throw std::runtime_error("Non-finite first Opus output");
			double after = 0;
			for (std::size_t i = 0; i < count; ++i) {
				after += double(block[i]) * block[i];
				peak = std::max(peak, std::abs(double(block[i])));
			}
			// Same gate for input/output; only one fixed gain for the entire recording.
			if (before / count > 1e-5) {
				reference += before;
				filtered += after;
			}
		});
		if (!filtered || !reference || !peak)
			return 1;
		return std::min({ std::sqrt(reference / filtered), 0.85 / peak, std::pow(10.0, 12.0 / 20) });
	}
} // namespace
PreviewReport preview(const std::filesystem::path &input, const std::filesystem::path &output,
					  const Parameters *parameters, const PreviewSettings &settings) {
	if (!std::isfinite(settings.headroomDb) || settings.headroomDb < 0 || settings.headroomDb > 24)
		throw std::invalid_argument("headroom-db must be 0..24");
	validate(settings.first);
	validate(settings.second);
	WaveReader source(input);
	const double gain = parameters && settings.levelMatch ? matchGain(input, *parameters, settings) : 1;
	std::optional< VoiceProcessor > processor;
	if (parameters)
		processor.emplace(*parameters);
	OpusCodec second(settings.second);
	WaveWriter writer(output, source.samples());
	Block encoded{}, decoded{};
	std::uint64_t remaining = source.samples();
	std::size_t skip        = second.lookahead();
	const Consumer send     = [&](float *block, std::size_t count) {
        encoded.fill(0);
        for (std::size_t i = 0; i < count; ++i)
            encoded[i] = static_cast< float >(block[i] * gain);
        second.process(encoded.data(), encoded.size(), decoded.data());
        const std::size_t discarded = std::min(skip, FrameSamples);
        skip -= discarded;
        const std::size_t take =
            static_cast< std::size_t >(std::min< std::uint64_t >(FrameSamples - discarded, remaining));
        writer.write(decoded.data() + discarded, take);
        remaining -= take;
	};
	const int firstLookahead = firstPass(input, settings, [&](float *block, std::size_t count) {
		if (processor && !processor->process(block, count))
			throw std::runtime_error("Invalid PCM from first Opus decoder");
		send(block, count);
	});
	Block zero{};
	for (int flush = 0; remaining && flush < 2; ++flush)
		send(zero.data(), zero.size());
	if (remaining)
		throw std::runtime_error("Second Opus drain stalled");
	writer.finish();
	return { source.samples(), writer.clipped(), 20 * std::log10(gain),
			 writer.peak(),    firstLookahead,   second.lookahead() };
}
} // namespace funkvoice
