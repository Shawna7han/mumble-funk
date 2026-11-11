#!/usr/bin/env bash
set -euo pipefail
[[ $EUID == 0 ]] || { echo 'Bitte mit sudo starten.' >&2; exit 1; }
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
# shellcheck source=scripts/funk/platform.sh
source "$repo/scripts/funk/platform.sh"
# shellcheck source=/dev/null
. /etc/os-release
funk_require_supported_os "${ID:-}" "${VERSION_ID:-}"
exec 9>/run/lock/mumble-funk-audio-engine-install.lock
flock -n 9 || { echo 'Audio-Preview-Installation läuft.' >&2; exit 1; }
# Do not stop a foreign service using the same name.
python3 "$repo/scripts/funk/audio-preview/setup_support.py" preflight >/dev/null
systemctl disable --now mumble-funk-audio-engine.service
rm -f /etc/systemd/system/mumble-funk-audio-engine.service /etc/logrotate.d/mumble-funk-audio-engine /usr/local/sbin/mumble-funk-audio-engine-info /usr/local/sbin/mumble-funk-audio-engine
systemctl daemon-reload
echo 'Nur Audio-Preview-Dienst entfernt. Einstellungen, Datenbank, Logs und Programmstände sind erhalten.'
echo 'Mumble-Funk v0.2 und Standard-Mumble wurden nicht verändert.'
