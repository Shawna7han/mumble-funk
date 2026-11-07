#include "OpusCodec.h"
#include "Preview.h"
#include "WaveIO.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace funkvoice;
using Block = std::array< float, FrameSamples >;
void check(bool condition, const char *message) {
	if (!condition)
		throw std::runtime_error(message);
}
template< class F > void refuses(F function) {
	bool error = false;
	try {
		function();
	} catch (const std::exception &) {
		error = true;
	}
	check(error, "Expected codec failure was not reported");
}
// Deliberately independent whole-vector reference, used only by this test.
std::vector< float > referenceRoundTrip(const std::vector< float > &input) {
	OpusSettings settings;
	OpusCodec codec(settings);
	std::vector< float > decoded;
	for (std::size_t offset = 0; offset < input.size() + FrameSamples; offset += FrameSamples) {
		Block source{}, output{};
		for (std::size_t i = 0; i < FrameSamples && offset + i < input.size(); ++i)
			source[i] = input[offset + i];
		codec.process(source.data(), source.size(), output.data());
		decoded.insert(decoded.end(), output.begin(), output.end());
	}
	return std::vector< float >(decoded.begin() + codec.lookahead(),
								decoded.begin() + codec.lookahead() + input.size());
}
void testLength(const std::filesystem::path &directory, std::size_t samples) {
	const std::filesystem::path input = directory / "input.wav", output = directory / "output.wav";
	std::vector< float > expected(samples);
	std::uint32_t random = 93;
	for (float &x : expected) {
		random = random * 1664525u + 1013904223u;
		x      = static_cast< float >((int(random >> 16) - 32768) / 131072.0);
	}
	WaveWriter writer(input, samples);
	for (std::size_t offset = 0; offset < samples; offset += FrameSamples)
		writer.write(expected.data() + offset, std::min(FrameSamples, samples - offset));
	writer.finish();
	// Quantize the input exactly as the public adapter receives it.
	WaveReader reader(input);
	for (std::size_t offset = 0; offset < samples; offset += FrameSamples)
		reader.read(expected.data() + offset, std::min(FrameSamples, samples - offset));
	expected = referenceRoundTrip(referenceRoundTrip(expected));
	PreviewSettings settings;
	settings.headroomDb        = 0;
	settings.levelMatch        = false;
	const PreviewReport report = preview(input, output, nullptr, settings);
	check(report.samples == samples && report.firstLookahead > 0 && report.secondLookahead > 0,
		  "Wrong sample count or lookahead");
	WaveReader result(output);
	check(result.samples() == samples, "Wrong WAV length");
	Block actual{};
	for (std::size_t offset = 0; offset < samples; offset += FrameSamples) {
		const std::size_t count = result.read(actual.data(), std::min(FrameSamples, samples - offset));
		for (std::size_t i = 0; i < count; ++i) {
			const double scaled   = std::clamp(double(expected[offset + i]), -1.0, 32767.0 / 32768);
			const float quantized = static_cast< float >(std::lround(scaled * 32768) / 32768.0);
			check(actual[i] == quantized, "Streaming start/EOF differs from independent two-stage reference");
		}
	}
	std::filesystem::remove(input);
	std::filesystem::remove(output);
}
void tests(const std::filesystem::path &directory) {
	for (const std::size_t n : { 1u, 17u, 959u, 960u, 961u, 1921u, 24017u })
		testLength(directory, n);
	OpusSettings settings;
	OpusCodec codec(settings);
	Block source{}, first{}, reset{}, fresh{};
	for (std::size_t i = 0; i < source.size(); ++i)
		source[i] = static_cast< float >(0.1 * std::sin(i * 0.17));
	codec.process(source.data(), source.size(), first.data());
	codec.reset();
	codec.process(source.data(), source.size(), reset.data());
	OpusCodec independent(settings);
	independent.process(source.data(), source.size(), fresh.data());
	check(first == reset && reset == fresh, "Codec state reset or independence failed");
	refuses([&] { codec.process(source.data(), 959, first.data()); });
	refuses([&] { codec.process(nullptr, 960, first.data()); });
	const unsigned char bad[] = { 3 };
	refuses([&] { codec.decode(bad, 1, first.data()); });
	// Declares eight 2.5ms frames but omits the required VBR size table.
	const unsigned char malformedVbr[] = { 0xe3, 0x88 };
	check(opus_packet_get_nb_samples(malformedVbr, 2, SampleRate) == 960, "Corrupt packet fixture duration");
	refuses([&] { codec.decode(malformedVbr, 2, first.data()); });
	refuses([&] { codec.decode(nullptr, 1, first.data()); });
	refuses([&] { codec.decode(bad, 1276, first.data()); });
	settings.bitrate = 0;
	refuses([&] { OpusCodec invalid(settings); });
	settings.bitrate    = 40000;
	settings.complexity = 11;
	refuses([&] { OpusCodec invalid(settings); });
}
} // namespace
int main() {
	// Test output stays outside source and private recordings; exclusive new directory.
	const std::filesystem::path base = std::filesystem::temp_directory_path();
	std::filesystem::path directory;
	for (unsigned i = 0; i < 1000; ++i) {
		const std::filesystem::path p = base / ("funk-voice-codec-" + std::to_string(i));
		if (std::filesystem::create_directory(p)) {
			directory = p;
			break;
		}
	}
	if (directory.empty())
		return 1;
	int status = 0;
	try {
		tests(directory);
		std::cout << "PASS: exact two-stage streaming reference, short/partial EOF, lookahead, reset, invalid "
					 "packets/settings\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		status = 1;
	}
	std::filesystem::remove_all(directory);
	return status;
}
