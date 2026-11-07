// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#pragma once
#include "OpusCodec.h"
#include <filesystem>

namespace funkvoice {
struct PreviewSettings {
	OpusSettings first, second;
	double headroomDb = 3;
	bool levelMatch   = true;
};
struct PreviewReport {
	std::uint64_t samples = 0, clipped = 0;
	double gainDb = 0, peak = 0;
	int firstLookahead = 0, secondLookahead = 0;
};
// nullptr parameters gives the same two-codec chain without any radio DSP.
PreviewReport preview(const std::filesystem::path &input, const std::filesystem::path &output,
					  const Parameters *parameters, const PreviewSettings &settings);
} // namespace funkvoice
