// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "AnalogVoiceStream.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

static std::size_t allocations = 0;
// Keep allocation hooks opaque to GCC 14's mismatched-new-delete inference.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void *
	operator new(std::size_t count) {
	++allocations;
	if (void *p = std::malloc(count ? count : 1)) {
		return p;
	}
	throw std::bad_alloc();
}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void operator delete(void *p) noexcept {
	std::free(p);
}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void operator delete(void *p, std::size_t) noexcept {
	std::free(p);
}

#ifdef VOICE_TEST_CODEC_FAULTS
static int encodeFailure = -1, decodeFailure = -1;
extern "C" int __real_opus_encode_float(OpusEncoder *, const float *, int, unsigned char *, opus_int32);
extern "C" int __real_opus_decode_float(OpusDecoder *, const unsigned char *, opus_int32, float *, int, int);
extern "C" int __wrap_opus_encode_float(OpusEncoder *encoder, const float *pcm, int samples, unsigned char *packet,
										opus_int32 capacity) {
	if (encodeFailure >= 0 && encodeFailure-- == 0) {
		return OPUS_INTERNAL_ERROR;
	}
	return __real_opus_encode_float(encoder, pcm, samples, packet, capacity);
}
extern "C" int __wrap_opus_decode_float(OpusDecoder *decoder, const unsigned char *packet, opus_int32 bytes, float *pcm,
										int samples, int fec) {
	if (decodeFailure >= 0 && decodeFailure-- == 0) {
		return OPUS_INTERNAL_ERROR;
	}
	return __real_opus_decode_float(decoder, packet, bytes, pcm, samples, fec);
}
#endif

namespace {
using namespace funkvoice;
using Stream  = AnalogVoiceStream;
using Packet  = Stream::Packet;
using Payload = std::vector< unsigned char >;
using Packets = std::vector< Packet >;
void check(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}
void ok(int code) {
	check(code >= 0, "Reference Opus call failed");
}
struct Decoder {
	OpusDecoder *value;
	Decoder() {
		int error;
		value = opus_decoder_create(SampleRate, 1, &error);
		ok(error);
	}
	~Decoder() { opus_decoder_destroy(value); }
	Decoder(const Decoder &) = delete;
	Decoder &operator=(const Decoder &) = delete;
};
struct Encoder {
	OpusEncoder *value;
	explicit Encoder(bool client = false, int channels = 1) {
		int error;
		value = opus_encoder_create(SampleRate, channels,
									client ? OPUS_APPLICATION_RESTRICTED_LOWDELAY : OPUS_APPLICATION_VOIP, &error);
		ok(error);
		ok(opus_encoder_ctl(value, OPUS_SET_BITRATE(40000)));
		ok(opus_encoder_ctl(value, OPUS_SET_COMPLEXITY(5)));
		ok(opus_encoder_ctl(value, OPUS_SET_DTX(0)));
		ok(opus_encoder_ctl(value, OPUS_SET_INBAND_FEC(0)));
	}
	~Encoder() { opus_encoder_destroy(value); }
	Encoder(const Encoder &) = delete;
	Encoder &operator=(const Encoder &) = delete;
};
class Source {
public:
	Encoder encoder{ true };
	std::size_t cursor = 0;
	Payload packet(int samples, int channels = 1) {
		std::array< float, 5760 * 2 > pcm{};
		for (int i = 0; i < samples; ++i) {
			const float value =
				static_cast< float >(0.18 * std::sin((cursor + i) * 0.089) + 0.07 * std::sin((cursor + i) * 0.161));
			for (int c = 0; c < channels; ++c) {
				pcm[i * channels + c] = value;
			}
		}
		cursor += samples;
		Payload payload(Stream::MaxInputBytes);
		int bytes;
		if (samples <= 2880) {
			bytes = opus_encode_float(encoder.value, pcm.data(), samples, payload.data(), payload.size());
		} else {
			// 120 ms is a legal multi-frame packet; do not rely on encoder frame-size extensions.
			std::array< unsigned char, 576 > parts[6]{};
			std::unique_ptr< OpusRepacketizer, decltype(&opus_repacketizer_destroy) > repacketizer(
				opus_repacketizer_create(), opus_repacketizer_destroy);
			check(bool(repacketizer), "Repacketizer allocation");
			for (int i = 0; i < 6; ++i) {
				const int length =
					opus_encode_float(encoder.value, pcm.data() + i * 960, 960, parts[i].data(), parts[i].size());
				ok(length);
				ok(opus_repacketizer_cat(repacketizer.get(), parts[i].data(), length));
			}
			bytes = opus_repacketizer_out(repacketizer.get(), payload.data(), payload.size());
		}
		ok(bytes);
		payload.resize(bytes);
		return payload;
	}
};
void feed(Stream &stream, const Payload &payload, std::uint64_t frame, bool end, Stream::Result &result) {
	const std::size_t before = allocations;
	stream.process({ payload.data(), payload.size(), frame, end }, result);
	check(allocations == before, "Live processing allocated C++ storage");
	check(result.disposition == Stream::Disposition::Accepted, "Valid input was rejected");
	check(result.count <= Stream::MaxOutputPackets, "Output capacity exceeded");
}
void collect(Packets &packets, const Stream::Result &result) {
	packets.insert(packets.end(), result.packets.begin(), result.packets.begin() + result.count);
}
std::vector< float > decode(const Packets &packets) {
	Decoder decoder;
	std::vector< float > audio;
	for (const Packet &packet : packets) {
		std::array< float, FrameSamples > pcm{};
		check(opus_packet_get_nb_samples(packet.payload.data(), packet.bytes, SampleRate) == 960,
			  "Output is not 20 ms");
		check(opus_decode_float(decoder.value, packet.payload.data(), packet.bytes, pcm.data(), pcm.size(), 0) == 960,
			  "Output is not decodable");
		for (float value : pcm) {
			check(std::isfinite(value), "Non-finite decoded audio");
		}
		audio.insert(audio.end(), pcm.begin(), pcm.end());
	}
	return audio;
}
void markers(const Packets &packets, std::uint64_t start, Stream::End end = Stream::End::InputMarker) {
	check(!packets.empty(), "No output for speech");
	for (std::size_t i = 0; i < packets.size(); ++i) {
		check(packets[i].frame == start + i * 2, "Wrong 10-ms output clock");
		check(packets[i].end == (i + 1 == packets.size() ? end : Stream::End::None), "Incorrect end marker");
	}
}
// Whole-transmission reference: independent input decoder, DSP and output encoder.
// Does not reuse the adapter's reblock, provenance, EOF or sequence code.
Packets reference(const std::vector< Payload > &inputs, const Parameters &parameters) {
	Decoder decoder;
	Encoder encoder;
	VoiceProcessor processor(parameters);
	std::vector< float > pcm;
	for (const Payload &input : inputs) {
		std::array< float, 5760 > block{};
		const int count = opus_decode_float(decoder.value, input.data(), input.size(), block.data(), block.size(), 0);
		ok(count);
		pcm.insert(pcm.end(), block.begin(), block.begin() + count);
	}
	for (std::size_t i = 0; i < pcm.size(); i += FrameSamples) {
		check(processor.process(pcm.data() + i, std::min(FrameSamples, pcm.size() - i)), "Reference DSP failed");
	}
	int lookahead;
	ok(opus_encoder_ctl(encoder.value, OPUS_GET_LOOKAHEAD(&lookahead)));
	pcm.resize(((pcm.size() + lookahead + 959) / 960) * 960, 0);
	Packets packets;
	for (std::size_t i = 0; i < pcm.size(); i += FrameSamples) {
		Packet packet;
		const int count =
			opus_encode_float(encoder.value, pcm.data() + i, 960, packet.payload.data(), packet.payload.size());
		ok(count);
		packet.bytes = count;
		packets.push_back(packet);
	}
	return packets;
}
void equalPayloads(const Packets &a, const Packets &b) {
	check(a.size() == b.size(), "Reference length differs (lookahead or EOF lost)");
	for (std::size_t i = 0; i < a.size(); ++i) {
		check(a[i].bytes == b[i].bytes
				  && std::equal(a[i].payload.begin(), a[i].payload.begin() + a[i].bytes, b[i].payload.begin()),
			  "Reference audio differs (input trim, padding or DSP state)");
	}
}
void provenance(const Packets &packets, const std::vector< int > &durations, int lookahead, std::uint64_t start) {
	std::size_t realSamples = 0;
	for (int duration : durations) {
		realSamples += duration;
	}
	std::size_t sample = 0;
	for (const Packet &packet : packets) {
		std::size_t covered = 0;
		for (std::size_t i = 0; i < packet.spanCount; ++i) {
			const Stream::Span &span = packet.spans[i];
			check(span.outputOffset == covered && span.samples > 0, "Provenance holes or overlap");
			for (unsigned j = 0; j < span.samples; ++j, ++sample) {
				if (sample < static_cast< std::size_t >(lookahead)) {
					check(span.origin == Stream::Origin::EncoderDelay, "Missing initial encoder delay");
				} else if (sample < realSamples + lookahead) {
					check(span.origin == Stream::Origin::Source, "Missing real source sample");
					std::size_t offset  = sample - lookahead;
					std::uint64_t frame = start;
					for (int duration : durations) {
						if (offset < static_cast< std::size_t >(duration)) {
							break;
						}
						offset -= duration;
						frame += duration / 480;
					}
					check(span.sourceFrame == frame && span.sourceOffset + j == offset,
						  "Source packet/offset mapping differs from delayed timeline");
				} else {
					check(span.origin == Stream::Origin::EndPadding, "Padding outside EOF");
				}
			}
			covered += span.samples;
		}
		check(covered == 960, "Incomplete output provenance");
	}
}
void transmission(const char *name, const std::vector< int > &durations, bool payloadEnd) {
	Source source;
	const Parameters parameters = profile(name);
	Stream stream(parameters);
	check(stream.begin(73), "Start failed");
	Stream::Result result;
	Packets actual;
	std::vector< Payload > inputs;
	std::uint64_t frame = 73, last = 73;
	std::size_t samples = 0;
	for (std::size_t i = 0; i < durations.size(); ++i) {
		inputs.push_back(source.packet(durations[i]));
		feed(stream, inputs.back(), frame, payloadEnd && i + 1 == durations.size(), result);
		collect(actual, result);
		last = frame;
		frame += durations[i] / 480;
		samples += durations[i];
	}
	if (!payloadEnd) {
		feed(stream, {}, last, true, result);
		collect(actual, result);
	}
	check(stream.nextSourceFrame() == frame, "Source clock counts callbacks instead of media time");
	check(stream.state() == Stream::State::Ended, "Stream did not end");
	markers(actual, 73);
	equalPayloads(actual, reference(inputs, parameters));
	provenance(actual, durations, stream.lookahead(), 73);
	const std::vector< float > audio = decode(actual);
	const std::size_t first          = stream.lookahead() + 120;
	double head = 0, tail = 0;
	for (std::size_t i = 0; i < 120; ++i) {
		head += audio[first + i] * audio[first + i];
		tail += audio[samples + stream.lookahead() - 120 + i] * audio[samples + stream.lookahead() - 120 + i];
	}
	check(head > 0.00001 && tail > 0.00001, "First/last speech energy missing");
	stream.finish(result);
	check(result.count == 0, "Duplicate close output");
}
void lifecycle() {
	Stream stream(profile("cb-am"));
	Stream::Result result;
	Source source;
	const Payload input = source.packet(960);
	std::uint64_t next  = 0;
	for (int i = 0; i < 6; ++i) {
		const std::size_t before = allocations;
		check(stream.begin(i == 3 ? 900 : 0), "New transmission failed");
		check(allocations == before, "Reset allocated C++ storage");
		check(!stream.begin(0), "Active generation silently replaced");
		feed(stream, input, i == 3 ? 900 : 0, true, result);
		Packets packets;
		collect(packets, result);
		check(packets.front().frame >= next, "Output clock regressed between transmissions");
		equalPayloads(packets, reference({ input }, profile("cb-am")));
		next = stream.nextOutputFrame();
	}
	stream.reset();
	check(stream.begin(1), "Explicit reset failed");
	feed(stream, input, 1, false, result);
	check(result.count == 0, "No actual unsent reserve");
	Packets packets;
	stream.finish(result);
	collect(packets, result);
	markers(packets, next, Stream::End::Technical);
	check(stream.begin(3), "Empty transmission begin");
	feed(stream, {}, 3, true, result);
	check(result.count == 0, "Invented audio for empty transmission");
}
void gaps() {
	for (int firstSamples : { 480, 960 }) {
		for (std::uint64_t gap : { 1, 2, 3, 4, 5, 40, 6001 }) {
			Stream stream(profile("business-fm"));
			Stream::Result result;
			Source source;
			check(stream.begin(10), "Gap begin failed");
			const Payload first = source.packet(firstSamples), second = source.packet(960);
			Packets packets;
			feed(stream, first, 10, false, result);
			collect(packets, result);
			const std::uint64_t expected = stream.nextSourceFrame();
			stream.process({ first.data(), first.size(), 10, true }, result);
			check(result.disposition == Stream::Disposition::Ignored && result.count == 0,
				  "Duplicate payload affected state/termination");
			feed(stream, second, expected + gap, true, result);
			check(result.concealedFrames <= 4 && result.concealedFrames + result.skippedFrames == gap,
				  "Unbounded or hidden loss processing");
			collect(packets, result);
			std::uint64_t previous = packets.front().frame;
			std::uint64_t jumps    = 0;
			std::size_t plcSamples = 0;
			for (std::size_t i = 0; i < packets.size(); ++i) {
				if (i) {
					check(packets[i].frame >= previous + 2, "Gap output reordered");
					jumps += packets[i].frame - previous - 2;
				}
				previous = packets[i].frame;
				for (std::size_t j = 0; j < packets[i].spanCount; ++j) {
					const Stream::Span &span = packets[i].spans[j];
					if (span.origin == Stream::Origin::Concealment) {
						check(span.sourceFrame >= expected && span.sourceFrame < expected + result.concealedFrames,
							  "PLC source range incorrect");
						plcSamples += span.samples;
					}
				}
				check(packets[i].end == (i + 1 == packets.size() ? Stream::End::InputMarker : Stream::End::None),
					  "Gap end marker incorrect");
			}
			check(jumps == result.skippedFrames && plcSamples == result.concealedFrames * 480,
				  "Lost media time silently compressed");
			decode(packets);
		}
	}
}
void longStream() {
	Stream stream(profile("narrowband"));
	Stream::Result result;
	Source source;
	Decoder decoder;
	check(stream.begin(5), "Long stream start");
	std::uint64_t inputFrame = 5, outputFrame = 5;
	std::size_t ends = 0, packets = 0;
	for (int i = 0; i < 3101; ++i) {
		const Payload input = source.packet(960);
		feed(stream, input, inputFrame, i == 3100, result);
		inputFrame += 2;
		for (std::size_t j = 0; j < result.count; ++j) {
			const Packet &packet = result.packets[j];
			check(packet.frame == outputFrame, "Clock wrapped after 60 seconds");
			outputFrame += 2;
			ends += packet.end != Stream::End::None;
			++packets;
			std::array< float, 960 > audio{};
			check(opus_decode_float(decoder.value, packet.payload.data(), packet.bytes, audio.data(), 960, 0) == 960,
				  "Long output decode failed");
		}
	}
	check(inputFrame == 6207 && outputFrame == 6209 && ends == 1 && packets == 3102,
		  "Long stream length/marker failed");
}
void boundaries() {
	Stream stream(profile("cb-am")), independent(profile("narrowband"));
	Stream::Result result, other;
	Source source;
	const Payload small = source.packet(480), large = source.packet(5760);
	check(stream.begin(0) && independent.begin(70), "Boundary start");
	Packets isolated;
	feed(independent, small, 70, false, other);
	for (std::uint64_t frame = 0; frame < 3; ++frame) {
		feed(stream, small, frame, false, result);
		check(result.count == 0, "Half-frame or reserve emitted too early");
	}
	feed(stream, large, 7, true, result);
	check(result.count == Stream::MaxOutputPackets, "Worst-case batch capacity proof changed");
	Packets packets;
	collect(packets, result);
	decode(packets);
	feed(independent, small, 71, true, other);
	collect(isolated, other);
	equalPayloads(isolated, reference({ small, small }, profile("narrowband")));
	markers(isolated, 70);
	check(stream.begin(20), "Overlap start");
	const Payload forty = source.packet(1920);
	feed(stream, forty, 20, false, result);
	packets.clear();
	collect(packets, result);
	const std::uint64_t cursor = stream.nextOutputFrame();
	stream.process({ small.data(), small.size(), 21, true }, result);
	check(result.disposition == Stream::Disposition::Ignored && result.count == 0 && stream.nextOutputFrame() == cursor
			  && stream.nextSourceFrame() == 24,
		  "Overlapping stale packet changed the stream");
	feed(stream, {}, 24, true, result);
	collect(packets, result);
	markers(packets, 20);
	equalPayloads(packets, reference({ forty }, profile("cb-am")));
	const std::size_t before = allocations;
	stream.reset();
	check(allocations == before, "Abort allocated");
	check(stream.begin(0), "Abort restart");
	feed(stream, small, 0, false, result);
	stream.reset();
	check(stream.begin(0), "Partial abort restart");
	feed(stream, small, 0, true, result);
	packets.clear();
	collect(packets, result);
	equalPayloads(packets, reference({ small }, profile("cb-am")));
	Stream reassigned(profile("cb-am"));
	check(reassigned.begin(5, 700), "External connection clock floor rejected");
	feed(reassigned, small, 5, true, result);
	check(result.packets[0].frame == 700 && reassigned.nextSourceFrame() == 6,
		  "External output floor changed source clock");
}
void invalidInputs() {
	Source source;
	const Payload valid             = source.packet(960);
	const unsigned char malformed[] = { 0xe3, 0x88 };
	const unsigned char invalid[]   = { 3 };
	for (bool started : { false, true }) {
		for (int kind = 0; kind < 7; ++kind) {
			Stream stream(profile("cb-am"));
			Stream::Result result;
			check(stream.begin(0), "Invalid test begin");
			if (started) {
				feed(stream, valid, 0, false, result);
				feed(stream, valid, 2, false, result);
				check(result.count == 1, "Fault test did not begin output");
			}
			const std::uint64_t frame = started ? 4 : 0;
			Stream::Input bad{ invalid, sizeof(invalid), frame, false };
			if (kind == 1) {
				bad.payload = malformed;
				bad.bytes   = sizeof(malformed);
			}
			if (kind == 2) {
				bad.payload = nullptr;
			}
			if (kind == 3) {
				bad.payload = valid.data();
				bad.bytes   = 1025;
			}
			if (kind == 4) {
				bad.bytes = 0;
			}
			if (kind == 5) {
				bad.bytes = 0;
				bad.end   = true;
				bad.frame = frame + 1;
			}
			Payload shortPacket;
			if (kind == 6) {
				shortPacket = source.packet(240);
				bad.payload = shortPacket.data();
				bad.bytes   = shortPacket.size();
			}
			stream.process(bad, result);
			check(result.disposition == Stream::Disposition::Rejected, "Invalid payload accepted");
			check(stream.state() == (started ? Stream::State::Faulted : Stream::State::Bypass),
				  "Bypass/fault latch wrong");
			check(result.count == (started ? 1u : 0u), "Fault reserve missing or duplicate");
			if (started) {
				check(result.packets[0].end == Stream::End::Fault && result.packets[0].frame == 2, "Wrong fault close");
			}
			stream.process({ valid.data(), valid.size(), frame, true }, result);
			check(result.count == 0 && result.disposition == Stream::Disposition::Ignored,
				  "Latched generation resumed");
			check(stream.begin(0), "Fault reset failed");
			feed(stream, valid, 0, true, result);
		}
	}
	// Valid stereo and 2.5-ms packets must be rejected before codec output.
	Encoder stereo(true, 2);
	std::array< float, 1920 > silence{};
	Payload input(1024);
	const int bytes = opus_encode_float(stereo.value, silence.data(), 960, input.data(), input.size());
	ok(bytes);
	input.resize(bytes);
	Stream stream(profile("cb-am"));
	Stream::Result result;
	check(stream.begin(0), "Stereo begin");
	stream.process({ input.data(), input.size(), 0, false }, result);
	check(result.error == Stream::Error::UnsupportedChannels && stream.state() == Stream::State::Bypass,
		  "Stereo downmixed silently");
	check(stream.begin(0), "2.5ms begin");
	input = source.packet(120);
	stream.process({ input.data(), input.size(), 0, false }, result);
	check(result.error == Stream::Error::UnsupportedDuration, "2.5ms silently renumbered");
	check(!stream.begin(std::numeric_limits< std::uint64_t >::max()), "Output overflow accepted");
	check(stream.begin(0), "Overflow reset failed");
	stream.process({ valid.data(), valid.size(), std::numeric_limits< std::uint64_t >::max(), false }, result);
	check(result.error == Stream::Error::SequenceOverflow, "Source overflow not caught");
}
#ifdef VOICE_TEST_CODEC_FAULTS
void codecFaults() {
	Source source;
	const Payload input = source.packet(960), large = source.packet(5760);
	for (bool decoder : { false, true }) {
		for (int prior : { 0, 1, 2 }) {
			Stream stream(profile("cb-am"));
			Stream::Result result;
			check(stream.begin(0), "Injected fault begin");
			for (int i = 0; i < prior; ++i) {
				feed(stream, input, i * 2, false, result);
			}
			(decoder ? decodeFailure : encodeFailure) = 0;
			stream.process({ input.data(), input.size(), static_cast< std::uint64_t >(prior * 2), true }, result);
			check(result.error == (decoder ? Stream::Error::Decoder : Stream::Error::Encoder)
					  && result.codecError == OPUS_INTERNAL_ERROR,
				  "Codec error not preserved");
			check(stream.state() == (prior ? Stream::State::Faulted : Stream::State::Bypass), "Codec fault latch");
			check(result.count == (prior ? 1u : 0u), "Injected fault reserve");
			encodeFailure = decodeFailure = -1;
		}
	}
	for (int failure : { 0, 1, 4, 6 }) {
		Stream stream(profile("cb-am"));
		Stream::Result result;
		check(stream.begin(0), "Batch failure begin");
		feed(stream, input, 0, false, result);
		encodeFailure = failure;
		stream.process({ large.data(), large.size(), 2, true }, result);
		check(stream.state() == Stream::State::Faulted && result.count == 1 && result.packets[0].frame == 0
				  && result.packets[0].end == Stream::End::Fault,
			  "Failing batch leaked partial output or lost original reserve");
		encodeFailure = -1;
	}
	Stream stream(profile("cb-am"));
	Stream::Result result;
	check(stream.begin(0), "Finish failure begin");
	feed(stream, input, 0, false, result);
	encodeFailure = 0;
	stream.finish(result);
	check(result.count == 1 && result.packets[0].end == Stream::End::Fault, "EOF failure did not terminate reserve");
	encodeFailure = -1;
}
#endif
} // namespace

int main() {
	try {
		for (const char *name : { "cb-am", "business-fm", "narrowband" }) {
			for (int duration : { 480, 960, 1920, 2880, 5760 }) {
				transmission(name, { duration }, true);
				transmission(name, { duration }, false);
			}
			transmission(name, { 480, 480, 960, 1920, 480, 5760, 2880, 480 }, true);
			transmission(name, { 480, 480, 960, 1920, 480, 5760, 2880, 480 }, false);
		}
		lifecycle();
		gaps();
		longStream();
		boundaries();
		invalidInputs();
#ifdef VOICE_TEST_CODEC_FAULTS
		codecFaults();
#endif
		std::cout << "PASS: durations/profiles, exact reference, provenance, EOF/reserve, clocks, gaps, faults, reset, "
					 "allocation\n";
		Stream measured(profile("cb-am"));
		std::cout << opus_get_version_string() << ", lookahead=" << measured.lookahead()
				  << ", stream bytes=" << sizeof(Stream) << ", result bytes=" << sizeof(Stream::Result) << '\n';
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
