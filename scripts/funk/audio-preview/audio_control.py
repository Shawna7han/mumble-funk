#!/usr/bin/env python3
"""Manage signal assets for the experimental instance only (never v0.2)."""
import argparse
import contextlib
import fcntl
import hashlib
import os
from pathlib import Path
import pwd
import re
import shutil
import stat
import subprocess
import sys
import tempfile

INSTANCE = "mumble-funk-audio-engine"


def checked(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or f"Befehl fehlgeschlagen: {command[0]}")
    return result.stdout.strip()


def update_ini(text, updates):
    """Preserve unrelated settings and sections; reject duplicate relevant root keys."""
    section = "General"
    found = set()
    lines = text.splitlines(keepends=True)
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('['):
            section = 'General' if stripped == '[General]' else stripped
        if section != 'General' or stripped.startswith(('#', ';')) or '=' not in stripped:
            continue
        key = stripped.split('=', 1)[0].strip().lower()
        if key in updates:
            if key in found:
                raise RuntimeError(f"INI enthält {key} mehrfach; keine Änderung vorgenommen.")
            found.add(key)
            lines[index] = f"{key}={updates[key]}\n"
    missing = ''.join(f"{key}={value}\n" for key, value in updates.items() if key not in found)
    # Root keys must precede any INI section, even if the file starts with a group.
    return missing + ''.join(lines)


def atomic_config(path, content):
    original = path.stat()
    fd, temporary = tempfile.mkstemp(prefix='.audio-config.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as file:
            os.fchmod(file.fileno(), stat.S_IMODE(original.st_mode))
            if os.geteuid() == 0:
                os.fchown(file.fileno(), original.st_uid, original.st_gid)
            file.write(content)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary, path)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


def managed_copy(source, sounds, binary, production, instance=INSTANCE,
                 validator="--validate-feedback-wav", prefix="busy", max_bytes=131072):
    # Limit before reading, including special files and accidental huge recordings.
    descriptor = os.open(source, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_size > max_bytes:
            raise RuntimeError("Nur normale WAV-Dateien innerhalb der Größenbegrenzung erlaubt (keine symbolischen Links).")
        with os.fdopen(os.dup(descriptor), "rb") as source_file:
            data = source_file.read(max_bytes + 1)
        if len(data) > max_bytes:
            raise RuntimeError("WAV-Datei zu groß.")
    finally:
        os.close(descriptor)
    sounds.mkdir(mode=0o750, exist_ok=True)
    if sounds.is_symlink():
        raise RuntimeError("Signalverzeichnis darf kein symbolischer Link sein.")
    fd, temporary = tempfile.mkstemp(prefix='.busy-', suffix='.wav', dir=sounds)
    try:
        with os.fdopen(fd, 'wb') as file:
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
            os.fchmod(file.fileno(), 0o640)
            if production:
                os.fchown(file.fileno(), 0, pwd.getpwnam(instance).pw_gid)
        # Exactly the same C++ parser and Opus encoder as the running server.
        checked([str(binary), validator, temporary])
        target = sounds / (prefix + '-' + hashlib.sha256(data).hexdigest() + '.wav')
        # Content-addressed assets are immutable: never replace an operator file.
        # Publishing a hard link is atomic and refuses an existing destination.
        try:
            os.link(temporary, target)
        except FileExistsError:
            existing = os.open(target, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            try:
                if not stat.S_ISREG(os.fstat(existing).st_mode):
                    raise RuntimeError("Vorhandene Signaldatei ist keine normale Datei.")
                with os.fdopen(os.dup(existing), 'rb') as saved:
                    if saved.read(max_bytes + 1) != data:
                        raise RuntimeError("Vorhandene Signaldatei weicht von ihrer Prüfsumme ab; keine Änderung.")
            finally:
                os.close(existing)
        return target.name
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['set-busy-tone', 'reset-busy-tone', 'enable', 'disable', 'volume'])
    parser.add_argument('value', nargs='?')
    parser.add_argument('--root', type=Path, default=Path('/'), help=argparse.SUPPRESS)
    parser.add_argument('--binary', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    production = args.root == Path('/')
    if production and os.geteuid() != 0:
        parser.error('Bitte sudo mumble-funk-audio-engine verwenden.')
    if production and args.binary:
        parser.error('Eigener Binary-Pfad nur für isolierte Tests erlaubt.')
    if args.command in ('set-busy-tone', 'volume') and args.value is None:
        parser.error('Datei beziehungsweise Lautstärke fehlt.')
    if args.command not in ('set-busy-tone', 'volume') and args.value is not None:
        parser.error('Dieser Befehl benötigt keinen weiteren Wert.')
    root = args.root
    config = root / f'etc/{INSTANCE}/server.ini'
    sounds = root / f'etc/{INSTANCE}/sounds'
    binary = args.binary or root / f'opt/{INSTANCE}/current/mumble-server'
    lock_path = root / ('run/lock/' + INSTANCE + '-install.lock')
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with lock_path.open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            if not config.is_file() or config.is_symlink():
                raise RuntimeError('Eigene Audio-Preview-Konfiguration fehlt oder ist ein symbolischer Link.')
            original = config.read_bytes()
            text = original.decode('utf-8-sig')
            # Check service ownership before changing anything or restarting a process.
            if production:
                fragment = checked(['systemctl', 'show', INSTANCE, '--property=FragmentPath', '--value'])
                command = checked(['systemctl', 'show', INSTANCE, '--property=ExecStart', '--value'])
                if fragment != f'/etc/systemd/system/{INSTANCE}.service' or f'/opt/{INSTANCE}/current/mumble-server -ini /etc/{INSTANCE}/server.ini -fg' not in command:
                    raise RuntimeError('Dienst gehört nicht zur erwarteten Audio-Preview-Installation.')
            if args.command == 'set-busy-tone':
                # Reject ambiguous keys before importing even a valid asset.
                update_ini(text, {'busytonefile': ''})
                name = managed_copy(args.value, sounds, binary, production)
                updates = {'busytonefile': f'/etc/{INSTANCE}/sounds/{name}'}
            elif args.command == 'reset-busy-tone':
                updates = {'busytonefile': ''}
            elif args.command == 'volume':
                if not re.fullmatch(r'\d{1,3}', args.value) or int(args.value) > 100:
                    raise RuntimeError('Lautstärke muss zwischen 0 und 100 liegen.')
                updates = {'busytonevolume': str(int(args.value))}
            else:
                updates = {'audiofeedback': 'true' if args.command == 'enable' else 'false'}
            updated = update_ini(text, updates).encode('utf-8')
            backup = config.with_name('server.ini.before-audio-change')
            # Private last-known configuration; never contains another instance's data.
            shutil.copy2(config, backup)
            atomic_config(config, updated)
            if production:
                try:
                    checked(['systemctl', 'restart', INSTANCE])
                    checked(['systemctl', 'is-active', '--quiet', INSTANCE])
                except RuntimeError:
                    atomic_config(config, original)
                    subprocess.run(['systemctl', 'restart', INSTANCE], check=False)
                    raise RuntimeError('Neustart fehlgeschlagen; vorige Konfiguration wiederhergestellt. Bitte server.log prüfen.')
        print('Audio-Preview aktualisiert.' + (' Nur mumble-funk-audio-engine wurde neu gestartet.' if production else ' Isolierter Test: kein Dienst gestartet.'))
        print('v0.2 wurde nicht verändert. Verbindung nach dem Neustart ggf. erneut herstellen.')
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Audio-Preview: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
