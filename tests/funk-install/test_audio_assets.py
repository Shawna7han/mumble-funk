"""WAV format validation uses the actual C++ server parser, also used by the admin command."""
import importlib.util
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

BINARY = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[2]
CONTROL = REPO / 'scripts/funk/audio-preview/audio_control.py'
SPEC = importlib.util.spec_from_file_location('audio_control', CONTROL)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def wav(path, rate=48000, channels=1, width=2, duration=.1, amplitude=.95):
    with wave.open(str(path), 'wb') as file:
        file.setparams((channels, width, rate, 0, 'NONE', 'not compressed'))
        data = bytearray()
        for sample in range(int(rate * duration)):
            value = int(amplitude * 32767 * math.sin(2 * math.pi * 1000 * sample / rate))
            data.extend((struct.pack('<h', value) if width == 2 else b'\x80') * channels)
        file.writeframes(bytes(data))


class AudioAssets(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.asset = self.root / 'own.wav'
        self.config = self.root / 'etc/mumble-funk-audio-engine/server.ini'
        self.config.parent.mkdir(parents=True)
        self.config.write_text('port=64741\nserverpassword=preserved\nsimplex=true\naudiofeedback=true\nbusytonefile=\n[Other]\nkeep=yes\n')
        self.config.chmod(0o640)

    def validate(self, path=None):
        return subprocess.run([str(BINARY), '--validate-feedback-wav', str(path or self.asset)], capture_output=True)

    def control(self, *args):
        return subprocess.run([sys.executable, str(CONTROL), *args, '--root', str(self.root), '--binary', str(BINARY)], capture_output=True, text=True)

    def test_supported_rates_and_boundary_lengths(self):
        for rate in (8000, 16000, 24000, 48000):
            for duration in (.02, .1, 1, 1.19725, 1.25):
                with self.subTest(rate=rate, duration=duration):
                    wav(self.asset, rate=rate, duration=duration)
                    result = self.validate()
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_formats_and_lengths(self):
        cases = [{'rate': 44100}, {'channels': 2}, {'width': 1}, {'duration': .01}, {'duration': 1.251}, {'duration': 2}]
        for parameters in cases:
            with self.subTest(parameters=parameters):
                wav(self.asset, **parameters)
                self.assertNotEqual(self.validate().returncode, 0)
        for content in (b'', b'RIFF', b'not an audio file', b'X' * 131073):
            self.asset.write_bytes(content)
            self.assertNotEqual(self.validate().returncode, 0)
        self.assertNotEqual(self.validate(self.root / 'missing.wav').returncode, 0)
        self.assertNotEqual(self.validate(self.root).returncode, 0)

    def test_malformed_riff_and_truncated_chunks(self):
        wav(self.asset)
        original = self.asset.read_bytes()
        variants = [original[:-1], original + b'extra', b'RIFX'+original[4:], original[:4]+b'\xff'*4+original[8:],
                    original[:16]+b'\xff'*4+original[20:], original[:20]+b'\x03\x00'+original[22:],
                    original[:32]+b'\x04\x00'+original[34:], original[:28]+b'\x00'*4+original[32:]]
        for content in variants:
            self.asset.write_bytes(content)
            self.assertNotEqual(self.validate().returncode, 0)

    def test_manager_copy_config_and_reset_without_compilation(self):
        wav(self.asset, rate=16000)
        before_binary = BINARY.stat().st_mtime_ns
        result = self.control('set-busy-tone', str(self.asset))
        self.assertEqual(result.returncode, 0, result.stderr)
        copied = list((self.config.parent/'sounds').glob('busy-*.wav'))
        self.assertEqual(len(copied), 1)
        self.assertEqual(copied[0].read_bytes(), self.asset.read_bytes())
        self.assertEqual(copied[0].stat().st_mode & 0o777, 0o640)
        text = self.config.read_text()
        self.assertIn('busytonefile=/etc/mumble-funk-audio-engine/sounds/busy-', text)
        self.assertIn('serverpassword=preserved\n', text)
        self.assertIn('[Other]\nkeep=yes\n', text)
        self.assertEqual(self.control('reset-busy-tone').returncode, 0)
        self.assertIn('busytonefile=\n', self.config.read_text())
        self.assertEqual(BINARY.stat().st_mtime_ns, before_binary)
        self.assertEqual(self.config.stat().st_mode & 0o777, 0o640)

    def test_invalid_asset_keeps_configuration(self):
        before = self.config.read_bytes()
        self.asset.write_bytes(b'corrupt audio')
        self.assertNotEqual(self.control('set-busy-tone', str(self.asset)).returncode, 0)
        self.assertEqual(self.config.read_bytes(), before)
        self.assertFalse(list((self.config.parent/'sounds').glob('busy-*.wav')))
        self.assertNotEqual(self.control('volume', '101').returncode, 0)
        self.assertEqual(self.config.read_bytes(), before)

    def test_symlinks_and_special_files_rejected(self):
        wav(self.asset)
        link = self.root / 'link.wav'
        link.symlink_to(self.asset)
        before = self.config.read_bytes()
        self.assertNotEqual(self.control('set-busy-tone', str(link)).returncode, 0)
        fifo = self.root / 'fifo.wav'
        os.mkfifo(fifo)
        self.assertNotEqual(self.control('set-busy-tone', str(fifo)).returncode, 0)
        self.assertEqual(self.config.read_bytes(), before)

    def test_enable_disable_volume_preserve_other_instances(self):
        protected = []
        for instance in ('mumble-funk', 'mumble-funk-audio'):
            foreign = self.root / f'etc/{instance}/server.ini'
            foreign.parent.mkdir()
            foreign.write_text('existing instance do not change')
            protected.append(foreign)
        for command in [('disable',), ('enable',), ('volume', '35')]:
            self.assertEqual(self.control(*command).returncode, 0)
        text = self.config.read_text()
        self.assertIn('audiofeedback=true\n', text)
        self.assertIn('busytonevolume=35\n', text)
        self.assertLess(text.index('busytonevolume=35'), text.index('[Other]'))
        for foreign in protected:
            self.assertEqual(foreign.read_text(), 'existing instance do not change')

    def test_ini_duplicate_safety(self):
        with self.assertRaises(RuntimeError):
            MODULE.update_ini('audiofeedback=true\n[General]\naudiofeedback=false\n', {'audiofeedback': 'true'})
        text = MODULE.update_ini('[Other]\naudiofeedback=foreign\n', {'audiofeedback': 'true'})
        self.assertTrue(text.startswith('audiofeedback=true\n[Other]'))
        self.assertIn('audiofeedback=foreign', text)


if __name__ == '__main__':
    unittest.main(verbosity=2)
