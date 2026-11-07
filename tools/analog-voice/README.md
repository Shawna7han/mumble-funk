# Analog voice: Phase-1 sources, B1 build integration

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

See [B1 provenance, verification and limitations](../../docs/funk/analog-voice-b1.md)
and [the B2 handoff](../../docs/funk/analog-voice-b2-handoff.md).
