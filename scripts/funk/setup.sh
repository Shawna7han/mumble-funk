#!/usr/bin/env bash
# Run as a normal user. Root operations are limited to apt and deployment checks/install.
set -Eeuo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# shellcheck source=scripts/funk/platform.sh
source "$repo/scripts/funk/platform.sh"
step='Vorprüfung'
report_error() {
    local result=$?
    printf '\nMumble-Funk: Schritt "%s" fehlgeschlagen (Code %s). Setup angehalten.\n' "$step" "$result" >&2
}
trap report_error ERR
port_args=()
source_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --experimental-squelch|--experimental-audio-volume|--experimental-busy-tone|--experimental-audio-defaults|--experimental-dev|--experimental-radio-noise) source_args=("$1"); shift ;;
        --port)
            [[ $# -ge 2 ]] || { echo 'Nach --port fehlt die Portnummer.' >&2; exit 1; }
            if [[ ! $2 =~ ^[0-9]{4,5}$ ]] || ((10#$2 < 1024 || 10#$2 > 65535 || 10#$2 == 64738)); then
                echo 'Port: 1024 bis 65535, außer Standard-Mumble-Port 64738.' >&2; exit 1
            fi
            port_args=(--port "$2"); shift 2 ;;
        --help|-h)
            echo 'Aufruf als normaler Benutzer: bash scripts/funk/setup.sh [--port PORT] [--experimental-squelch|--experimental-audio-volume|--experimental-busy-tone|--experimental-audio-defaults|--experimental-dev|--experimental-radio-noise]'
            echo 'Bestehende Einstellungen werden erhalten. Kein automatisches Git-Update.'
            exit 0 ;;
        *) echo "Unbekannte Option: $1" >&2; exit 1 ;;
    esac
done
[[ $EUID != 0 ]] || { echo 'Bitte als normaler Benutzer starten, ohne sudo vor setup.sh. Der Build läuft nicht als root.' >&2; exit 1; }
# OS-provided file, not part of the repository.
# shellcheck source=/dev/null
. /etc/os-release
funk_require_supported_os "${ID:-}" "${VERSION_ID:-}"
# shellcheck source=scripts/funk/source.sh
source "$repo/scripts/funk/source.sh"
funk_require_supported_source "$repo" "${source_args[@]}"
command -v sudo >/dev/null || { echo 'sudo fehlt. Bitte einen Ubuntu-Benutzer mit sudo-Rechten verwenden.' >&2; exit 1; }
if ! command -v systemctl >/dev/null || ! systemctl show --property=Version --value >/dev/null; then
    echo 'Ein laufendes systemd wird benötigt. Das Setup ist für Ubuntu-Server/VMs vorgesehen.' >&2; exit 1
fi
# Prevent two setup runs by the same user; install.sh also locks deployment globally.
exec 8>"$(git -C "$repo" rev-parse --path-format=absolute --git-path funk-setup.lock)"
flock -n 8 || { echo 'In diesem Quellverzeichnis läuft bereits ein Setup.' >&2; exit 1; }
ram_kib=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)
swap_kib=$(awk '/^SwapTotal:/ {print $2}' /proc/meminfo)
free_kib=$(df -Pk -- "$repo" | awk 'NR==2 {print $4}')
((ram_kib + swap_kib >= 2097152)) || {
    echo 'Zum Kompilieren werden mindestens 2 GiB RAM einschließlich Swap benötigt (4 GiB RAM empfohlen).' >&2; exit 1;
}
((free_kib >= 8388608)) || { echo 'Mindestens 8 GiB freier Speicher im Quellverzeichnis erforderlich.' >&2; exit 1; }
jobs=${FUNK_BUILD_JOBS:-2}
[[ $jobs =~ ^[1-9][0-9]?$ ]] || { echo 'FUNK_BUILD_JOBS muss eine Zahl von 1 bis 99 sein.' >&2; exit 1; }
if ((ram_kib < 4194304)) && [[ -z ${FUNK_BUILD_JOBS:-} ]]; then
    jobs=1
    echo 'Weniger als 4 GiB RAM: Build mit einem Prozess; dies dauert länger.'
fi
printf 'Ubuntu %s; RAM %.0f MiB; freier Speicher %.0f MiB; Build-Prozesse: %s\n' \
    "$VERSION_ID" "$((ram_kib / 1024))" "$((free_kib / 1024))" "$jobs"
step='Administratorrechte'
sudo -v
apt_updated=false
install_missing() {
    local package
    local missing=()
    for package in "$@"; do
        if [[ $(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true) != 'install ok installed' ]]; then
            missing+=("$package")
        fi
    done
    if ((${#missing[@]})); then
        if [[ $apt_updated == false ]]; then
            sudo env DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=l apt-get update
            apt_updated=true
        fi
        sudo env DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=l \
            apt-get install --yes --no-install-recommends "${missing[@]}"
    fi
}
step='Grundwerkzeuge installieren'
install_missing python3 iproute2 util-linux
step='Vorhandene Installation und Port prüfen'
selected_port=$(sudo python3 "$repo/scripts/funk/setup_support.py" preflight "${port_args[@]}")
echo "Funkserver-Port: $selected_port TCP/UDP. Bestehende Einstellungen werden erhalten."
step='Build-Abhängigkeiten installieren'
install_missing git ca-certificates build-essential cmake ninja-build pkg-config \
    qtbase5-dev qtbase5-dev-tools libqt5sql5-sqlite libboost-dev libssl-dev \
    libopus-dev libprotobuf-dev protobuf-compiler libprotoc-dev libcap-dev \
    libavahi-compat-libdnssd-dev libzeroc-ice-dev openssl logrotate
step='Git-Submodule initialisieren'
git -C "$repo" submodule update --init --recursive
step='Server kompilieren und alle Tests ausführen'
FUNK_BUILD_JOBS="$jobs" bash "$repo/scripts/funk/build.sh" "${source_args[@]}"
step='Eigenen Funkdienst installieren und Start prüfen'
sudo bash "$repo/scripts/funk/install.sh" --setup --port "$selected_port" "${source_args[@]}"
printf '\n========================================\n MUMBLE-FUNK INSTALLATION ERFOLGREICH\n========================================\n'
echo 'Der Dienst ist aktiv, startet automatisch und lauscht auf TCP und UDP.'
echo 'Simplex wurde anhand der neuen Startmeldungen geprüft; vorhandene Einstellungen bleiben erhalten.'
echo 'Verbindungsdaten und Zugangspasswort lokal anzeigen:'
echo '  sudo mumble-funk-info'
echo 'In Mumla/Windows-Mumble die dort angezeigte Adresse, den Port und das Passwort verwenden.'
echo 'Router und Firewalls wurden nicht verändert. Ggf. den gewählten Port für TCP und UDP freigeben.'
sudo /usr/local/sbin/mumble-funk-info --hide-password
