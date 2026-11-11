#!/usr/bin/env python3
"""Manage Busy and experimental Squelch for the normal mumble-funk installation."""
import argparse
import contextlib
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import stat
import sys

INSTANCE = 'mumble-funk'
STANDARD_DIR = '/usr/local/share/mumble-funk/audio'
BUSY_STANDARD = STANDARD_DIR+'/busy-tone.wav'
NOISE_STANDARD = STANDARD_DIR+'/radio-noise.wav'
DEFAULTS = {
    'feedbackboostdb': '0', 'audiofeedback': 'false', 'busytone': 'true', 'busytonefile': '', 'busytonevolume': '50',
    'squelch': 'false', 'squelchopen': 'true', 'squelchclose': 'true',
    'squelchclosetimeoutfallback': 'false',
    'squelchopenfile': STANDARD_DIR+'/squelch-open.wav',
    'squelchclosefile': STANDARD_DIR+'/squelch-close.wav',
    'squelchopenvolume': '50', 'squelchclosevolume': '50',
    'radionoise': 'false', 'radionoisefile': '', 'radionoisevolume': '20',
}
HERE = Path(__file__).resolve().parent
COMMON_PATH = next(p for p in (HERE/'audio_control.py', HERE/'audio-preview/audio_control.py',
                              HERE.parent/'lib/mumble-funk/audio_control.py') if p.is_file())
SPEC = importlib.util.spec_from_file_location('funk_audio_common', COMMON_PATH)
COMMON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMMON)


def values(text):
    """Read only audio/radio keys; never return passwords or unrelated configuration."""
    result = {}
    section = 'General'
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith('['):
            section = 'General' if stripped == '[General]' else stripped
        if section != 'General' or stripped.startswith(('#', ';')) or '=' not in stripped:
            continue
        key, value = (part.strip() for part in stripped.split('=', 1))
        key = key.lower()
        if key not in DEFAULTS and key != 'simplex':
            continue
        if key in result:
            raise RuntimeError(f'INI enthält {key} mehrfach; keine Änderung vorgenommen.')
        result[key] = json.loads(value) if value.startswith('"') else value
    return result


@contextlib.contextmanager
def installation_lock(root):
    path = root/'run/lock/mumble-funk-install.lock'
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise RuntimeError('Installationssperre ist keine normale Datei.')
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield
    finally:
        os.close(descriptor)


def main(argv=None, container=False):
    commands = ['use-standard-busy-tone', 'boost', 'status', 'enable', 'disable', 'volume', 'set-busy-tone', 'reset-busy-tone',
                'enable-squelch', 'disable-squelch', 'enable-open', 'disable-open',
                'enable-close', 'disable-close', 'open-volume', 'close-volume',
                'enable-timeout-close', 'disable-timeout-close',
                'set-open', 'set-close', 'reset-open', 'reset-close',
                'set-radio-noise', 'enable-radio-noise', 'disable-radio-noise', 'radio-noise-volume',
                'use-standard-radio-noise']
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=commands, nargs='?', default='status')
    parser.add_argument('value', nargs='?')
    parser.add_argument('--root', type=Path, default=Path('/'), help=argparse.SUPPRESS)
    parser.add_argument('--binary', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    production = args.root.resolve() == Path('/')
    if production and not container and os.geteuid() != 0:
        parser.error('Bitte sudo mumble-funk-audio verwenden.')
    if production and args.binary:
        parser.error('Eigener Binary-Pfad nur für isolierte Tests erlaubt.')
    valued = {'boost', 'volume', 'open-volume', 'close-volume', 'set-busy-tone', 'set-open', 'set-close', 'set-radio-noise', 'radio-noise-volume'}
    if (args.command in valued) != (args.value is not None):
        parser.error('Dieser Befehl benötigt genau einen Wert.' if args.command in valued else 'Kein weiterer Wert erlaubt.')
    root = args.root
    config = root/'etc/mumble-funk/server.ini'
    sounds = root/('var/lib/mumble-funk/audio' if container else 'etc/mumble-funk/sounds')
    binary = args.binary or (Path('/usr/local/bin/mumble-server') if container else root/'opt/mumble-funk/current/mumble-server')
    try:
        # Container callers hold runtime.instance_lock for modifications; status is read-only.
        lock = contextlib.nullcontext() if container or args.command == 'status' else installation_lock(root)
        with lock:
            if not config.is_file() or config.is_symlink():
                raise RuntimeError('mumble-funk-Konfiguration fehlt oder ist ein symbolischer Link.')
            original = config.read_bytes()
            text = original.decode('utf-8-sig')
            configured = values(text)
            if args.command == 'status':
                # Deliberately whitelist output. Never print the complete INI or a password.
                print(json.dumps({key: configured.get(key, default) for key, default in DEFAULTS.items()}, indent=2))
                return 0
            if production and not container:
                fragment = COMMON.checked(['systemctl', 'show', INSTANCE, '--property=FragmentPath', '--value'])
                command = COMMON.checked(['systemctl', 'show', INSTANCE, '--property=ExecStart', '--value'])
                if (fragment != '/etc/systemd/system/mumble-funk.service'
                        or '/opt/mumble-funk/current/mumble-server -ini /etc/mumble-funk/server.ini -fg' not in command):
                    raise RuntimeError('Dienst gehört nicht zur erwarteten mumble-funk-Installation.')
            if args.command.startswith('set-'):
                signal = {'set-busy-tone': 'busy', 'set-open': 'open', 'set-close': 'close', 'set-radio-noise': 'radio-noise'}[args.command]
                key = ('radionoisefile' if signal == 'radio-noise' else
                       'busytonefile' if signal == 'busy' else 'squelch'+signal+'file')
                COMMON.update_ini(text, {key: ''})  # Reject duplicates before copying.
                if sounds.is_symlink():
                    raise RuntimeError('Signalverzeichnis darf kein symbolischer Link sein.')
                sounds.mkdir(mode=0o750, exist_ok=True)
                if production and not container:
                    sounds.chmod(0o750)
                    os.chown(sounds, 0, pwd.getpwnam(INSTANCE).pw_gid)
                name = COMMON.managed_copy(args.value, sounds, binary, production and not container,
                                           instance=INSTANCE, prefix=signal,
                                           validator=('--validate-radio-noise-wav' if signal == 'radio-noise' else
                                                      '--validate-feedback-wav' if signal == 'busy' else '--validate-squelch-wav'),
                                           max_bytes=8*1024*1024 if signal == 'radio-noise' else 131072)
                updates = {key: ('/var/lib/mumble-funk/audio/' if container else '/etc/mumble-funk/sounds/')+name}
            elif args.command == 'use-standard-busy-tone':
                COMMON.checked([str(binary), '--validate-feedback-wav', str(root/BUSY_STANDARD.lstrip('/'))])
                updates = {'busytonefile': BUSY_STANDARD}
            elif args.command == 'use-standard-radio-noise':
                COMMON.checked([str(binary), '--validate-radio-noise-wav', str(root/NOISE_STANDARD.lstrip('/'))])
                updates = {'radionoisefile': NOISE_STANDARD}
            elif args.command in ('reset-open', 'reset-close', 'reset-busy-tone'):
                signal = {'reset-open': 'open', 'reset-close': 'close', 'reset-busy-tone': 'busy'}[args.command]
                key = 'busytonefile' if signal == 'busy' else 'squelch'+signal+'file'
                if signal != 'busy':
                    COMMON.checked([str(binary), '--validate-squelch-wav', str(root/DEFAULTS[key].lstrip('/'))])
                updates = {key: '' if signal == 'busy' else DEFAULTS[key]}
            elif args.command == 'boost':
                if not re.fullmatch(r'(0|[1-9]|1[0-2])', args.value):
                    raise RuntimeError('Audio Boost muss eine ganze Zahl zwischen 0 und 12 dB sein.')
                updates = {'feedbackboostdb': args.value}
            elif args.command in ('volume', 'open-volume', 'close-volume', 'radio-noise-volume'):
                if not re.fullmatch(r'\d{1,3}', args.value) or int(args.value) > 100:
                    raise RuntimeError('Lautstärke muss zwischen 0 und 100 liegen.')
                key = {'volume': 'busytonevolume', 'open-volume': 'squelchopenvolume', 'close-volume': 'squelchclosevolume',
                       'radio-noise-volume': 'radionoisevolume'}[args.command]
                updates = {key: str(int(args.value))}
            elif args.command == 'enable-radio-noise':
                if any(configured.get(key, 'false').lower() != 'true' for key in ('simplex', 'audiofeedback')):
                    raise RuntimeError('Rauschen benötigt bereits aktiviertes Simplex und Audio-Feedback.')
                selected = configured.get('radionoisefile', '')
                if not selected or not Path(selected).is_absolute():
                    raise RuntimeError('Zuerst eine Rausch-WAV importieren oder use-standard-radio-noise verwenden.')
                COMMON.checked([str(binary), '--validate-radio-noise-wav', str(root/selected.lstrip('/'))])
                updates = {'radionoise': 'true'}
            elif args.command == 'disable-radio-noise':
                updates = {'radionoise': 'false'}
            elif args.command == 'enable-squelch':
                if configured.get('simplex', 'false').lower() != 'true':
                    raise RuntimeError('Squelch benötigt bereits aktiviertes Simplex; Simplex wird nicht automatisch geändert.')
                updates = {'squelch': 'true', 'audiofeedback': 'true'}
                for signal in ('open', 'close'):
                    key = 'squelch'+signal+'file'
                    selected = configured.get(key) or DEFAULTS[key]
                    if not configured.get(key):
                        updates[key] = selected
                    if configured.get('squelch'+signal, 'true').lower() == 'true':
                        COMMON.checked([str(binary), '--validate-squelch-wav', str(root/selected.lstrip('/'))])
            else:
                key = {'enable': 'audiofeedback', 'disable': 'audiofeedback',
                       'disable-squelch': 'squelch', 'enable-open': 'squelchopen', 'disable-open': 'squelchopen',
                       'enable-timeout-close': 'squelchclosetimeoutfallback', 'disable-timeout-close': 'squelchclosetimeoutfallback',
                       'enable-close': 'squelchclose', 'disable-close': 'squelchclose'}[args.command]
                updates = {key: 'true' if args.command.startswith('enable') else 'false'}
                if args.command in ('enable-open', 'enable-close'):
                    file_key = key+'file'
                    selected = configured.get(file_key) or DEFAULTS[file_key]
                    COMMON.checked([str(binary), '--validate-squelch-wav', str(root/selected.lstrip('/'))])
                    if not configured.get(file_key):
                        updates[file_key] = selected
            updated = COMMON.update_ini(text, updates).encode('utf-8')
            backup = config.with_name('server.ini.before-audio-change')
            if backup.is_symlink():
                raise RuntimeError('Konfigurationssicherung darf kein symbolischer Link sein.')
            shutil.copy2(config, backup)
            COMMON.atomic_config(config, updated)
            if production and not container:
                try:
                    COMMON.checked(['systemctl', 'restart', INSTANCE])
                    COMMON.checked(['systemctl', 'is-active', '--quiet', INSTANCE])
                except RuntimeError as error:
                    COMMON.atomic_config(config, original)
                    COMMON.checked(['systemctl', 'restart', INSTANCE])
                    raise RuntimeError('Neustart fehlgeschlagen; vorige Konfiguration wiederhergestellt. Bitte server.log prüfen.') from error
        print('Audio-Einstellungen gespeichert.' + (' Nur mumble-funk wurde neu gestartet.' if production and not container else ' Kein Dienst gestartet.'))
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Mumble-Funk Audio: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
