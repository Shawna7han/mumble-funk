"""Synthetic fixtures only. No private speech recordings or Python DSP filters."""
import array
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

BINARY = Path(sys.argv.pop(1)).resolve()


def riff(fmt, data, extra=b''):
    def chunk(name, payload):
        return name + struct.pack('<I', len(payload)) + payload + bytes(len(payload) % 2)
    body = b'WAVE' + chunk(b'fmt ', fmt) + extra + chunk(b'data', data)
    return b'RIFF' + struct.pack('<I', len(body)) + body


def format_chunk(kind=1, channels=1, rate=48000, bits=16):
    align = channels * (bits // 8)
    return struct.pack('<HHIIHH', kind, channels, rate, rate * align, align, bits)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'speech.wav'
        self.original = riff(format_chunk(), struct.pack('<24017h', *(
            int(5000 * math.sin(i * .17)) for i in range(24017))))
        self.source.write_bytes(self.original)

    def run_cli(self, *args, success=True):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        self.assertEqual(self.source.read_bytes(), self.original)
        return result

    def convert(self, *args, success=True, source=None, output=None):
        return self.run_cli('--input', source or self.source, '--output', output or self.root/'result.wav', *args, success=success)

    def test_all_outputs_original_and_report(self):
        directory = self.root/'comparison'
        result = self.run_cli('--input', self.source, '--all', '--output-dir', directory)
        self.assertEqual((directory/'original.wav').read_bytes(), self.original)
        reports = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(reports), 4)
        hashes = set()
        for name in ('opus-reference', 'cb-am', 'business-fm', 'narrowband'):
            path = directory/(name+'.wav')
            with wave.open(str(path)) as reader:
                self.assertEqual((reader.getnchannels(), reader.getsampwidth(), reader.getframerate(), reader.getnframes()), (1, 2, 48000, 24017))
            hashes.add(hashlib.sha256(path.read_bytes()).digest())
        self.assertEqual(len(hashes), 4)
        self.assertTrue(all(r['clipped_samples'] == 0 and r['samples'] == 24017 for r in reports))
        self.run_cli('--input', self.source, '--all', '--output-dir', directory, success=False)

    def test_supported_formats_and_unknown_metadata(self):
        for kind, bits in ((1, 16), (1, 24), (1, 32), (3, 32)):
            with self.subTest(kind=kind, bits=bits):
                source = self.root/f'format-{kind}-{bits}.wav'
                if kind == 3:
                    data = struct.pack('<961f', *(.05*math.sin(i*.17) for i in range(961)))
                else:
                    width = bits//8
                    data = b''.join(int(.05*math.sin(i*.17)*(2**(bits-1))).to_bytes(width, 'little', signed=True) for i in range(961))
                source.write_bytes(riff(format_chunk(kind=kind, bits=bits), data, b'JUNK\x01\x00\x00\x00x\x00'))
                self.convert('--profile', 'reference', source=source, output=self.root/f'out-{kind}-{bits}.wav')

    def test_wrong_rate_channels_format_and_empty_refused(self):
        formats = [format_chunk(rate=44100), format_chunk(channels=2), format_chunk(bits=8), format_chunk(kind=6), format_chunk(kind=65534)]
        for i, fmt in enumerate(formats):
            source = self.root/f'bad-{i}.wav'; source.write_bytes(riff(fmt, bytes(200)))
            self.convert(source=source, success=False)
        source = self.root/'empty.wav'; source.write_bytes(riff(format_chunk(), b''))
        self.convert(source=source, success=False)

    def test_invalid_float_and_truncation_leave_no_output(self):
        for i, value in enumerate((float('nan'), float('inf'), -float('inf'), 99.0)):
            source = self.root/f'float-{i}.wav'
            source.write_bytes(riff(format_chunk(kind=3, bits=32), struct.pack('<f', value)))
            self.convert(source=source, success=False)
            self.assertFalse((self.root/'result.wav').exists())
        source = self.root/'truncated.wav'; source.write_bytes(self.original[:-1])
        self.convert(source=source, success=False)

    def test_riff_bounds_duplicates_and_alignment_refused(self):
        fixtures = [self.original[:4]+struct.pack('<I', 0xffffffff)+self.original[8:],
                    riff(format_chunk(), b'\0'),
                    riff(format_chunk(), b'\0\0', b'data\x02\0\0\0\0\0'),
                    riff(format_chunk(), b'\0\0', b'JUNK\xff\xff\xff\xff'),
                    b'RF64'+self.original[4:]]
        for i, data in enumerate(fixtures):
            source = self.root/f'broken-{i}.wav'; source.write_bytes(data)
            self.convert(source=source, success=False)

    def test_read_error_after_partial_output_removes_incomplete_file(self):
        source = self.root/'late-error.wav'
        source.write_bytes(riff(format_chunk(kind=3, bits=32), struct.pack('<961f', *([.1]*960+[float('nan')]))))
        self.convert('--profile', 'reference', source=source, success=False)
        self.assertFalse((self.root/'result.wav').exists())

    def test_original_output_and_existing_file_are_never_overwritten(self):
        self.convert(output=self.source, success=False)
        existing = self.root/'existing.wav'; existing.write_bytes(b'preserve')
        self.convert(output=existing, success=False)
        self.assertEqual(existing.read_bytes(), b'preserve')
        linked = self.root/'linked.wav'; linked.symlink_to(self.source)
        self.convert(output=linked, success=False)
        hard = self.root/'hard.wav'; hard.hardlink_to(self.source)
        self.convert(output=hard, success=False)

    def test_options_and_dsp_configuration_validation(self):
        for args in [('--bitrate', '0'), ('--bitrate', '40000.5'), ('--complexity', '11'),
                     ('--headroom-db', '-1'), ('--headroom-db', 'nan'), ('--set', 'ratio=0'),
                     ('--set', 'mid_q=nan'), ('--set', 'highpass_hz=4000'),
                     ('--set', 'unknown=1'), ('--profile', 'unknown'), ('--vbr', 'maybe'),
                     ('--bandwidth', 'unknown'), ('--profile', 'reference', '--set', 'ratio=2')]:
            with self.subTest(args=args): self.convert(*args, success=False)
        self.convert('--set', 'ratio=2.5', '--set', 'mid_gain_db=3', '--bitrate', '24000', '--second-bitrate', '32000', '--vbr', 'off', '--bandwidth', 'wide')

    def test_silence_and_level_matching_disabled(self):
        source = self.root/'silence.wav'; source.write_bytes(riff(format_chunk(), bytes(4000)))
        result = self.convert('--level-match', 'off', source=source)
        self.assertEqual(json.loads(result.stdout)['level_gain_db'], 0)
        with wave.open(str(self.root/'result.wav')) as reader:
            samples = array.array('h', reader.readframes(reader.getnframes()))
        self.assertLessEqual(max(map(abs, samples)), 5)  # Codec floor, not a new radio noise source.


if __name__ == '__main__':
    unittest.main(verbosity=2)
