#!/usr/bin/env bash
# Run as your normal login user, from any directory.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# shellcheck source=scripts/funk/source.sh
source "$repo/scripts/funk/source.sh"
source_args=()
if [[ $# == 1 && ( $1 == --experimental-squelch || $1 == --experimental-audio-volume || $1 == --experimental-busy-tone || $1 == --experimental-audio-defaults || $1 == --experimental-dev || $1 == --experimental-radio-noise ) ]]; then
    source_args=("$1")
elif [[ $# != 0 ]]; then
    echo 'Aufruf: build.sh [--experimental-squelch|--experimental-audio-volume|--experimental-busy-tone|--experimental-audio-defaults|--experimental-dev|--experimental-radio-noise]' >&2; exit 1
fi
funk_require_supported_source "$repo" "${source_args[@]}"
cmake -S "$repo" -B "$repo/build/funk" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_NUMBER=939 \
    -Dclient=OFF -Dserver=ON -Dtests=ON -Dplugins=OFF -Doverlay=OFF \
    -Dlto=OFF -DCMAKE_UNITY_BUILD=OFF
cmake --build "$repo/build/funk" --parallel "${FUNK_BUILD_JOBS:-2}"
ctest --test-dir "$repo/build/funk" --output-on-failure
echo "Build und Tests erfolgreich. Installation: sudo bash scripts/funk/install.sh ${source_args[*]}"
