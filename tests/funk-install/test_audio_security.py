"""Security regressions using disposable config, terminals and loopback sockets only."""
import contextlib
import errno
import fcntl
import importlib.util
import io
import os
from pathlib import Path
import pty
import socket
import subprocess
import sys
import tempfile
import termios
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / 'scripts/funk/audio-preview/setup_support.py'
SPEC = importlib.util.spec_from_file_location('audio_security_support', SCRIPT)
SUPPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUPPORT)


class AudioSecurityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.installation = SUPPORT.Installation(self.root)
        self.installation.config.parent.mkdir(parents=True)
        self.installation.create_config(REPO / 'scripts/funk/audio-preview/ubuntu/server.ini', 64741, True)
        self.password = self.installation.settings()['serverpassword']  # Disposable generated test credential.

    def command(self, *args):
        return [sys.executable, str(SCRIPT), 'info', '--root', str(self.root), *args]

    def test_all_info_report_variants_hide_password(self):
        for arguments in ((), (True,), (False,)):
            with self.subTest(arguments=arguments):
                output, errors = io.StringIO(), io.StringIO()
                with patch.object(SUPPORT, 'run', return_value=''), contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                    self.installation.info(*arguments)
                self.assertIn('Port:          64741', output.getvalue())
                self.assertIn('--show-password', output.getvalue())
                self.assertNotIn(self.password, output.getvalue() + errors.getvalue())

    def test_cli_reports_and_explicit_show_refuse_redirected_output(self):
        for arguments in ((), ('--hide-password',), ('--show-password',)):
            with self.subTest(arguments=arguments):
                result = subprocess.run(self.command(*arguments), capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1 if '--show-password' in arguments else 0)
                self.assertNotIn(self.password, result.stdout + result.stderr)

    def terminal_command(self, *, show=True, redirect=False, controlling=True):
        master, slave = pty.openpty()
        def setup_terminal():
            os.setsid()
            if controlling:
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)
        process = None
        try:
            process = subprocess.Popen(self.command(*(['--show-password'] if show else [])),
                                       stdin=slave, stdout=subprocess.PIPE if redirect else slave,
                                       stderr=subprocess.PIPE, preexec_fn=setup_terminal)
            os.close(slave)
            slave = None
            output, errors = process.communicate(timeout=10)
            terminal = bytearray()
            while True:
                try:
                    chunk = os.read(master, 8192)
                    if not chunk:
                        break
                    terminal.extend(chunk)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    break  # Linux PTY slave closed.
            return process.returncode, output or b'', errors, bytes(terminal)
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            if slave is not None:
                os.close(slave)
            os.close(master)

    def test_explicit_interactive_display_uses_controlling_terminal_only(self):
        code, output, errors, terminal = self.terminal_command()
        self.assertEqual(code, 0, errors)
        self.assertIn(self.password.encode(), terminal)
        self.assertNotIn(self.password.encode(), output + errors)

    def test_interactive_default_does_not_reveal_password(self):
        code, output, errors, terminal = self.terminal_command(show=False)
        self.assertEqual(code, 0, errors)
        self.assertNotIn(self.password.encode(), output + errors + terminal)

    def test_redirected_stdout_cannot_leak_to_log_or_terminal(self):
        code, output, errors, terminal = self.terminal_command(redirect=True)
        self.assertEqual(code, 1)
        self.assertNotIn(self.password.encode(), output + errors + terminal)

    def test_missing_controlling_terminal_fails_closed(self):
        code, output, errors, terminal = self.terminal_command(controlling=False)
        self.assertEqual(code, 1)
        self.assertNotIn(self.password.encode(), output + errors + terminal)

    def test_unexpected_regular_file_is_not_truncated_or_written(self):
        target = self.root / 'unexpected-terminal'
        target.write_bytes(b'PRESERVE EXISTING FILE')
        original_open = os.open
        descriptors = []
        def open_unexpected(path, flags):
            self.assertEqual(path, '/dev/tty')
            descriptor = original_open(target, flags)
            descriptors.append(descriptor)
            return descriptor
        with patch.object(sys.stdin, 'isatty', return_value=True), patch.object(sys.stdout, 'isatty', return_value=True):
            with patch.object(SUPPORT.os, 'open', side_effect=open_unexpected):
                with self.assertRaises(SUPPORT.SetupError):
                    self.installation.show_password()
        self.assertEqual(target.read_bytes(), b'PRESERVE EXISTING FILE')
        for descriptor in descriptors:
            with self.assertRaises(OSError):
                os.fstat(descriptor)

    def test_unexpected_symlink_is_not_followed(self):
        target = self.root / 'preserved'
        target.write_bytes(b'PRESERVE SYMLINK TARGET')
        link = self.root / 'unexpected-terminal'
        link.symlink_to(target)
        original_open = os.open
        with patch.object(sys.stdin, 'isatty', return_value=True), patch.object(sys.stdout, 'isatty', return_value=True):
            with patch.object(SUPPORT.os, 'open', side_effect=lambda path, flags: original_open(link, flags)):
                with self.assertRaises(OSError):
                    self.installation.show_password()
        self.assertEqual(target.read_bytes(), b'PRESERVE SYMLINK TARGET')

    def test_bound_non_listening_tcp_on_other_local_address_is_rejected(self):
        with socket.socket() as holder:
            holder.bind(('127.0.0.2', 0))
            port = holder.getsockname()[1]
            # A loopback-only "fix" would miss this actual occupied port.
            with socket.socket() as incomplete_probe:
                incomplete_probe.bind(('127.0.0.1', port))
            with self.assertRaises(SUPPORT.SetupError):
                self.installation.check_port(port)
            self.assertEqual(holder.getsockname()[1], port)

    def test_udp_conflict_remains_bound_and_is_not_modified(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as holder:
            holder.bind(('127.0.0.1', 0))
            port = holder.getsockname()[1]
            with self.assertRaises(SUPPORT.SetupError):
                self.installation.check_port(port)
            self.assertEqual(holder.getsockname()[1], port)

    def test_ipv6_bound_non_listening_tcp_is_rejected(self):
        with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as holder:
            holder.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
            holder.bind(('::1', 0))
            with self.assertRaises(SUPPORT.SetupError):
                self.installation.check_port(holder.getsockname()[1])

    def test_free_probe_closes_sockets_and_never_listens_or_receives(self):
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        original_socket = socket.socket
        probes = []
        class BindOnlyProbe(original_socket):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                probes.append(self)
            def listen(self, *args):
                raise AssertionError('Availability probes must never listen')
            def accept(self):
                raise AssertionError('Availability probes must never accept')
            def recv(self, *args):
                raise AssertionError('Availability probes must never receive')
            def recvfrom(self, *args):
                raise AssertionError('Availability probes must never receive')
            def send(self, *args):
                raise AssertionError('Availability probes must never send')
            def sendto(self, *args):
                raise AssertionError('Availability probes must never send')
        with patch.object(SUPPORT.socket, 'socket', BindOnlyProbe):
            self.installation.check_port(port)
        self.assertEqual(len(probes), 4)
        self.assertTrue(all(probe.fileno() == -1 for probe in probes))
        with original_socket() as tcp, original_socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            tcp.bind(('127.0.0.1', port))
            udp.bind(('127.0.0.1', port))

    def test_failed_socket_inventory_never_assumes_port_is_free(self):
        with patch.object(SUPPORT.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', 'failed')):
            with self.assertRaises(SUPPORT.SetupError):
                self.installation.check_port(49152)


if __name__ == '__main__':
    unittest.main(verbosity=2)
