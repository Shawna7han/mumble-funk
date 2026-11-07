// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "WaveIO.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace funkvoice {
namespace {
	std::uint32_t le(const unsigned char *p, unsigned bytes) {
		std::uint32_t n = 0;
		for (unsigned i = 0; i < bytes; ++i)
			n |= std::uint32_t(p[i]) << (8 * i);
		return n;
	}
	void readExact(std::istream &file, void *buffer, std::size_t size) {
		if (!file.read(static_cast< char * >(buffer), static_cast< std::streamsize >(size)))
			throw std::runtime_error("WAV: truncated data");
	}
	void writeExact(std::FILE *file, const void *data, std::size_t bytes) {
		if (std::fwrite(data, 1, bytes, file) != bytes)
			throw std::runtime_error("WAV: output write failed");
	}
	void writeLe(std::FILE *file, std::uint32_t n, unsigned bytes) {
		unsigned char data[4]{};
		for (unsigned i = 0; i < bytes; ++i)
			data[i] = static_cast< unsigned char >(n >> (8 * i));
		writeExact(file, data, bytes);
	}
} // namespace
WaveReader::WaveReader(const std::filesystem::path &path) : m_file(path, std::ios::binary) {
	if (!m_file || !std::filesystem::is_regular_file(path))
		throw std::runtime_error("WAV: cannot read regular input file");
	const std::uint64_t physical = std::filesystem::file_size(path);
	if (physical < 12 || physical > 2147483648ULL)
		throw std::runtime_error("WAV: expected RIFF file <=2 GiB");
	std::array< unsigned char, 16 > header{};
	readExact(m_file, header.data(), 12);
	if (std::memcmp(header.data(), "RIFF", 4) || std::memcmp(header.data() + 8, "WAVE", 4))
		throw std::runtime_error("WAV: only little-endian RIFF/WAVE supported");
	const std::uint64_t end = std::uint64_t(le(header.data() + 4, 4)) + 8;
	if (end > physical || end < 12)
		throw std::runtime_error("WAV: invalid RIFF length");
	std::uint64_t offset = 12, dataOffset = 0, dataBytes = 0;
	bool haveFormat = false, haveData = false;
	while (offset < end) {
		if (end - offset < 8)
			throw std::runtime_error("WAV: incomplete chunk header");
		m_file.seekg(static_cast< std::streamoff >(offset));
		readExact(m_file, header.data(), 8);
		const std::uint64_t size = le(header.data() + 4, 4), next = offset + 8 + size + (size & 1);
		if (next > end)
			throw std::runtime_error("WAV: chunk exceeds RIFF bounds");
		if (!std::memcmp(header.data(), "fmt ", 4)) {
			if (haveFormat || size < 16 || size > 64)
				throw std::runtime_error("WAV: invalid fmt chunk");
			readExact(m_file, header.data(), 16);
			m_format                = le(header.data(), 2);
			const unsigned channels = le(header.data() + 2, 2), rate = le(header.data() + 4, 4);
			const unsigned bits = le(header.data() + 14, 2), alignment = le(header.data() + 12, 2);
			m_width = bits / 8;
			if (channels != 1 || rate != SampleRate)
				throw std::runtime_error("WAV: mono 48000 Hz required; no implicit resampling");
			if (!((m_format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (m_format == 3 && bits == 32)))
				throw std::runtime_error("WAV: PCM16/24/32 or IEEE float32 required");
			if (alignment != m_width || le(header.data() + 8, 4) != SampleRate * m_width)
				throw std::runtime_error("WAV: inconsistent byte rate or block alignment");
			haveFormat = true;
		} else if (!std::memcmp(header.data(), "data", 4)) {
			if (haveData)
				throw std::runtime_error("WAV: multiple data chunks unsupported");
			haveData   = true;
			dataOffset = offset + 8;
			dataBytes  = size;
		}
		offset = next;
	}
	if (!haveFormat || !haveData || !dataBytes || dataBytes % m_width)
		throw std::runtime_error("WAV: missing/empty or misaligned audio data");
	m_samples = m_remaining = dataBytes / m_width;
	m_file.seekg(static_cast< std::streamoff >(dataOffset));
}
std::size_t WaveReader::read(float *output, std::size_t count) {
	if (!output || count > FrameSamples)
		throw std::invalid_argument("WAV: invalid read buffer");
	count = static_cast< std::size_t >(std::min< std::uint64_t >(count, m_remaining));
	std::array< unsigned char, FrameSamples * 4 > bytes{};
	readExact(m_file, bytes.data(), count * m_width);
	for (std::size_t i = 0; i < count; ++i) {
		const std::uint32_t raw = le(bytes.data() + i * m_width, m_width);
		if (m_format == 3) {
			static_assert(sizeof(float) == 4 && std::numeric_limits< float >::is_iec559, "IEEE float32 required");
			std::memcpy(output + i, &raw, 4);
			if (!std::isfinite(output[i]) || std::abs(output[i]) > 8)
				throw std::runtime_error("WAV: invalid float PCM");
		} else {
			const unsigned bits = m_width * 8;
			const std::int64_t signedValue =
				raw & (std::uint32_t(1) << (bits - 1)) ? std::int64_t(raw) - (std::int64_t(1) << bits) : raw;
			output[i] = static_cast< float >(signedValue / std::pow(2.0, bits - 1));
		}
	}
	m_remaining -= count;
	return count;
}
WaveWriter::WaveWriter(const std::filesystem::path &path, std::uint64_t samples) : m_path(path), m_expected(samples) {
	if (!samples || samples > (2147483648ULL - 44) / 2)
		throw std::invalid_argument("WAV: output duration exceeds RIFF limit");
	// Exclusive creation also rejects an existing input path, symlink or hard link.
	m_file = std::fopen(path.string().c_str(), "wbx");
	if (!m_file)
		throw std::runtime_error("WAV: cannot create new output (existing files are never overwritten)");
	try {
		writeExact(m_file, "RIFF", 4);
		writeLe(m_file, static_cast< std::uint32_t >(36 + samples * 2), 4);
		writeExact(m_file, "WAVEfmt ", 8);
		writeLe(m_file, 16, 4);
		writeLe(m_file, 1, 2);
		writeLe(m_file, 1, 2);
		writeLe(m_file, SampleRate, 4);
		writeLe(m_file, SampleRate * 2, 4);
		writeLe(m_file, 2, 2);
		writeLe(m_file, 16, 2);
		writeExact(m_file, "data", 4);
		writeLe(m_file, static_cast< std::uint32_t >(samples * 2), 4);
	} catch (...) {
		std::fclose(m_file);
		m_file = nullptr;
		std::filesystem::remove(m_path);
		throw;
	}
}
WaveWriter::~WaveWriter() {
	if (m_file)
		std::fclose(m_file);
	if (!m_finished) {
		std::error_code error;
		std::filesystem::remove(m_path, error);
	}
}
void WaveWriter::write(const float *samples, std::size_t count) {
	if (!m_file || !samples || count > FrameSamples || count > m_expected - m_written)
		throw std::invalid_argument("WAV: invalid output count");
	std::array< unsigned char, FrameSamples * 2 > buffer{};
	for (std::size_t i = 0; i < count; ++i) {
		const double x = samples[i];
		if (!std::isfinite(x))
			throw std::runtime_error("WAV: non-finite output");
		m_peak = std::max(m_peak, std::abs(x));
		if (x < -1 || x > 32767.0 / 32768)
			++m_clipped;
		const int value             = static_cast< int >(std::lround(std::clamp(x, -1.0, 32767.0 / 32768) * 32768));
		const std::uint16_t encoded = static_cast< std::uint16_t >(value);
		buffer[2 * i]               = static_cast< unsigned char >(encoded);
		buffer[2 * i + 1]           = static_cast< unsigned char >(encoded >> 8);
	}
	writeExact(m_file, buffer.data(), count * 2);
	m_written += count;
}
void WaveWriter::finish() {
	if (!m_file || m_written != m_expected)
		throw std::runtime_error("WAV: incomplete output");
	const int status = std::fclose(m_file);
	m_file           = nullptr;
	if (status != 0)
		throw std::runtime_error("WAV: output close failed");
	m_finished = true;
}
} // namespace funkvoice
