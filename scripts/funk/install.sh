#!/usr/bin/env bash
# Only deploys our instance. Never runs apt or cmake --install.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
templates="$repo/scripts/funk/ubuntu"
# shellcheck source=scripts/funk/platform.sh
source "$repo/scripts/funk/platform.sh"
root=
binary=
setup=false
port_args=()
source_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage)
            [[ $# -ge 3 ]] || { echo 'Stage-Ziel und Serverbinary fehlen.' >&2; exit 1; }
            root=$(realpath -m -- "$2")
            [[ $root != / ]] || { echo 'Stage darf nicht / sein.' >&2; exit 1; }
            binary=$(realpath -- "$3"); shift 3 ;;
        --setup) setup=true; shift ;;
        --experimental-squelch|--experimental-audio-volume|--experimental-busy-tone|--experimental-audio-defaults|--experimental-dev|--experimental-radio-noise) source_args=("$1"); shift ;;
        --port)
            [[ $# -ge 2 ]] || { echo 'Port fehlt.' >&2; exit 1; }
            port_args=(--port "$2"); shift 2 ;;
        *) echo 'Aufruf: install.sh [--setup] [--port PORT] [--experimental-squelch|--experimental-audio-volume|--experimental-busy-tone|--experimental-audio-defaults|--experimental-dev|--experimental-radio-noise] [--stage TESTZIEL SERVERBINARY]' >&2; exit 1 ;;
    esac
done
root_args=()
if [[ -n $root ]]; then
    root_args=(--root "$root")
else
    [[ $EUID == 0 ]] || { echo 'Bitte mit sudo bash scripts/funk/install.sh starten.' >&2; exit 1; }
    # OS-provided file, not part of the repository.
    # shellcheck source=/dev/null
    . /etc/os-release
    funk_require_supported_os "${ID:-}" "${VERSION_ID:-}" || exit 1
    # Experimental source requires explicit opt-in and verified publication.
    # shellcheck source=scripts/funk/source.sh
    source "$repo/scripts/funk/source.sh"
    funk_require_supported_source "$repo" "${source_args[@]}"
    exec 9>/run/lock/mumble-funk-install.lock
    flock -n 9 || { echo 'Eine andere Funkserver-Installation läuft bereits.' >&2; exit 1; }
    binary="$repo/build/funk/mumble-server"
fi
command -v python3 >/dev/null || { echo 'python3 fehlt; bitte setup.sh verwenden.' >&2; exit 1; }
port=$(python3 "$repo/scripts/funk/setup_support.py" preflight "${root_args[@]}" "${port_args[@]}")
[[ -x $binary ]] || { echo "Serverbinary fehlt: $binary" >&2; exit 1; }
version=$("$binary" --version 2>&1)
[[ $version == *1.5.939* ]] || { echo 'Erwartet wird Mumble 1.5.939.' >&2; exit 1; }
# Validate the shipped short signals before writing deployment files.
"$binary" --validate-feedback-wav "$repo/assets/audio/busy-tone.wav" >/dev/null
for asset in squelch-open.wav squelch-close.wav; do
    "$binary" --validate-squelch-wav "$repo/assets/audio/$asset" >/dev/null
done
# Validate the long recording with its own bounds, not the short-cue parser.
"$binary" --validate-radio-noise-wav "$repo/assets/audio/radio-noise.wav" >/dev/null
# Never overwrite an operator recording at a newly packaged standard path.
# Reject conflicts before writing deployment files or changing a service.
for asset in busy-tone.wav radio-noise.wav; do
    standard="$root/usr/local/share/mumble-funk/audio/$asset"
    if [[ -e $standard || -L $standard ]]; then
        if [[ ! -f $standard || -L $standard ]] || \
           [[ $(sha256sum -- "$standard" | cut -d ' ' -f 1) != $(sha256sum -- "$repo/assets/audio/$asset" | cut -d ' ' -f 1) ]]; then
            echo "Vorhandene abweichende Standardpfad-Datei $asset bleibt erhalten. Bitte vor dem Update separat sichern/importieren und den Pfad freigeben." >&2
            exit 1
        fi
    fi
done
release=$(sha256sum -- "$binary" | cut -d ' ' -f 1)
base="$root/opt/mumble-funk"
if [[ -z $root ]]; then
    if ! id mumble-funk >/dev/null 2>&1; then
        useradd --system --user-group --home-dir /var/lib/mumble-funk --shell /usr/sbin/nologin mumble-funk
    fi
fi
install -d -m 0755 "$base" "$base/releases" "$base/releases/$release" "$root/etc/systemd/system" "$root/etc/logrotate.d"
install -d -m 0750 "$root/etc/mumble-funk" "$root/var/lib/mumble-funk" "$root/var/log/mumble-funk" "$root/etc/mumble-funk/sounds"
if [[ ! -e $base/releases/$release/mumble-server ]]; then
    install -m 0755 "$binary" "$base/releases/$release/mumble-server"
else
    [[ $(sha256sum "$base/releases/$release/mumble-server" | cut -d ' ' -f 1) == "$release" ]] || {
        echo 'Vorhandene Release-Datei stimmt nicht mit ihrer Prüfsumme überein.' >&2; exit 1;
    }
fi
# Public standards and helper library are separate from user WAVs/configuration.
install -d -m 0755 "$root/usr/local/share/mumble-funk/audio" "$root/usr/local/lib/mumble-funk"
install -m 0644 "$repo/assets/audio/busy-tone.wav" "$repo/assets/audio/squelch-open.wav" "$repo/assets/audio/squelch-close.wav" \
    "$repo/assets/audio/radio-noise.wav" "$repo/assets/audio/LICENSE.md" "$root/usr/local/share/mumble-funk/audio/"
install -m 0644 "$repo/scripts/funk/audio-preview/audio_control.py" "$root/usr/local/lib/mumble-funk/audio_control.py"
branch=$(git -C "$repo" branch --show-current)
revision=$(git -C "$repo" rev-parse HEAD)
experimental=false
if [[ $branch == dev || $branch == feature/squelch-audio || $branch == feature/audio-volume || $branch == feature/busy-tone-asset || $branch == feature/audio-defaults || $branch == feature/radio-noise ]]; then experimental=true; fi
printf 'revision=%s\nbranch=%s\nexperimental=%s\n' "$revision" "$branch" "$experimental" > "$base/releases/$release/BUILD-INFO"
chmod 0644 "$base/releases/$release/BUILD-INFO"
config_existed=false
if [[ -f $root/etc/mumble-funk/server.ini ]]; then config_existed=true; fi
simplex_args=()
if [[ $setup == true ]]; then simplex_args=(--simplex); fi
python3 "$repo/scripts/funk/setup_support.py" create-config "${root_args[@]}" \
    --template "$templates/server.ini" --port "$port" "${simplex_args[@]}"
install -d -m 0755 "$root/usr/local/sbin"
install -m 0755 "$repo/scripts/funk/audio.py" "$root/usr/local/sbin/mumble-funk-audio"
install -m 0755 "$repo/scripts/funk/setup_support.py" "$root/usr/local/sbin/mumble-funk-info"
install -m 0644 "$templates/mumble-funk.service" "$root/etc/systemd/system/mumble-funk.service"
install -m 0644 "$templates/logrotate" "$root/etc/logrotate.d/mumble-funk"
if [[ -z $root ]]; then
    chown root:mumble-funk /etc/mumble-funk /etc/mumble-funk/sounds /etc/mumble-funk/server.ini
    chown mumble-funk:mumble-funk /var/lib/mumble-funk /var/log/mumble-funk
    touch /var/log/mumble-funk/server.log
    chown mumble-funk:mumble-funk /var/log/mumble-funk/server.log
    chmod 0640 /var/log/mumble-funk/server.log
    # Stop only our instance; copy the entire SQLite state while it is stopped.
    systemctl daemon-reload
    systemctl stop mumble-funk.service
    if [[ $config_existed == true ]] || [[ -n $(find /var/lib/mumble-funk -mindepth 1 -maxdepth 1 -print -quit) ]]; then
        backup="/var/backups/mumble-funk/$(date -u +%Y%m%dT%H%M%S)-$$"
        install -d -m 0700 "$backup"
        cp -a /var/lib/mumble-funk "$backup/state"
        cp -a /etc/mumble-funk "$backup/config"
        echo "Sicherung: $backup"
    fi
fi
if [[ -L $base/current ]]; then
    previous=$(readlink -- "$base/current")
    if [[ $previous != "releases/$release" ]]; then
        ln -sfn -- "$previous" "$base/previous"
    fi
fi
ln -sfn -- "releases/$release" "$base/current.new"
mv -Tf -- "$base/current.new" "$base/current"
if [[ -z $root ]]; then
    systemctl daemon-reload
    systemctl enable mumble-funk.service
    log_offset=$(stat -c %s /var/log/mumble-funk/server.log)
    systemctl start mumble-funk.service
    python3 "$repo/scripts/funk/setup_support.py" verify --log-offset "$log_offset"
    systemctl --no-pager --full status mumble-funk.service
    echo 'Installation abgeschlossen. Verbindungsdaten und Zugangspasswort: sudo mumble-funk-info'
else
    echo "Testinstallation erstellt: $root (kein Dienst gestartet)"
fi
