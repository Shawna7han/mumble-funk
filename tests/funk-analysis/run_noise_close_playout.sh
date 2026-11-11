#!/usr/bin/env bash
# Isolated native scheduling model; no Android, network or installation changes.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
probe_dir=$(mktemp -d "${TMPDIR:-/tmp}/mumble-noise-playout.XXXXXX")
trap 'rm -rf -- "$probe_dir"' EXIT
cat > "$probe_dir/speexdsp_config_types.h" <<'TYPES'
#include <stdint.h>
typedef int16_t spx_int16_t;
typedef uint16_t spx_uint16_t;
typedef int32_t spx_int32_t;
typedef uint32_t spx_uint32_t;
TYPES
cc -std=c99 -DEXPORT= -DFLOATING_POINT -I"$probe_dir" \
    -I"$repo/3rdparty/speexdsp/include" -I"$repo/3rdparty/speexdsp/libspeexdsp" \
    "$repo/tests/funk-analysis/noise_close_playout.c" \
    "$repo/3rdparty/speexdsp/libspeexdsp/jitter.c" -lm -o "$probe_dir/probe"
"$probe_dir/probe"
