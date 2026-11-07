# Analog voice: approved DSP and isolated live adapter

The dependency-free `funk-voice-dsp` static library preserves the approved Phase-1
DSP byte for byte. It is available in the main build when `server` or `tests` is
enabled, but is not yet linked to Murmur. Standalone builds need CMake >= 3.15
and a C++17 compiler:

```sh
cmake -S tools/analog-voice -B build/analog-voice -DCMAKE_BUILD_TYPE=Release
cmake --build build/analog-voice --parallel 2
ctest --test-dir build/analog-voice --output-on-failure
(cd tools/analog-voice && sha256sum -c SHA256SUMS)
```

`VOICE_BUILD_TESTS` defaults to ON standalone and to the parent's `tests` option
when embedded. Set it to OFF for only the library. `VOICE_SANITIZERS=ON` enables
ASan/UBSan on these targets with GCC/Clang.

The original offline preview and WAV/codec/CLI tests are optional:

```sh
cmake -S tools/analog-voice -B build/analog-offline \
  -DCMAKE_BUILD_TYPE=Release -DVOICE_BUILD_PREVIEW=ON
cmake --build build/analog-offline --parallel 2
ctest --test-dir build/analog-offline --output-on-failure
build/analog-offline/offline/funk-voice-preview --help
```

Preview requires libopus headers/library; its CLI tests also require Python 3.
Neither is required by the DSP target. Never commit recordings or generated WAVs.
The offline codec roundtrips and recording-wide RMS matching are not a live adapter.

The isolated B2 `AnalogVoiceStream` accepts external mono Opus packets, decodes,
processes the unchanged DSP, and encodes 20-ms outputs. It has no server, network,
Qt or thread dependency and is not linked to Murmur. Enable it explicitly:

```sh
cmake -S tools/analog-voice -B build/analog-live \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=OFF -DVOICE_BUILD_LIVE=ON \
  -DVOICE_BUILD_PREVIEW=ON
cmake --build build/analog-live --parallel 2
ctest --test-dir build/analog-live --output-on-failure
```

`VOICE_BUILD_LIVE` defaults to OFF and requires libopus development files.
`VOICE_BUILD_PREVIEW` remains optional; enabling both runs all four tests.
See [B2 API, sequence/EOF contract and results](../../docs/funk/analog-voice-b2.md)
and [B3 handoff](../../docs/funk/analog-voice-b3-handoff.md).

See [B1 provenance, verification and limitations](../../docs/funk/analog-voice-b1.md)
and [the B2 handoff](../../docs/funk/analog-voice-b2-handoff.md).
