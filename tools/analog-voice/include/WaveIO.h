// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#pragma once
#include "VoiceProcessor.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace funkvoice {
class WaveReader {
public:
	explicit WaveReader(const std::filesystem::path &path);
	std::size_t read(float *output, std::size_t count);
	std::uint64_t samples() const noexcept { return m_samples; }

private:
	std::ifstream m_file;
	std::uint64_t m_samples = 0, m_remaining = 0;
	unsigned m_format = 0, m_width = 0;
};
class WaveWriter {
public:
	WaveWriter(const std::filesystem::path &path, std::uint64_t samples);
	~WaveWriter();
	WaveWriter(const WaveWriter &)            = delete;
	WaveWriter &operator=(const WaveWriter &) = delete;
	void write(const float *samples, std::size_t count);
	void finish();
	std::uint64_t clipped() const noexcept { return m_clipped; }
	double peak() const noexcept { return m_peak; }

private:
	std::filesystem::path m_path;
	std::FILE *m_file = nullptr;
	std::uint64_t m_expected, m_written = 0, m_clipped = 0;
	double m_peak   = 0;
	bool m_finished = false;
};
} // namespace funkvoice
