"""Reject invalid direct INI boost settings before a server starts listening."""
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class BoostConfigTests(unittest.TestCase):
    def test_invalid_ini_values_are_rejected_even_with_feedback_off(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for value in ('-1', '13', '100', '1.5', 'NaN', 'inf', '+6', '01', '6e0', '', '1, 2', '@Invalid()'):
                with self.subTest(value=value):
                    ini = root/'invalid.ini'
                    ini.write_text('audiofeedback=false\nfeedbackboostdb='+value+'\n')
                    result = subprocess.run([str(BINARY), '-fg', '-ini', str(ini)], cwd=root,
                                            capture_output=True, timeout=5, preexec_fn=no_core)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(b'feedbackboostdb must be an integer from 0 to 12 dB', result.stderr)
                    self.assertFalse((root/'mumble-server.sqlite').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
