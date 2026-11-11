"""Staged native radio-noise installation; no system paths, users or services touched."""
from pathlib import Path
import hashlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[2]


class RadioNoiseInstallation(unittest.TestCase):
    def test_fresh_standard_load_and_existing_settings_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / 'stage'
            command = ['bash', str(REPO / 'scripts/funk/install.sh'), '--setup',
                       '--experimental-radio-noise', '--stage', str(stage), str(BINARY)]
            subprocess.run(command, check=True, capture_output=True)
            ini = stage / 'etc/mumble-funk/server.ini'
            original = ini.read_text()
            self.assertIn('radionoise=true\n', original)
            self.assertIn('radionoisefile=/usr/local/share/mumble-funk/audio/radio-noise.wav\n', original)
            self.assertIn('radionoisevolume=20\n', original)
            standard = stage/'usr/local/share/mumble-funk/audio/radio-noise.wav'
            self.assertEqual(standard.read_bytes(), (REPO/'assets/audio/radio-noise.wav').read_bytes())
            self.assertEqual(hashlib.sha256(standard.read_bytes()).hexdigest(),
                             'dc0a1c97115eb797f261af52631288b868a648d0d9310fff0a431e8163536312')
            self.assertEqual(standard.stat().st_mode & 0o777, 0o644)
            self.assertFalse((stage / 'etc/mumble-funk/sounds/radio-noise.wav').exists())
            info = (stage / 'opt/mumble-funk/current/BUILD-INFO').read_text()
            # GitHub Actions checks out PRs detached. Source authorization and
            # radio-noise BUILD-INFO are covered by the orchestration fixture.
            branch = subprocess.check_output(['git', '-C', str(REPO), 'branch', '--show-current'],
                                             text=True).strip()
            self.assertIn(f'branch={branch}\n', info)
            if branch == 'feature/radio-noise':
                self.assertIn('experimental=true\n', info)
            self.assertEqual(ini.stat().st_mode & 0o777, 0o640)
            self.assertEqual((stage / 'etc/mumble-funk/sounds').stat().st_mode & 0o777, 0o750)

            source = REPO/'assets/audio/radio-noise.wav'
            recording = source.read_bytes()
            wav = stage / 'etc/mumble-funk/sounds/radio-noise.wav'

            def startup(configuration):
                with socket.socket() as reserve:
                    reserve.bind(('127.0.0.1', 0))
                    port = reserve.getsockname()[1]
                text = configuration.replace('host=0.0.0.0', 'host=127.0.0.1')
                text = text.replace('port=64739', f'port={port}')
                text = text.replace('/var/lib/mumble-funk', str(stage / 'var/lib/mumble-funk'))
                text = text.replace('/var/log/mumble-funk', str(stage / 'var/log/mumble-funk'))
                text = text.replace('/usr/local/share/mumble-funk/audio', str(stage / 'usr/local/share/mumble-funk/audio'))
                # No secrets are printed; this disposable INI retains the staged password.
                live_ini = root / 'live.ini'
                live_ini.write_text(text)
                log = root / 'startup.log'
                with log.open('wb') as output:
                    server = subprocess.Popen([str(BINARY), '-ini', str(live_ini), '-fg'],
                                              stdout=output, stderr=subprocess.STDOUT)
                    try:
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline:
                            current = log.read_text(errors='replace')
                            if 'Server listening on' in current:
                                return current
                            self.assertIsNone(server.poll(), current)
                            time.sleep(.05)
                        self.fail('Disposable server did not start')
                    finally:
                        server.terminate()
                        try:
                            server.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            server.kill()
                            server.wait()

            enabled = startup(original)
            self.assertIn('Experimental radio noise enabled:', enabled)
            self.assertIn('Experimental audio feedback enabled:', enabled)
            self.assertIn('Experimental Squelch', enabled)
            disabled = startup(original.replace('radionoise=true', 'radionoise=false'))
            self.assertNotIn('Experimental radio noise enabled:', disabled)
            shutil.copyfile(source, wav)
            wav.chmod(0o640)
            state = stage / 'var/lib/mumble-funk/server.sqlite'
            custom = original.replace('radionoisefile=/usr/local/share/mumble-funk/audio/radio-noise.wav',
                                      'radionoisefile=/etc/mumble-funk/sounds/radio-noise.wav')
            variants = [custom.replace('radionoisevolume=20', 'radionoisevolume=37'),
                        custom.replace('radionoise=true', 'radionoise=false').replace('radionoisevolume=20', 'radionoisevolume=0'),
                        '\n'.join(line for line in original.splitlines() if not line.startswith('radionoise'))+'\n']
            for text in variants:
                ini.write_text(text)
                protected = {p: p.read_bytes() for p in (ini, wav, state)}
                subprocess.run(command, check=True, capture_output=True)
                for path, content in protected.items():
                    self.assertEqual(path.read_bytes(), content, str(path))
            self.assertEqual(wav.stat().st_mode & 0o777, 0o640)
            # A custom file at the newly shipped path must not be overwritten.
            before = ini.read_bytes()
            for symlink in (False, True):
                standard.unlink()
                if symlink:
                    standard.symlink_to(wav)
                else:
                    standard.write_bytes(b'operator-owned recording')
                result = subprocess.run(command, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(ini.read_bytes(), before)
                self.assertEqual(wav.read_bytes(), recording)
                self.assertEqual(standard.is_symlink(), symlink)
                self.assertEqual(standard.read_bytes(), recording if symlink else b'operator-owned recording')
            self.assertEqual(source.read_bytes(), recording)


if __name__ == '__main__':
    unittest.main(verbosity=2)
