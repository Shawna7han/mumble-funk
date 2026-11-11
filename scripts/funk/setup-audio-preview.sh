#!/usr/bin/env bash
# Separate entry point. Stable setup.sh remains unchanged.
set -euo pipefail
exec bash "$(dirname -- "${BASH_SOURCE[0]}")/audio-preview/setup.sh" "$@"
