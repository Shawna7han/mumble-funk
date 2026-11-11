#!/usr/bin/env bash
# Only deploys our instance. Never runs apt or cmake --install.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
templates="$repo/scripts/funk/audio-preview/ubuntu"
# shellcheck source=scripts/funk/platform.sh
source "$repo/scripts/funk/platform.sh"
root=
binary=
setup=false
port_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage)
            [[ $# -ge 3 ]] || { echo 'Stage-Ziel und Serverbinary fehlen.' >&2; exit 1; }
            root=$(realpath -m -- "$2")
            [[ $root != / ]] || { echo 'Stage darf nicht / sein.' >&2; exit 1; }
            binary=$(realpath -- "$3"); shift 3 ;;
        --setup) setup=true; shift ;;
        --port)
            [[ $# -ge 2 ]] || { echo 'Port fehlt.' >&2; exit 1; }
            port_args=(--port "$2"); shift 2 ;;
        *) echo 'Aufruf: install.sh [--setup] [--port PORT] [--stage TESTZIEL SERVERBINARY]' >&2; exit 1 ;;
    esac
done
root_args=()
if [[ -n $root ]]; then
    root_args=(--root "$root")
else
    [[ $EUID == 0 ]] || { echo 'Bitte mit sudo bash scripts/funk/audio-preview/install.sh starten.' >&2; exit 1; }
    # OS-provided file, not part of the repository.
    # shellcheck source=/dev/null
    . /etc/os-release
    funk_require_supported_os "${ID:-}" "${VERSION_ID:-}" || exit 1
    exec 9>/run/lock/mumble-funk-audio-engine-install.lock
    flock -n 9 || { echo 'Eine andere Funkserver-Installation läuft bereits.' >&2; exit 1; }
    [[ $(git -C "$repo" branch --show-current) == feature/audio-engine ]] || {
        echo 'Experimentelle Installation nur aus feature/audio-engine.' >&2; exit 1;
    }
    binary="$repo/build/audio-preview/mumble-server"
fi
command -v python3 >/dev/null || { echo 'python3 fehlt; bitte setup.sh verwenden.' >&2; exit 1; }
port=$(python3 "$repo/scripts/funk/audio-preview/setup_support.py" preflight "${root_args[@]}" "${port_args[@]}")
[[ -x $binary ]] || { echo "Serverbinary fehlt: $binary" >&2; exit 1; }
version=$("$binary" --version 2>&1)
[[ $version == *1.5.939* ]] || { echo 'Erwartet wird Mumble 1.5.939.' >&2; exit 1; }
release=$(sha256sum -- "$binary" | cut -d ' ' -f 1)
base="$root/opt/mumble-funk-audio-engine"
if [[ -z $root ]]; then
    if ! id mumble-funk-audio-engine >/dev/null 2>&1; then
        useradd --system --user-group --home-dir /var/lib/mumble-funk-audio-engine --shell /usr/sbin/nologin mumble-funk-audio-engine
    fi
fi
install -d -m 0755 "$base" "$base/releases" "$base/releases/$release" "$root/etc/systemd/system" "$root/etc/logrotate.d"
install -d -m 0750 "$root/etc/mumble-funk-audio-engine" "$root/etc/mumble-funk-audio-engine/sounds" "$root/var/lib/mumble-funk-audio-engine" "$root/var/log/mumble-funk-audio-engine"
if [[ ! -e $base/releases/$release/mumble-server ]]; then
    install -m 0755 "$binary" "$base/releases/$release/mumble-server"
else
    [[ $(sha256sum "$base/releases/$release/mumble-server" | cut -d ' ' -f 1) == "$release" ]] || {
        echo 'Vorhandene Release-Datei stimmt nicht mit ihrer Prüfsumme überein.' >&2; exit 1;
    }
fi
# Ship the existing project license and the newly linked codec's notices.
install -d -m 0755 "$base/releases/$release/licenses"
install -m 0644 "$repo/LICENSE" "$base/releases/$release/licenses/LICENSE"
install -m 0644 "$repo/3rdPartyLicenses/libopus_license.txt" "$base/releases/$release/licenses/Opus.txt"
config_existed=false
if [[ -f $root/etc/mumble-funk-audio-engine/server.ini ]]; then config_existed=true; fi
simplex_args=()
if [[ $setup == true ]]; then simplex_args=(--simplex); fi
python3 "$repo/scripts/funk/audio-preview/setup_support.py" create-config "${root_args[@]}" \
    --template "$templates/server.ini" --port "$port" "${simplex_args[@]}"
install -d -m 0755 "$root/usr/local/sbin"
install -m 0755 "$repo/scripts/funk/audio-preview/audio_control.py" "$root/usr/local/sbin/mumble-funk-audio-engine"
install -m 0755 "$repo/scripts/funk/audio-preview/setup_support.py" "$root/usr/local/sbin/mumble-funk-audio-engine-info"
install -m 0644 "$templates/mumble-funk-audio-engine.service" "$root/etc/systemd/system/mumble-funk-audio-engine.service"
install -m 0644 "$templates/logrotate" "$root/etc/logrotate.d/mumble-funk-audio-engine"
if [[ -z $root ]]; then
    chown root:mumble-funk-audio-engine /etc/mumble-funk-audio-engine /etc/mumble-funk-audio-engine/sounds /etc/mumble-funk-audio-engine/server.ini
    chown mumble-funk-audio-engine:mumble-funk-audio-engine /var/lib/mumble-funk-audio-engine /var/log/mumble-funk-audio-engine
    touch /var/log/mumble-funk-audio-engine/server.log
    chown mumble-funk-audio-engine:mumble-funk-audio-engine /var/log/mumble-funk-audio-engine/server.log
    chmod 0640 /var/log/mumble-funk-audio-engine/server.log
    # Stop only our instance; copy the entire SQLite state while it is stopped.
    systemctl daemon-reload
    systemctl stop mumble-funk-audio-engine.service
    if [[ $config_existed == true ]] || [[ -n $(find /var/lib/mumble-funk-audio-engine -mindepth 1 -maxdepth 1 -print -quit) ]]; then
        backup="/var/backups/mumble-funk-audio-engine/$(date -u +%Y%m%dT%H%M%S)-$$"
        install -d -m 0700 "$backup"
        cp -a /var/lib/mumble-funk-audio-engine "$backup/state"
        cp -a /etc/mumble-funk-audio-engine "$backup/config"
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
    systemctl enable mumble-funk-audio-engine.service
    log_offset=$(stat -c %s /var/log/mumble-funk-audio-engine/server.log)
    systemctl start mumble-funk-audio-engine.service
    python3 "$repo/scripts/funk/audio-preview/setup_support.py" verify --log-offset "$log_offset"
    systemctl --no-pager --full status mumble-funk-audio-engine.service
    echo 'Installation abgeschlossen. Verbindungsdaten: sudo mumble-funk-audio-engine-info'
    echo 'Passwort nur im interaktiven Terminal: sudo mumble-funk-audio-engine-info --show-password'
else
    echo "Testinstallation erstellt: $root (kein Dienst gestartet)"
fi
