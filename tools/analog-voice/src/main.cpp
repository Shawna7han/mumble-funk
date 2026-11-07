// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "Preview.h"
#include "WaveIO.h"
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
double number(const std::string &text) {
	std::size_t consumed = 0;
	const double n       = std::stod(text, &consumed);
	if (consumed != text.size() || !std::isfinite(n))
		throw std::invalid_argument("Invalid numeric option");
	return n;
}
int integer(const std::string &text) {
	const double n = number(text);
	if (n != std::floor(n) || n < 0 || n > 1000000)
		throw std::invalid_argument("Invalid integer option");
	return static_cast< int >(n);
}
void help() {
	std::cout << "funk-voice-preview --input FILE --profile cb-am|business-fm|narrowband|reference --output NEW.wav\n"
				 "  --all --output-dir NEWDIR       original + reference + three profiles\n"
				 "  --bitrate 40000 --second-bitrate 40000 --complexity 10 --vbr on|off\n"
				 "  --bandwidth auto|full|wide|narrow --headroom-db 3 --level-match on|off\n"
				 "  --set highpass_hz=300           repeatable DSP parameter override\n"
				 "  --list-profiles                 print all profile parameters\n"
				 "Input: mono 48kHz PCM16/24/32 or IEEE float32 RIFF WAV. Output: PCM16.\n"
				 "Existing outputs are never overwritten. Reference has two Opus round trips, no radio filters.\n";
}
void list() {
	for (const char *name : { "cb-am", "business-fm", "narrowband" }) {
		const funkvoice::Parameters p = funkvoice::profile(name);
		std::cout << name << ": highpass_hz=" << p.highpassHz << " lowpass_hz=" << p.lowpassHz << " mid_hz=" << p.midHz
				  << " mid_gain_db=" << p.midGainDb << " mid_q=" << p.midQ << " input_gain_db=" << p.inputGainDb
				  << " threshold_db=" << p.thresholdDb << " ratio=" << p.ratio << " knee_db=" << p.kneeDb
				  << " attack_ms=" << p.attackMs << " release_ms=" << p.releaseMs << " makeup_db=" << p.makeupDb
				  << " saturation_drive=" << p.saturationDrive << " output_gain_db=" << p.outputGainDb << '\n';
	}
}
} // namespace
int main(int argc, char **argv) {
	try {
		std::filesystem::path input, output, directory;
		std::string selection = "cb-am";
		std::map< std::string, double > overrides;
		funkvoice::PreviewSettings settings;
		bool secondSpecified = false, all = false;
		if (argc == 1) {
			help();
			return 0;
		}
		for (int i = 1; i < argc; ++i) {
			const std::string option = argv[i];
			if (option == "--help") {
				help();
				return 0;
			}
			if (option == "--list-profiles") {
				list();
				return 0;
			}
			if (option == "--all") {
				all = true;
				continue;
			}
			if (i + 1 >= argc)
				throw std::invalid_argument("Missing option value: " + option);
			const std::string value = argv[++i];
			if (option == "--input")
				input = value;
			else if (option == "--output")
				output = value;
			else if (option == "--output-dir")
				directory = value;
			else if (option == "--profile")
				selection = value;
			else if (option == "--bitrate")
				settings.first.bitrate = integer(value);
			else if (option == "--second-bitrate") {
				settings.second.bitrate = integer(value);
				secondSpecified         = true;
			} else if (option == "--complexity")
				settings.first.complexity = settings.second.complexity = integer(value);
			else if (option == "--headroom-db")
				settings.headroomDb = number(value);
			else if (option == "--vbr" || option == "--level-match") {
				if (value != "on" && value != "off")
					throw std::invalid_argument("Expected on or off");
				if (option == "--vbr")
					settings.first.vbr = settings.second.vbr = value == "on";
				else
					settings.levelMatch = value == "on";
			} else if (option == "--bandwidth") {
				int band = OPUS_AUTO;
				if (value == "full")
					band = OPUS_BANDWIDTH_FULLBAND;
				else if (value == "wide")
					band = OPUS_BANDWIDTH_WIDEBAND;
				else if (value == "narrow")
					band = OPUS_BANDWIDTH_NARROWBAND;
				else if (value != "auto")
					throw std::invalid_argument("Unknown bandwidth");
				settings.first.bandwidth = settings.second.bandwidth = band;
			} else if (option == "--set") {
				const std::size_t eq = value.find('=');
				if (eq == std::string::npos)
					throw std::invalid_argument("Expected --set key=value");
				overrides[value.substr(0, eq)] = number(value.substr(eq + 1));
			} else
				throw std::invalid_argument("Unknown option: " + option);
		}
		if (!secondSpecified)
			settings.second.bitrate = settings.first.bitrate;
		if (input.empty())
			throw std::invalid_argument("--input required");
		funkvoice::WaveReader validated(input);
		if (all && (!output.empty() || directory.empty()))
			throw std::invalid_argument("--all requires --output-dir, not --output");
		if (!all && (output.empty() || !directory.empty()))
			throw std::invalid_argument("Single profile requires --output, not --output-dir");
		if (!all && selection == "reference" && !overrides.empty())
			throw std::invalid_argument("Reference cannot have DSP overrides");
		const std::vector< std::string > names =
			all ? std::vector< std::string >{ "reference", "cb-am", "business-fm", "narrowband" }
				: std::vector< std::string >{ selection };
		std::vector< funkvoice::Parameters > parameters;
		for (const std::string &name : names) {
			funkvoice::Parameters p;
			if (name != "reference") {
				p = funkvoice::profile(name);
				for (const std::pair< const std::string, double > &entry : overrides)
					funkvoice::setParameter(p, entry.first, entry.second);
				funkvoice::validate(p);
			}
			parameters.push_back(p);
		}
		funkvoice::validate(settings.first);
		funkvoice::validate(settings.second);
		if (!std::isfinite(settings.headroomDb) || settings.headroomDb < 0 || settings.headroomDb > 24)
			throw std::invalid_argument("headroom-db must be 0..24");
		if (all) {
			if (!std::filesystem::create_directory(directory))
				throw std::invalid_argument("Output directory must be new");
			std::filesystem::copy_file(input, directory / "original.wav");
		}
		for (std::size_t i = 0; i < names.size(); ++i) {
			const std::string &name = names[i];
			const std::filesystem::path destination =
				all ? directory / (name == "reference" ? "opus-reference.wav" : name + ".wav") : output;
			const funkvoice::PreviewReport report =
				funkvoice::preview(input, destination, name == "reference" ? nullptr : &parameters[i], settings);
			std::cout << std::setprecision(10) << "{\"profile\":\"" << name << "\",\"samples\":" << report.samples
					  << ",\"level_gain_db\":" << report.gainDb << ",\"peak\":" << report.peak
					  << ",\"clipped_samples\":" << report.clipped << ",\"headroom_db\":" << settings.headroomDb
					  << ",\"first_bitrate\":" << settings.first.bitrate
					  << ",\"second_bitrate\":" << settings.second.bitrate
					  << ",\"lookahead_first\":" << report.firstLookahead
					  << ",\"lookahead_second\":" << report.secondLookahead << "}\n";
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Audio preview failed: " << e.what() << '\n';
		return 1;
	}
}
