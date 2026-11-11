"""Isolated preview deployment; never writes host system directories."""
import hashlib
import importlib.util
import os
from pathlib import Path
import socket
import ssl
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[2]
PREVIEW = REPO / "scripts/funk/audio-preview"
SPEC = importlib.util.spec_from_file_location("audio_preview_support", PREVIEW / "setup_support.py")
SUPPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUPPORT)


class PreviewTests(unittest.TestCase):
    def stage(self, root):
        subprocess.run(["bash", str(PREVIEW / "install.sh"), "--setup", "--stage", str(root), str(BINARY)], check=True)

    def test_isolated_repeat_preserves_all_instances(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            protected = {}
            for instance in ("mumble-server", "mumble-funk", "mumble-funk-audio"):
                for name in (f"etc/{instance}/server.ini", f"var/lib/{instance}/server.sqlite",
                             f"var/log/{instance}/server.log", f"etc/systemd/system/{instance}.service",
                             f"usr/local/sbin/{instance}-info"):
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b"PROTECTED EXISTING INSTANCE\n")
                    protected[path] = path.read_bytes()
            self.stage(root)
            config = root / "etc/mumble-funk-audio-engine/server.ini"
            values = SUPPORT.ini_values(config)
            self.assertEqual(values["port"], "64741")
            self.assertEqual(values["simplex"], "true")
            for line in ("audiofeedback=false", "busytone=true", "busytonevolume=50",
                         "simplexdiagnostics=false", "voicediagnostics=false"):
                self.assertIn(line + "\n", config.read_text())
            self.assertEqual(config.stat().st_mode & 0o777, 0o640)
            self.assertRegex(values["serverpassword"], r"^[a-f0-9]{32}$")
            config.write_text(config.read_text().replace("busytonevolume=50", "busytonevolume=23"))
            protected[config] = config.read_bytes()
            database = root / "var/lib/mumble-funk-audio-engine/server.sqlite"
            with sqlite3.connect(database) as db:
                db.execute("CREATE TABLE preserved(value TEXT)")
                db.execute("INSERT INTO preserved VALUES ('keep me')")
            protected[database] = database.read_bytes()
            cert = root / "var/lib/mumble-funk-audio-engine/cert.pem"
            cert.write_text("EXISTING CERTIFICATE")
            protected[cert] = cert.read_bytes()
            self.stage(root)
            for path, content in protected.items():
                self.assertEqual(path.read_bytes(), content, str(path))
            deployed = root / "opt/mumble-funk-audio-engine/current/mumble-server"
            self.assertEqual(hashlib.sha256(deployed.read_bytes()).digest(), hashlib.sha256(BINARY.read_bytes()).digest())
            licenses = root / "opt/mumble-funk-audio-engine/current/licenses"
            self.assertEqual((licenses / "LICENSE").read_bytes(), (REPO / "LICENSE").read_bytes())
            self.assertIn("Xiph.Org", (licenses / "Opus.txt").read_text())
            unit = (root / "etc/systemd/system/mumble-funk-audio-engine.service").read_text()
            self.assertIn("User=mumble-funk-audio-engine\n", unit)
            self.assertIn("ReadWritePaths=/var/lib/mumble-funk-audio-engine /var/log/mumble-funk-audio-engine", unit)

    def test_protected_ports_and_foreign_databases_rejected(self):
        for port in (64738, 64739, 64740, 0, 65536, "other"):
            with self.subTest(port=port), self.assertRaises(SUPPORT.SetupError):
                SUPPORT.valid_port(port)
        with tempfile.TemporaryDirectory() as temporary:
            instance = SUPPORT.Installation(temporary)
            instance.config.parent.mkdir(parents=True)
            for database in ("/var/lib/mumble-server/server.sqlite", "/var/lib/mumble-funk/server.sqlite",
                             "/var/lib/mumble-funk-audio/server.sqlite"):
                instance.config.write_text(f"database={database}\n")
                with self.assertRaises(SUPPORT.SetupError):
                    instance.preflight()

    def test_foreign_port_refused_before_files_written(self):
        for port in (64738, 64739, 64740):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "stage"
                result = subprocess.run(["bash", str(PREVIEW / "install.sh"), "--stage", str(root), str(BINARY),
                                         "--port", str(port)], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(root.exists())

    def test_build_and_entrypoint_are_separate(self):
        build = (PREVIEW / "build.sh").read_text()
        self.assertIn('== feature/audio-engine', build)
        self.assertIn('$repo/build/audio-preview', build)
        self.assertNotIn('$repo/build/funk', build)
        self.assertIn('funk_require_supported_source', (REPO / 'scripts/funk/install.sh').read_text())
        self.assertIn('libopus-dev', (PREVIEW / "setup.sh").read_text())
        self.assertIn('audio-preview/setup.sh', (REPO / "scripts/funk/setup-audio-preview.sh").read_text())
        result = subprocess.run(["bash", str(REPO / "scripts/funk/setup-audio-preview.sh"), "--help"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn('audio-preview/setup.sh', result.stdout)

    def test_three_real_servers_and_independent_state(self):
        # A smoke test of four actual processes, not systemd supervision.
        # Own temporary ports only, never bind the ports of real VM services.
        reservations = []
        ports = []
        for _ in range(4):
            tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tcp.bind(('127.0.0.1', 0))
            port = tcp.getsockname()[1]
            udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            udp.bind(('127.0.0.1', port))
            reservations.extend((tcp, udp))
            ports.append(port)
        for reservation in reservations:
            reservation.close()
        with tempfile.TemporaryDirectory() as temporary:
            processes, logs = [], []
            try:
                for port in ports:
                    state = Path(temporary) / str(port)
                    state.mkdir()
                    ini = (PREVIEW / "ubuntu/server.ini").read_text()
                    ini = ini.replace('/var/lib/mumble-funk-audio-engine', str(state)).replace('/var/log/mumble-funk-audio-engine', str(state))
                    ini = ini.replace('0.0.0.0', '127.0.0.1').replace('port=64741', f'port={port}').replace('@PASSWORD@', 'test-only')
                    ini = ini.replace("audiofeedback=false", "audiofeedback=true" if port == ports[-1] else "audiofeedback=false")
                    ini += f"\nsimplex={'true' if port != ports[0] else 'false'}\n"
                    config = state / "server.ini"
                    config.write_text(ini)
                    log = open(state / "server.log", "wb")
                    logs.append(log)
                    processes.append(subprocess.Popen([str(BINARY), '-ini', str(config), '-fg'], stdout=log, stderr=subprocess.STDOUT))
                deadline = time.monotonic() + 20
                for port, process in zip(ports, processes):
                    while True:
                        self.assertIsNone(process.poll())
                        try:
                            with socket.create_connection(('127.0.0.1', port), timeout=.2):
                                break
                        except OSError:
                            if time.monotonic() > deadline:
                                self.fail(f"No listener on {port}")
                            time.sleep(.05)
                certificates = []
                for port in ports:
                    log = (Path(temporary) / str(port) / 'server.log').read_text()
                    self.assertEqual('Experimental audio feedback enabled:' in log, port == ports[-1])
                    with sqlite3.connect(Path(temporary) / str(port) / 'server.sqlite') as db:
                        self.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0], 'ok')
                        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                        context.minimum_version = ssl.TLSVersion.TLSv1_2
                        context.check_hostname = False
                        context.verify_mode = ssl.CERT_NONE  # Disposable test server's self-signed certificate only.
                        with socket.create_connection(('127.0.0.1', port)) as raw:
                            with context.wrap_socket(raw, server_hostname='localhost') as tls:
                                certificates.append(tls.getpeercert(binary_form=True))
                self.assertEqual(len(set(certificates)), 4)
            finally:
                for process in processes:
                    process.terminate()
                for process in processes:
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                for log in logs:
                    log.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
