#include "VoiceProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

static std::size_t allocations = 0;
void *operator new(std::size_t n) {
	++allocations;
	if (void *p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void *p) noexcept {
	std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
	std::free(p);
}

namespace {
using namespace funkvoice;
constexpr double Pi = 3.14159265358979323846;
void check(bool condition, const char *why) {
	if (!condition)
		throw std::runtime_error(why);
}
double tone(Parameters p, double hz, double amplitude) {
	VoiceProcessor processor(p);
	std::array< float, FrameSamples > block{};
	double energy = 0;
	for (int frame = 0; frame < 100; ++frame) {
		for (std::size_t i = 0; i < block.size(); ++i)
			block[i] =
				static_cast< float >(amplitude * std::sin(2 * Pi * hz * (frame * FrameSamples + i) / SampleRate));
		check(processor.process(block.data(), block.size()), "tone process failed");
		if (frame >= 50)
			for (float x : block)
				energy += x * x;
	}
	return std::sqrt(energy / (50 * FrameSamples));
}
void tests() {
	for (const char *name : { "cb-am", "business-fm", "narrowband" }) {
		Parameters p = profile(name);
		VoiceProcessor whole(p), split(p), independent(p);
		std::array< float, FrameSamples > a{}, b{}, untouched{};
		for (int f = 0; f < 70; ++f) {
			for (std::size_t i = 0; i < a.size(); ++i)
				a[i] = static_cast< float >(0.7 * std::sin((f * FrameSamples + i) * 0.17));
			b                        = a;
			const std::size_t before = allocations;
			check(whole.process(a.data(), a.size()), "20ms block rejected");
			check(allocations == before, "DSP allocated memory");
			for (std::size_t offset = 0; offset < b.size(); offset += 32)
				check(split.process(b.data() + offset, 32), "small block rejected");
			check(a == b, "block boundaries changed samples or states");
			for (float x : a)
				check(std::isfinite(x) && std::abs(x) <= 0.980001, "unsafe peak");
		}
		independent.process(untouched.data(), untouched.size());
		for (float x : untouched)
			check(x == 0, "instances share history or silence makes sound");
		whole.reset();
		independent.reset();
		a.fill(0);
		a[0] = 0.5f;
		b    = a;
		whole.process(a.data(), a.size());
		independent.process(b.data(), b.size());
		check(a == b, "reset did not clear all state");
		p.ratio           = 1;
		p.midGainDb       = 0;
		p.makeupDb        = 0;
		p.inputGainDb     = 0;
		p.saturationDrive = 0;
		const double mid  = tone(p, 1000, 0.02);
		const double bass = tone(p, 70, 0.02), treble = tone(p, 8000, 0.02);
		check(bass < mid * 0.08, "highpass stopband leak");
		check(treble < mid * 0.03, "lowpass stopband leak");
		std::cout << name << ": 70Hz/1kHz=" << 20 * std::log10(bass / mid)
				  << "dB 8kHz/1kHz=" << 20 * std::log10(treble / mid) << "dB\n";
		check(tone(p, 1000, 1e-8) > 1e-9, "quiet signal lost");
	}
	Parameters p      = profile("cb-am");
	p.midGainDb       = 0;
	p.inputGainDb     = 0;
	p.makeupDb        = 0;
	p.saturationDrive = 0;
	p.kneeDb          = 0;
	const double low = tone(p, 1000, 0.1), high = tone(p, 1000, 0.4);
	const double slope = 20 * std::log10(high / low) / (20 * std::log10(4));
	check(slope > 0.29 && slope < 0.38, "3:1 compressor steady-state slope incorrect");
	std::cout << "3:1 compression slope=" << slope << '\n';
	Parameters maximum      = profile("cb-am");
	maximum.inputGainDb     = 18;
	maximum.makeupDb        = 18;
	maximum.outputGainDb    = 12;
	maximum.ratio           = 1;
	maximum.saturationDrive = 0;
	VoiceProcessor overload(maximum);
	std::array< float, FrameSamples > loud{};
	for (int frame = 0; frame < 100; ++frame) {
		for (std::size_t i = 0; i < loud.size(); ++i)
			loud[i] = i % 2 ? 8.0f : -8.0f;
		check(overload.process(loud.data(), loud.size()), "finite overload rejected");
		for (float x : loud)
			check(std::isfinite(x) && std::abs(x) <= 0.980001, "overload escaped safety ceiling");
	}
	for (int frame = 0; frame < 500; ++frame) {
		loud.fill(0);
		overload.process(loud.data(), loud.size());
	}
	for (float x : loud)
		check(std::abs(x) < 1e-15, "silence tail unstable");
	p.enabled = false;
	VoiceProcessor bypass(p);
	std::array< float, FrameSamples > block{};
	block.fill(0.123f);
	const std::array< float, FrameSamples > copy = block;
	check(bypass.process(block.data(), block.size()) && block == copy, "bypass altered samples");
	block[0] = std::numeric_limits< float >::infinity();
	block[1] = std::numeric_limits< float >::quiet_NaN();
	check(!bypass.process(block.data(), block.size()) && block[0] == 0 && block[1] == 0, "invalid PCM not contained");
	check(!bypass.process(nullptr, 1) && bypass.process(nullptr, 0), "invalid pointer contract");
	check(!bypass.process(block.data(), 961), "oversize block accepted");
	bool refused = false;
	p.ratio      = 0;
	try {
		VoiceProcessor invalid(p);
	} catch (const std::invalid_argument &) {
		refused = true;
	}
	check(refused, "bad configuration accepted");
}
} // namespace
int main() {
	try {
		tests();
		std::cout << "PASS: blocks/state/allocation/frequency/compression/limits/silence/reset/independence\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
