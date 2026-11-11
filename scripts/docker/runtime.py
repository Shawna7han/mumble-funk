#!/usr/bin/env python3
"""Rootless lifecycle and offline backups for the isolated Mumble-Funk container."""
import contextlib
import fcntl
import getpass
import hashlib
import re
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import secrets
import shutil
import socket
import sqlite3
import ssl
import struct
import subprocess
import sys
import tarfile
import tempfile
import time

ROOT = Path(os.environ.get('FUNK_TEST_ROOT', '/'))
DIRECTORIES = {name: ROOT / path for name, path in {
    'config': 'etc/mumble-funk', 'state': 'var/lib/mumble-funk', 'logs': 'var/log/mumble-funk'}.items()}
CONFIG = DIRECTORIES['config'] / 'server.ini'
DATABASE = DIRECTORIES['state'] / 'server.sqlite'
BINARY = '/usr/local/bin/mumble-server'


def settings(path=CONFIG):
    result = {}
    section = 'General'
    for raw in path.read_text(encoding='utf-8-sig').splitlines():
        line = raw.strip()
        if line.startswith('['):
            section = 'General' if line == '[General]' else line
        if section != 'General' or not line or line.startswith(('#', ';', '[')) or '=' not in line:
            continue
        key, value = (part.strip() for part in line.split('=', 1))
        key = key.lower()
        if key in result:
            raise RuntimeError('Doppelter INI-Schlüssel: '+key)
        result[key] = json.loads(value) if value.startswith('"') else value
    if result.get('port') != '64739' or result.get('database') != '/var/lib/mumble-funk/server.sqlite':
        raise RuntimeError('Docker benötigt intern Port 64739 und die eigene /var/lib/mumble-funk/server.sqlite.')
    if result.get('dbdriver', 'QSQLITE') != 'QSQLITE' or result.get('dbprefix', ''):
        raise RuntimeError('Diese Docker-Verwaltung unterstützt die eigene SQLite-Datenbank ohne Präfix.')
    return result


def write_private(path, data):
    fd, name = tempfile.mkstemp(prefix='.config-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as file:
            os.fchmod(file.fileno(), 0o600)
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        os.replace(name, path)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(name)


@contextlib.contextmanager
def instance_lock():
    # Never remove/replace this inode. The lock also protects offline operations.
    descriptor = os.open(DIRECTORIES['state'] / '.instance.lock', os.O_CREAT | os.O_RDWR, 0o600)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError('Instanz läuft noch oder wird bereits verwaltet; zuerst stoppen.') from error
        yield descriptor
    finally:
        os.close(descriptor)


def initialize():
    pending = DIRECTORIES['config'] / '.bootstrap.pending'
    if not CONFIG.exists():
        existing = [p for p in DIRECTORIES['state'].iterdir() if p.name not in ('.instance.lock', 'audio')]
        if existing:
            raise RuntimeError('Konfiguration fehlt, aber Daten existieren. Bitte die Konfiguration wiederherstellen.')
        pending.touch(mode=0o600)
        password = secrets.token_hex(24)
        text = f'''# Generated once. Updates never rewrite this file.
database=/var/lib/mumble-funk/server.sqlite
host=0.0.0.0
port=64739
users=20
bandwidth=72000
serverpassword={password}
welcometext="Mumble-Funk Simplex v0.2 (Docker)"
registerName=Mumble-Funk
ice=
bonjour=false
logfile=
logdays=-1
radionoise=true
radionoisefile=/usr/local/share/mumble-funk/audio/radio-noise.wav
radionoisevolume=20
simplex=true
simplextimeout=500
simplexretrytimeout=500
simplexdiagnostics=true
simplexlogfile=/var/log/mumble-funk/simplex
simplexlogmaxsize=10485760
voicediagnostics=false
voicediagnosticsfile=/var/log/mumble-funk/voice
audiofeedback=true
busytone=true
busytonefile=/usr/local/share/mumble-funk/audio/busy-tone.wav
busytonevolume=50
feedbackboostdb=8
squelch=true
squelchopen=true
squelchclose=true
squelchclosetimeoutfallback=true
squelchopenfile=/usr/local/share/mumble-funk/audio/squelch-open.wav
squelchclosefile=/usr/local/share/mumble-funk/audio/squelch-close.wav
squelchopenvolume=50
squelchclosevolume=50
'''
        write_private(CONFIG, text.encode())
    settings()
    if not DATABASE.exists():
        if not pending.exists():
            raise RuntimeError('Datenbank fehlt bei vorhandener Konfiguration. Keine automatische Neuerstellung.')
        # Pre-create SuperUser with password login disabled. Otherwise stock Mumble
        # generates and prints a SuperUser password during its first normal start.
        result = subprocess.run([BINARY, '-ini', str(CONFIG), '-disablesu'], capture_output=True)
        if result.returncode:
            raise RuntimeError('Initialisierung der eigenen Datenbank fehlgeschlagen; Rechte/Platz prüfen.')
    pending.unlink(missing_ok=True)


def integrity(database=DATABASE):
    with contextlib.closing(sqlite3.connect(database.resolve().as_uri()+'?mode=ro', uri=True)) as db:
        if db.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
            raise RuntimeError('SQLite-Integritätsprüfung fehlgeschlagen.')


def backup():
    with instance_lock():
        settings()
        integrity()
        with tarfile.open(fileobj=sys.stdout.buffer, mode='w|gz') as archive:
            for name, directory in DIRECTORIES.items():
                def keep(info):
                    if info.name in ('state/.instance.lock', 'config/.bootstrap.pending'):
                        return None
                    if not (info.isfile() or info.isdir()):
                        raise RuntimeError('Backup enthält symbolische Links/Sonderdateien; bitte prüfen.')
                    return info
                archive.add(directory, arcname=name, filter=keep)


def restore():
    with instance_lock():
        for name, directory in DIRECTORIES.items():
            for path in directory.iterdir():
                if name == 'state' and (path.name == '.instance.lock' or path.name == 'audio' and path.is_dir() and not any(path.iterdir())):
                    continue
                raise RuntimeError('Wiederherstellung nur in leere Ziel-Volumes; vorhandene Daten bleiben erhalten.')
        with tempfile.TemporaryDirectory(prefix='.restore-', dir=DIRECTORIES['state']) as temporary:
            stage = Path(temporary)
            compressed = stage / 'backup.tar.gz'
            with compressed.open('wb') as file:
                size = 0
                while chunk := sys.stdin.buffer.read(1024*1024):
                    size += len(chunk)
                    if size > 2*1024**3:
                        raise RuntimeError('Backup größer als 2 GiB komprimiert.')
                    file.write(chunk)
            extract = stage / 'extracted'
            extract.mkdir()
            with tarfile.open(compressed, mode='r:gz') as archive:
                size = 0
                seen = set()
                for index, member in enumerate(archive):
                    parts = PurePosixPath(member.name).parts
                    size += member.size
                    if (index > 100000 or size > 8*1024**3 or not parts or parts[0] not in DIRECTORIES
                            or member.name.startswith('/') or '..' in parts or member.name in seen
                            or not (member.isfile() or member.isdir()) or member.size < 0
                            or member.name == 'state/.instance.lock'):
                        raise RuntimeError('Ungültiges/zu großes Backup; keine Zieldaten geändert.')
                    seen.add(member.name)
                    target = extract.joinpath(*parts)
                    if member.isdir():
                        target.mkdir(parents=True, exist_ok=True)
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with archive.extractfile(member) as source, target.open('xb') as output:
                            shutil.copyfileobj(source, output, 1024*1024)
                        target.chmod(0o600)
            if not all((extract / name).is_dir() for name in DIRECTORIES):
                raise RuntimeError('Backup ist unvollständig; keine Zieldaten geändert.')
            settings(extract / 'config/server.ini')
            integrity(extract / 'state/server.sqlite')
            # Publish configuration last. Source image, host paths and SQLite files
            # outside these initially empty volumes can never be overwritten.
            for name in ('state', 'logs', 'config'):
                source = extract / name
                if not source.is_dir():
                    raise RuntimeError('Backup ist unvollständig.')
                for item in source.iterdir():
                    target = DIRECTORIES[name] / item.name
                    if item.name == 'audio' and target.is_dir() and not any(target.iterdir()):
                        target.rmdir()
                    shutil.move(str(item), str(target))
        print('Backup in leere Docker-Volumes wiederhergestellt.', file=sys.stderr)


def health():
    settings()
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    # This is a local liveness probe of our self-signed server, not remote trust.
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    with socket.create_connection(('127.0.0.1', 64739), timeout=2) as raw:
        with context.wrap_socket(raw, server_hostname='localhost') as tls:
            header = b''
            while len(header) < 6:
                chunk = tls.recv(6-len(header))
                if not chunk:
                    raise RuntimeError('Kein Mumble-Version-Header.')
                header += chunk
            kind, length = struct.unpack('!HI', header)
            if kind != 0 or not 0 < length <= 8192:
                raise RuntimeError('Ungültiger Mumble-Version-Header.')
    stamp = struct.pack('!Q', time.monotonic_ns() & ((1 << 64)-1))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.settimeout(2)
        udp.sendto(b'\0'*4+stamp, ('127.0.0.1', 64739))
        reply = udp.recv(64)
        if len(reply) != 24 or reply[4:12] != stamp:
            raise RuntimeError('Kein gültiger lokaler Mumble-UDP-Ping.')


def info(as_json=False):
    values = settings()
    if DATABASE.exists():
        with contextlib.closing(sqlite3.connect(DATABASE.resolve().as_uri()+'?mode=ro', uri=True)) as db:
            for key, value in db.execute("SELECT key,value FROM config WHERE server_id=1"):
                if key in ('serverpassword','port','simplex'):
                    values[key] = str(value)
    if as_json:
        print(json.dumps(values))
    else:
        print('Mumble-Funk Simplex v0.2 — Docker')
        print('Adresse: IP-Adresse deines Docker-Hosts')
        print('Interner Port: '+values['port']+' TCP/UDP')
        print('Host-Port anzeigen: docker compose port mumble-funk 64739')
        print('Simplex: '+values.get('simplex','false'))
        print('Zugangspasswort: '+values.get('serverpassword',''))
        print('Dieses Passwort verleiht keine Administratorrechte.')


def configure(key, value):
    allowed = {'simplex': ('true','false'), 'simplexdiagnostics': ('true','false'), 'voicediagnostics': ('true','false')}
    if key in allowed:
        valid = value in allowed[key]
    else:
        valid = key in ('simplextimeout','simplexretrytimeout') and value.isdecimal() and 50 <= int(value) <= 60000
    if not valid:
        raise RuntimeError('Erlaubt: simplex/simplexdiagnostics/voicediagnostics true|false oder Timeout 50..60000 ms.')
    with instance_lock():
        settings()
        lines = CONFIG.read_text().splitlines()
        found = False
        section = 'General'
        for i, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith('['):
                section = 'General' if stripped == '[General]' else stripped
            if (section == 'General' and '=' in stripped and not stripped.startswith(('#',';'))
                    and stripped.split('=',1)[0].strip().lower() == key):
                lines[i] = key+'='+value
                found = True
        if not found:
            lines.insert(0,key+'='+value)
        write_private(CONFIG, ('\n'.join(lines)+'\n').encode())


def admin_password():
    with instance_lock():
        settings()
        integrity()
        password = getpass.getpass('Neues SuperUser-Passwort (mindestens 12 ASCII-Zeichen): ')
        if (not 12 <= len(password) <= 200 or any(ord(c) < 32 or ord(c) > 126 for c in password)
                or password != getpass.getpass('Passwort wiederholen: ')):
            raise RuntimeError('Passwort muss übereinstimmen und aus 12..200 druckbaren ASCII-Zeichen bestehen.')
        result = subprocess.run([BINARY, '-ini', str(CONFIG), '-readsupw'],
                                input=(password+'\n').encode('ascii'), capture_output=True)
        if result.returncode:
            raise RuntimeError('SuperUser-Passwort konnte nicht gesetzt werden.')
        print('SuperUser-Passwort gesetzt. Es wurde nicht in Konfiguration oder Logs geschrieben.')


def rollback_audio(expected_revision):
    """Restore only our last audio change, never a concurrently changed INI."""
    if not re.fullmatch(r'[0-9a-f]{64}', expected_revision):
        raise RuntimeError('Ungültige Audio-Konfigurationsrevision.')
    with instance_lock():
        settings()
        if hashlib.sha256(CONFIG.read_bytes()).hexdigest() != expected_revision:
            raise RuntimeError('Konfiguration inzwischen verändert; automatische Wiederherstellung abgelehnt.')
        previous = CONFIG.with_name('server.ini.before-audio-change')
        if previous.is_symlink() or not previous.is_file():
            raise RuntimeError('Keine sichere vorige Audio-Konfiguration vorhanden.')
        settings(previous)
        write_private(CONFIG, previous.read_bytes())
    print('Vorige Audio-Konfiguration wiederhergestellt.')


def main():
    os.umask(0o077)
    command = sys.argv[1] if len(sys.argv)>1 else 'serve'
    try:
        if command == 'serve':
            with instance_lock() as descriptor:
                initialize()
                os.set_inheritable(descriptor, True)
                os.execv(BINARY, [BINARY, '-ini', str(CONFIG), '-fg'])
        elif command == 'info':
            info('--json' in sys.argv[2:])
        elif command == 'health':
            health()
        elif command == 'backup':
            backup()
        elif command == 'restore':
            restore()
        elif command == 'admin-password':
            admin_password()
        elif command == 'audio-rollback' and len(sys.argv) == 3:
            rollback_audio(sys.argv[2])
        elif command == 'audio':
            settings()  # Validate this container's own configuration without rewriting it.
            spec = importlib.util.spec_from_file_location('funk_audio', '/usr/local/lib/mumble-funk/audio.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            arguments = sys.argv[2:]
            if not arguments or arguments == ['status']:
                return module.main(arguments, container=True)
            # Mutations are offline; the host wrapper stops only this Compose service.
            with instance_lock():
                result = module.main(arguments, container=True)
                if result == 0:
                    print('AUDIO-REVISION='+hashlib.sha256(CONFIG.read_bytes()).hexdigest())
                return result
        elif command == 'set' and len(sys.argv) == 4:
            configure(sys.argv[2],sys.argv[3])
        else:
            raise RuntimeError('Aufruf: funk-container serve|info [--json]|health|backup|restore|admin-password|audio BEFEHL [WERT]|set SCHLÜSSEL WERT')
        return 0
    except (OSError, ValueError, RuntimeError, sqlite3.Error, tarfile.TarError) as error:
        print('Mumble-Funk Docker: '+str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
