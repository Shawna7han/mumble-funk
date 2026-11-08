// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
// Linked only into Linux test builds. No fault/delay switch in production builds.
#include <atomic>
#include <cstdlib>
#include <fcntl.h>
#include <opus.h>
#include <string>
#include <unistd.h>

extern "C" int __real_opus_decode_float(OpusDecoder *, const unsigned char *, opus_int32, float *, int, int);
extern "C" int __wrap_opus_decode_float(OpusDecoder *decoder, const unsigned char *data, opus_int32 len, float *pcm,
										int frameSize, int fec) {
	static std::atomic< bool > entered{ false };
	const char *directory = std::getenv("MUMBLE_TEST_VOICE_GATE");
	if (directory && !entered.exchange(true)) {
		const int permit = ::open((std::string(directory) + "/release").c_str(), O_RDONLY);
		const int ready  = ::open((std::string(directory) + "/entered").c_str(), O_WRONLY);
		if (ready >= 0) {
			const char signal  = 'x';
			const auto written = ::write(ready, &signal, 1);
			(void) written;
			::close(ready);
		}
		if (permit >= 0) {
			char signal;
			const auto received = ::read(permit, &signal, 1);
			(void) received;
			::close(permit);
		}
	}
	return __real_opus_decode_float(decoder, data, len, pcm, frameSize, fec);
}
