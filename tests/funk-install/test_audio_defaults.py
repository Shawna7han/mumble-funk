"""Fresh installer configs on real isolated servers; never install a live service."""
import importlib.util
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import time
import types
import unittest
from unittest.mock import patch

BINARY = Path(sys.argv.pop(1)).resolve()
PROBE = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[2]
EXPECTED = {
    'simplex': 'true', 'audiofeedback': 'true', 'busytone': 'true',
    'busytonefile': '/usr/local/share/mumble-funk/audio/busy-tone.wav',
    'busytonevolume': '50', 'squelch': 'true', 'squelchopen': 'true',
    'squelchclose': 'true', 'squelchopenvolume': '50', 'squelchclosevolume': '50',
    'feedbackboostdb': '8', 'squelchclosetimeoutfallback': 'true',
    'radionoise': 'true', 'radionoisefile': '/usr/local/share/mumble-funk/audio/radio-noise.wav',
    'radionoisevolume': '20',
}


def values(text):
    return dict(line.split('=', 1) for line in text.splitlines()
                if '=' in line and not line.startswith(('#', ';')))


class AudioDefaults(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def native(self, name, setup=True):
        root = self.root/name
        args = ['bash', str(REPO/'scripts/funk/install.sh')]
        if setup:
            args.append('--setup')
        args += ['--stage', str(root), str(BINARY)]
        subprocess.run(args, check=True, capture_output=True)
        return root, root/'etc/mumble-funk/server.ini', args

    def docker(self):
        with patch.dict(os.environ, FUNK_TEST_ROOT=str(self.root/'docker')):
            spec = importlib.util.spec_from_file_location('defaults_runtime', REPO/'scripts/docker/runtime.py')
            runtime = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(runtime)
        for directory in runtime.DIRECTORIES.values():
            directory.mkdir(parents=True, exist_ok=True)
        (runtime.DIRECTORIES['state']/'audio').mkdir()
        # Generate the exact runtime INI without starting a container or database.
        with patch.object(runtime.subprocess, 'run', return_value=types.SimpleNamespace(returncode=0)):
            runtime.initialize()
        return runtime.CONFIG

    def test_same_complete_defaults_for_native_setup_manual_and_docker(self):
        configs = [self.native('setup')[1], self.native('manual', False)[1], self.docker()]
        for config in configs:
            with self.subTest(config=config):
                actual = values(config.read_text())
                self.assertEqual({key: actual[key] for key in EXPECTED}, EXPECTED)
                for key in EXPECTED:
                    self.assertEqual(sum(line.startswith(key+'=') for line in config.read_text().splitlines()), 1)
                self.assertEqual(actual['port'], '64739')

    def test_existing_native_missing_disabled_custom_and_silent_are_preserved(self):
        root, config, args = self.native('existing')
        own = root/'etc/mumble-funk/sounds/own.wav'
        own.write_bytes((REPO/'assets/audio/busy-tone.wav').read_bytes())
        base = 'port=64739\ndatabase=/var/lib/mumble-funk/server.sqlite\nserverpassword=private-fixture\n'
        variants = [
            '',  # All radio/audio keys absent: core defaults must stay off/boost0.
            'simplex=false\naudiofeedback=false\nsquelch=false\nfeedbackboostdb=0\nbusytonefile=\nsquelchclosetimeoutfallback=false\n',
            'simplex=true\naudiofeedback=false\nsquelch=false\nbusytone=false\nfeedbackboostdb=12\n'
            'busytonefile=/etc/mumble-funk/sounds/own.wav\nbusytonevolume=0\n'
            'squelchopen=false\nsquelchclose=false\nsquelchclosetimeoutfallback=true\n',
        ]
        for variant in variants:
            with self.subTest(variant=variant):
                before = (base+variant+'\n[Other]\nkeep=unchanged\n').encode()
                config.write_bytes(before)
                subprocess.run(args, check=True, capture_output=True)
                self.assertEqual(config.read_bytes(), before)
                self.assertEqual(own.read_bytes(), (REPO/'assets/audio/busy-tone.wav').read_bytes())
        # Admin status uses legacy defaults when fields are missing; no migration.
        config.write_text(base)
        before = config.read_bytes()
        result = subprocess.run([sys.executable, str(root/'usr/local/sbin/mumble-funk-audio'),
                                 'status', '--root', str(root), '--binary', str(BINARY)],
                                check=True, capture_output=True, text=True)
        import json
        status = json.loads(result.stdout)
        self.assertEqual((status['audiofeedback'], status['squelch'], status['feedbackboostdb'], status['busytonefile']),
                         ('false', 'false', '0', ''))
        self.assertEqual(status['squelchclosetimeoutfallback'], 'false')
        self.assertEqual((status['radionoise'], status['radionoisefile'], status['radionoisevolume']),
                         ('false', '', '20'))
        self.assertEqual(config.read_bytes(), before)

    def test_actual_fresh_audio_and_volume_zero_native_and_docker(self):
        configs = [self.native('live')[1], self.docker()]
        for original in configs:
            for muted in (None, False, True):
                with self.subTest(source=original, muted=muted):
                    with socket.socket() as reserve:
                        reserve.bind(('127.0.0.1', 0))
                        port = reserve.getsockname()[1]
                    ini = self.root/'live.ini'
                    text = original.read_text().replace('host=0.0.0.0', 'host=127.0.0.1')
                    text = text.replace('port=64739', f'port={port}')
                    text = text.replace('/var/lib/mumble-funk', str(self.root/'state'))
                    text = text.replace('/var/log/mumble-funk', str(self.root/'logs'))
                    text = text.replace('/usr/local/share/mumble-funk/audio', str(REPO/'assets/audio'))
                    # Exercise the untouched new config first, then retain the
                    # existing independent Busy/Squelch byte/mute regressions.
                    if muted is not None:
                        text = text.replace('radionoise=true', 'radionoise=false')
                    if muted:
                        for key in ('busytonevolume', 'squelchopenvolume', 'squelchclosevolume'):
                            text = text.replace(key+'=50', key+'=0')
                    (self.root/'state').mkdir(exist_ok=True)
                    (self.root/'logs').mkdir(exist_ok=True)
                    ini.write_text(text)
                    server = subprocess.Popen([str(BINARY), '-ini', str(ini), '-fg'],
                                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    try:
                        startup = b''
                        deadline = time.monotonic()+15
                        while b'Server listening on' not in startup and time.monotonic()<deadline:
                            if select.select([server.stdout], [], [], .1)[0]:
                                startup += os.read(server.stdout.fileno(), 65536)
                            if server.poll() is not None:
                                break
                        self.assertIn(b'Server listening on', startup)
                        self.assertIn(b'Experimental audio feedback enabled:', startup)
                        self.assertIn(b'Experimental Squelch', startup)
                        password = values(text)['serverpassword']
                        mode = '--radio-noise' if muted is None else '--audio-defaults-muted' if muted else '--audio-defaults'
                        result = subprocess.run([str(PROBE), str(port), mode], input=password+'\n',
                                                # The radio probe includes a 62-second transmission;
                                                # retain the 20-second bound for legacy probes.
                                                capture_output=True, text=True, timeout=100 if muted is None else 20)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertIn('Container Open/Noise/Close verified' if muted is None else
                                      'Installed audio defaults verified', result.stdout)
                    finally:
                        server.terminate()
                        try:
                            server.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            server.kill()
                            server.wait()
                        server.stdout.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
