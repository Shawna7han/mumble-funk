#!/usr/bin/env python3
"""Local deployment checks, creation of new configuration, and connection information."""
import argparse
from contextlib import closing
import ast
import ipaddress
import os
from pathlib import Path
import re
import secrets
import socket
import sqlite3
import stat
import subprocess
import sys
import tempfile
import time


class SetupError(RuntimeError):
    pass


def run(*args, check=True):
    result = subprocess.run(args, capture_output=True, text=True, check=False)
    if check and result.returncode:
        raise SetupError(f"Befehl fehlgeschlagen: {args[0]} {args[1] if len(args) > 1 else ''}. "
                         "Bitte Dienststatus und Berechtigungen prüfen.")
    return result.stdout.strip() if result.returncode == 0 else ""


def valid_port(value):
    if not re.fullmatch(r"[0-9]{4,5}", str(value)) or not 1024 <= int(value) <= 65535 or int(value) in (64738, 64739, 64740):
        raise SetupError("Port muss zwischen 1024 und 65535 liegen; 64738, 64739 und 64740 bleiben den Referenzservern vorbehalten.")
    return int(value)


def ini_values(path):
    """Read relevant root/General keys without rewriting the user's INI file."""
    values = {}
    section = "General"
    relevant = {"port", "host", "database", "dbdriver", "dbprefix", "serverpassword", "simplex",
                "simplextimeout", "simplexretrytimeout", "simplexdiagnostics", "audiofeedback",
                "busytone", "busytonefile", "busytonevolume"}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if not line or line.startswith(("#", ";")):
            continue
        if line.startswith("["):
            section = line
            if line == "[General]":
                section = "General"
            continue
        if section != "General" or "=" not in line:
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        key = key.lower()
        if key not in relevant:
            continue
        if key in values:
            raise SetupError(f"Mehrdeutige Konfiguration: {key} kommt mehrfach vor. Keine Änderung vorgenommen.")
        if value.startswith('"') or "\\" in value:
            # Common QSettings quoted strings/escapes; ambiguous encodings fail closed.
            literal = value if value.startswith('"') else '"' + value + '"'
            if any(char not in "\\\"'abfnrtv01234567x" for char in re.findall(r'\\(.)', literal)):
                raise SetupError(f"Die spezielle INI-Kodierung von {key} wird vom Setup nicht unterstützt.")
            try:
                value = ast.literal_eval(literal)
            except (ValueError, SyntaxError) as error:
                raise SetupError(f"INI-Wert {key} kann nicht sicher gelesen werden.") from error
            if not isinstance(value, str):
                raise SetupError(f"INI-Wert {key} muss Text sein.")
        if value.startswith("@@"):
            value = value[1:]
        elif value.startswith("@"):
            raise SetupError(f"Der spezielle QSettings-Wert für {key} wird nicht automatisch verarbeitet.")
        values[key] = value
    return values


class Installation:
    def __init__(self, root=Path("/")):
        self.root = Path(root)
        self.config = self.path("/etc/mumble-funk-audio-engine/server.ini")
        self.log = self.path("/var/log/mumble-funk-audio-engine/server.log")

    def path(self, path):
        return self.root / path.lstrip("/")

    def settings(self):
        values = {"port": "64741", "host": "0.0.0.0", "database": "/var/lib/mumble-funk-audio-engine/server.sqlite",
                  "simplex": "false", "serverpassword": "", "audiofeedback": "false",
                  "busytone": "true", "busytonevolume": "50", "busytonefile": ""}
        if not self.config.exists():
            return values
        values.update(ini_values(self.config))
        if values.get("dbdriver", "QSQLITE").upper() != "QSQLITE":
            raise SetupError("Das automatische Setup unterstützt nur die eigene lokale SQLite-Datenbank.")
        database = Path(values["database"])
        allowed = Path("/var/lib/mumble-funk-audio-engine")
        if not database.is_absolute() or allowed not in database.parents:
            raise SetupError("Datenbank liegt außerhalb /var/lib/mumble-funk-audio-engine. Zum Schutz fremder Daten abgebrochen.")
        actual = self.path(str(database))
        if self.path(str(allowed)).resolve() not in actual.resolve().parents:
            raise SetupError("Der Datenbankpfad verweist auf fremde Daten. Keine Installation vorgenommen.")
        if actual.exists():
            prefix = values.get("dbprefix", "")
            if not re.fullmatch(r"[A-Za-z0-9_]*", prefix):
                raise SetupError("Dieses Datenbankpräfix wird vom Setup nicht unterstützt.")
            try:
                with closing(sqlite3.connect(actual.as_uri() + "?mode=ro", uri=True)) as db:
                    table = prefix + "config"
                    exists = db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table,)).fetchone()
                    if exists:
                        for key, value in db.execute(f'SELECT key, value FROM "{table}" WHERE server_id=1'):
                            if key in {"port", "host", "serverpassword", "simplex", "audiofeedback", "busytone", "busytonevolume", "busytonefile"}:
                                values[key] = str(value)
            except sqlite3.Error as error:
                raise SetupError("Vorhandene SQLite-Datenbank kann nicht sicher gelesen werden; keine Änderung.") from error
        valid_port(values["port"])
        return values

    def service_pid(self):
        value = run("systemctl", "show", "mumble-funk-audio-engine.service", "--property=MainPID", "--value", check=False)
        return int(value) if value.isdigit() else 0

    def check_service_owner(self):
        fragment = run("systemctl", "show", "mumble-funk-audio-engine.service", "--property=FragmentPath", "--value", check=False)
        if not fragment:
            return
        command = run("systemctl", "show", "mumble-funk-audio-engine.service", "--property=ExecStart", "--value")
        expected = "/opt/mumble-funk-audio-engine/current/mumble-server -ini /etc/mumble-funk-audio-engine/server.ini -fg"
        if fragment != "/etc/systemd/system/mumble-funk-audio-engine.service" or expected not in command:
            raise SetupError("mumble-funk-audio-engine.service gehört nicht zur erwarteten Installation. Fremder Dienst bleibt unberührt.")

    def check_port(self, port, owner_pid=0, require_listener=False):
        result = subprocess.run(["ss", "-H", "-lntup", f"( sport = :{port} )"],
                                capture_output=True, text=True, check=False)
        if result.returncode:
            raise SetupError("Portprüfung mit ss fehlgeschlagen. Keine Installation vorgenommen.")
        lines = result.stdout.splitlines()
        for line in lines:
            pids = set(map(int, re.findall(r"pid=(\d+)", line)))
            if not owner_pid or pids != {owner_pid}:
                raise SetupError(f"Port {port} ist bereits durch einen anderen oder nicht eindeutig erkannten Prozess belegt. "
                                 "Kein Prozess wurde gestoppt. Für eine neue Installation: setup.sh --port 64741")
        if require_listener:
            transports = {line.split()[0] for line in lines if line.split()}
            if not {"tcp", "udp"}.issubset(transports):
                raise SetupError(f"Der eigene Dienst lauscht noch nicht auf TCP und UDP Port {port}.")
        elif not lines:
            # Catch bound sockets that are not listening (and races with ss).
            # Wildcard addresses are intentional: a loopback-only bind would miss
            # conflicts on other local addresses. These short-lived probes never
            # listen, accept, send or receive application data and close immediately.
            # See docs/funk/AUDIO-SECURITY-REVIEW.md; do not weaken this collision check.
            for family, address in ((socket.AF_INET, "0.0.0.0"), (socket.AF_INET6, "::")):
                for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
                    try:
                        with socket.socket(family, kind) as probe:
                            if family == socket.AF_INET6:
                                probe.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
                            probe.bind((address, port))
                    except OSError as error:
                        if family == socket.AF_INET6 and error.errno in (97, 99):
                            continue  # IPv6 disabled on this host.
                        raise SetupError(f"Port {port} kann nicht sicher belegt werden. Kein Dienst wurde verändert.") from error

    def preflight(self, requested_port=None):
        exists = self.config.exists()
        if not exists:
            state = self.path("/var/lib/mumble-funk-audio-engine")
            if ((state.exists() and any(state.iterdir()))
                    or self.path("/etc/systemd/system/mumble-funk-audio-engine.service").exists()
                    or self.path("/opt/mumble-funk-audio-engine/current").exists()):
                raise SetupError("Funkserver-Dateien vorhanden, aber server.ini fehlt. Bitte Sicherung wiederherstellen; "
                                 "es wird kein neues Passwort erzeugt.")
        settings = self.settings()
        port = valid_port(requested_port if requested_port is not None else settings["port"])
        if exists and port != int(settings["port"]):
            raise SetupError("Eine vorhandene Installation behält ihren Port. --port darf sie nicht umkonfigurieren.")
        if self.root == Path("/"):
            self.check_service_owner()
            self.check_port(port, self.service_pid() if exists else 0)
        return port

    def create_config(self, template, port, simplex):
        if self.config.exists():
            return
        text = Path(template).read_text()
        text = text.replace("@PASSWORD@", secrets.token_hex(16)).replace("port=64741\n", f"port={valid_port(port)}\n")
        if simplex:
            settings = {"simplex": "true", "simplextimeout": "500", "simplexretrytimeout": "500",
                        "simplexdiagnostics": "false", "simplexlogfile": "/var/log/mumble-funk-audio-engine/simplex",
                        "simplexlogmaxsize": "10485760", "simplexloggrants": "true",
                        "simplexlogdenials": "true", "simplexlogreleases": "true"}
            for key, value in settings.items():
                text, count = re.subn(rf"(?m)^#{key}=.*$", f"{key}={value}", text)
                if count != 1:
                    raise SetupError(f"INI-Vorlage enthält keinen eindeutigen Platzhalter für {key}.")
        # Publish a complete private file atomically; do not replace an existing one.
        fd, temporary = tempfile.mkstemp(prefix=".server.ini.", dir=self.config.parent)
        try:
            with os.fdopen(fd, "w") as file:
                os.fchmod(file.fileno(), 0o640)
                file.write(text)
                file.flush()
                os.fsync(file.fileno())
            try:
                os.link(temporary, self.config)
            except FileExistsError:
                pass
        finally:
            os.unlink(temporary)

    def verify(self, log_offset, deadline=15):
        settings = self.settings()
        port = valid_port(settings["port"])
        end = time.monotonic() + deadline
        error = SetupError("Dienststart konnte nicht bestätigt werden.")
        while time.monotonic() < end:
            try:
                if run("systemctl", "is-active", "mumble-funk-audio-engine.service", check=False) != "active":
                    raise SetupError("mumble-funk-audio-engine.service ist nicht aktiv.")
                if run("systemctl", "is-enabled", "mumble-funk-audio-engine.service", check=False) != "enabled":
                    raise SetupError("mumble-funk-audio-engine.service ist nicht für den Systemstart aktiviert.")
                pid = self.service_pid()
                if not pid:
                    raise SetupError("Der Dienst hat keinen laufenden Hauptprozess.")
                self.check_port(port, pid, require_listener=True)
                with self.log.open("rb") as file:
                    file.seek(log_offset if self.log.stat().st_size >= log_offset else 0)
                    startup = file.read().decode("utf-8", errors="replace")
                if "Server listening on" not in startup:
                    raise SetupError("Eine neue Startmeldung fehlt im Funkserver-Log.")
                active = "Simplex enabled:" in startup
                expected = settings.get("simplex", "false").lower() in ("true", "1")
                if active != expected:
                    raise SetupError("Simplex-Startmeldung passt nicht zur Konfiguration. Bitte server.log prüfen.")
                feedback = (expected and settings.get("audiofeedback", "false").lower() in ("true", "1")
                            and settings.get("busytone", "true").lower() in ("true", "1")
                            and int(settings.get("busytonevolume", "50")) > 0)
                if ("Experimental audio feedback enabled:" in startup) != feedback:
                    raise SetupError("Audio-Feedback-Startmeldung passt nicht zur Konfiguration; server.log prüfen.")
                return
            except (SetupError, OSError) as caught:
                error = caught
                time.sleep(0.2)
        raise SetupError(f"Startprüfung fehlgeschlagen: {error} Bitte sudo systemctl status mumble-funk-audio-engine und server.log prüfen.")

    def show_password(self):
        # An explicit administrator request to the controlling terminal only.
        # Never route a credential through stdout/stderr, pipes or setup logs.
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            raise SetupError("Passwortanzeige nur im interaktiven Terminal ohne Ausgabeumleitung erlaubt.")
        # Open an existing device only: never create/truncate a file or follow
        # an unexpected symlink. Verify the descriptor before writing any secret.
        descriptor = os.open("/dev/tty", os.O_WRONLY | os.O_NOCTTY | os.O_NOFOLLOW | os.O_CLOEXEC)
        try:
            if not stat.S_ISCHR(os.fstat(descriptor).st_mode) or not os.isatty(descriptor):
                raise SetupError("Kein interaktives Terminal für die Passwortanzeige vorhanden.")
            password = self.settings().get("serverpassword", "")
            display = ("Zugangspasswort: " + (password or "(kein Zugangspasswort konfiguriert)") + "\n").encode("utf-8")
            while display:
                written = os.write(descriptor, display)
                if written <= 0:
                    raise SetupError("Passwort konnte nicht am Terminal angezeigt werden.")
                display = display[written:]
        finally:
            os.close(descriptor)

    def info(self, hide_password=True):
        # Keep the old argument for callers, but never put secrets in this report.
        # Explicit interactive disclosure is handled separately by show_password().
        if not self.config.exists():
            raise SetupError("Keine Mumble-Funk-Konfiguration gefunden.")
        settings = self.settings()
        status = run("systemctl", "is-active", "mumble-funk-audio-engine.service", check=False)
        candidates = []
        for item in run("hostname", "-I", check=False).split():
            try:
                address = ipaddress.ip_address(item)
                if address.version == 4 and not address.is_loopback and not address.is_link_local:
                    candidates.append(str(address))
            except ValueError:
                pass
        host = settings.get("host", "0.0.0.0")
        if host not in ("0.0.0.0", "", "::"):
            candidates = [host]
        enabled = settings.get("simplex", "false").lower() in ("true", "1")
        print("\n========================================")
        print(" MUMBLE-FUNK VERBINDUNGSINFORMATIONEN")
        print("========================================")
        print("Server:        Mumble-Funk")
        print("Version:       Audio-Engine Phase 1 (experimentell) / Mumble 1.5.939")
        print(f"Betrieb:       {'Aktiv' if status == 'active' else 'Nicht aktiv'}")
        print(f"Simplex:       {'Aktiviert' if enabled else 'Deaktiviert (bestehende Einstellung erhalten)'}")
        print(f"IP-Adresse(n): {', '.join(dict.fromkeys(candidates)) or 'Nicht eindeutig erkannt; IP der Ubuntu-VM verwenden.'}")
        if len(set(candidates)) > 1:
            print("               Mehrere Adressen: die vom Endgerät erreichbare Adresse der VM wählen.")
        print("               Lokale Adressen; keine öffentliche Internetadresse ermittelt.")
        print(f"Port:          {settings['port']}")
        print("Protokoll:     TCP + UDP")
        print("Systemdienst:  mumble-funk-audio-engine")
        print(f"Audiofeedback: {settings.get('audiofeedback', 'false')}")
        print(f"Busy Tone:     {settings.get('busytone', 'true')}; Lautstärke {settings.get('busytonevolume', '50')}%")
        print(f"Signaldatei:   {settings.get('busytonefile') or '(synthetischer Standardton)'}")
        print("Zugangspasswort: verborgen; im Terminal mit sudo mumble-funk-audio-engine-info --show-password anzeigen")
        print("========================================")
        print("In Mumla/Windows-Mumble: Adresse, Port, eigenen Benutzernamen und Zugangspasswort eintragen.")
        print("Das Zugangspasswort verleiht keine Administratorrechte.")
        print("Router/Proxmox-/Ubuntu-Firewall ggf. separat für diesen TCP- und UDP-Port freigeben.")
        print("Es wurden keine Firewall- oder Routerregeln geändert.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["preflight", "create-config", "verify", "info"], nargs="?", default="info")
    parser.add_argument("--root", type=Path, default=Path("/"), help=argparse.SUPPRESS)
    parser.add_argument("--port")
    password_options = parser.add_mutually_exclusive_group()
    password_options.add_argument("--hide-password", action="store_true", help="Kompatibel: Passwort bleibt verborgen (Standard)")
    password_options.add_argument("--show-password", action="store_true", help="Passwort ausdrücklich nur im interaktiven Terminal anzeigen")
    parser.add_argument("--template", type=Path)
    parser.add_argument("--simplex", action="store_true")
    parser.add_argument("--log-offset", type=int, default=0)
    args = parser.parse_args()
    if args.show_password and args.command != "info":
        parser.error("--show-password ist nur für info erlaubt")
    if args.root == Path("/") and os.geteuid() != 0:
        parser.error("Bitte mit sudo ausführen, z. B. sudo mumble-funk-audio-engine-info")
    instance = Installation(args.root)
    try:
        if args.command == "preflight":
            print(instance.preflight(args.port))
        elif args.command == "create-config":
            if args.template is None or args.port is None:
                parser.error("Vorlage und Port fehlen")
            instance.create_config(args.template, args.port, args.simplex)
        elif args.command == "verify":
            instance.verify(args.log_offset)
        else:
            instance.info()
            if args.show_password:
                instance.show_password()
    except (SetupError, OSError) as error:
        print(f"Mumble-Funk: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
