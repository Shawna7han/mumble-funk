#!/usr/bin/env bash
# Uses only the existing Compose service and volumes. No .env or port migration.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo"
command=${1:-status}
case "$command" in
    status)
        [[ $# -le 1 ]] || { echo 'Aufruf: audio.sh status' >&2; exit 1; }
        docker compose run --rm --no-deps -T mumble-funk audio status
        exit 0 ;;
    enable|disable|reset-busy-tone|use-standard-busy-tone|enable-squelch|disable-squelch|enable-open|disable-open|enable-close|disable-close|enable-timeout-close|disable-timeout-close|reset-open|reset-close|enable-radio-noise|disable-radio-noise|use-standard-radio-noise)
        [[ $# == 1 ]] || { echo 'Dieser Befehl benötigt keinen Wert.' >&2; exit 1; } ;;
    boost)
        if [[ $# != 2 ]] || [[ ! $2 =~ ^(0|[1-9]|1[0-2])$ ]]; then
            echo 'Audio Boost: ganze Zahl von 0 bis 12 dB.' >&2; exit 1
        fi ;;
    volume|open-volume|close-volume|radio-noise-volume)
        if [[ $# != 2 ]] || [[ ! $2 =~ ^[0-9]{1,3}$ ]] || ((10#$2 > 100)); then
            echo 'Lautstärke: 0 bis 100.' >&2; exit 1
        fi ;;
    set-busy-tone|set-open|set-close|set-radio-noise)
        [[ $# == 2 ]] || { echo 'WAV-Datei fehlt.' >&2; exit 1; }
        [[ -f $2 && ! -L $2 ]] || { echo 'Normale WAV-Datei ohne symbolischen Link benötigt.' >&2; exit 1; } ;;
    *) echo 'Unbekannter Audio-Befehl; siehe docs/funk/docker.md (Audioverwaltung).' >&2; exit 1 ;;
esac
was_running=$(docker compose ps --status running --quiet mumble-funk)
revision=''
temporary=''
healthy() {
    for ((attempt=0; attempt<30; attempt++)); do
        if docker compose exec -T mumble-funk funk-container health >/dev/null 2>&1; then return 0; fi
        sleep 0.2
    done
    return 1
}
resume() {
    result=$?
    trap - EXIT
    if [[ -n $temporary ]]; then rm -rf -- "$temporary"; fi
    if [[ -n $was_running ]]; then
        if ! docker compose start mumble-funk >&2 || ! healthy; then
            echo 'Audio-Neustart nicht erfolgreich; vorige Konfiguration wird wiederhergestellt.' >&2
            if [[ -n $revision ]]; then
                # Only restore the configuration we just published. The runtime
                # refuses if another administrator has changed it meanwhile.
                if docker compose stop --timeout 30 mumble-funk >&2 &&
                   docker compose run --rm --no-deps -T mumble-funk audio-rollback "$revision" >&2; then
                    if ! docker compose start mumble-funk >&2 || ! healthy; then
                        echo 'Auch der vorige Stand startet nicht gesund; bitte Containerlog prüfen.' >&2
                    fi
                else
                    echo 'Wiederherstellung abgelehnt; bitte Konfiguration und Containerlog prüfen.' >&2
                fi
            fi
            result=1
        fi
    fi
    exit "$result"
}
trap resume EXIT
docker compose stop --timeout 30 mumble-funk >&2
if [[ $command == set-* ]]; then
    # Snapshot a bounded local file. Only this temporary copy is mounted read-only.
    # The manager validates it with the actual server parser before publishing.
    temporary=$(mktemp -d)
    limit=131072
    if [[ $command == set-radio-noise ]]; then limit=8388608; fi
    python3 - "$2" "$temporary/asset.wav" "$limit" <<'PY'
import os, stat, sys
limit = int(sys.argv[3])
fd = os.open(sys.argv[1], os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
try:
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
        raise SystemExit('WAV-Datei überschreitet die Größenbegrenzung dieses Signaltyps.')
    with os.fdopen(os.dup(fd), 'rb') as source:
        data = source.read(limit + 1)
    if len(data) > limit:
        raise SystemExit('WAV-Datei zu groß.')
finally:
    os.close(fd)
with open(sys.argv[2], 'xb') as target:
    target.write(data)
os.chmod(sys.argv[2], 0o444)
PY
    chmod 0755 "$temporary"
    output=$(docker compose run --rm --no-deps -T -v "$temporary/asset.wav:/tmp/funk-import.wav:ro" mumble-funk audio "$command" /tmp/funk-import.wav)
else
    output=$(docker compose run --rm --no-deps -T mumble-funk audio "$@")
fi
# The revision is a hash only; neither configuration nor password is printed.
revision=$(sed -n 's/^AUDIO-REVISION=\([0-9a-f]\{64\}\)$/\1/p' <<< "$output")
[[ $revision =~ ^[0-9a-f]{64}$ ]] || { echo 'Audio-Konfigurationsrevision fehlt.' >&2; exit 1; }
printf '%s\n' "$output" | sed '/^AUDIO-REVISION=/d'
