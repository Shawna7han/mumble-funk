"""Deployment checks without root, systemd or writes to system directories.

Run: python3 tests/funk-install/test_install.py /path/to/mumble-server
Network permission is needed for the concurrent-server smoke test.
"""
import hashlib
from contextlib import closing
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[2]


class InstallationTests(unittest.TestCase):
    def test_supported_operating_systems(self):
        # Point releases use VERSION_ID=24.04/26.04, not the display version.
        cases = [("ubuntu", "24.04", True), ("ubuntu", "26.04", True),
                 ("ubuntu", "22.04", False), ("ubuntu", "25.10", False),
                 ("ubuntu", "28.04", False), ("debian", "26.04", False),
                 ("linuxmint", "26.04", False), ("", "", False),
                 ("ubuntu", "", False), ("", "26.04", False)]
        for os_id, version_id, supported in cases:
            with self.subTest(os_id=os_id, version_id=version_id):
                result = subprocess.run([
                    "bash", "-c", 'source "$1"; funk_require_supported_os "$2" "$3"',
                    "platform-test", str(REPO / "scripts/funk/platform.sh"), os_id, version_id,
                ], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0 if supported else 1)
                if not supported:
                    self.assertIn("Ubuntu 24.04 LTS und 26.04 LTS", result.stderr)

    def test_staged_repeat_preserves_configuration_database_and_standard_server(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            protected = {}
            for name in ("etc/mumble-server.ini", "var/lib/mumble-server/server.sqlite",
                         "var/log/mumble-server.log", "etc/systemd/system/mumble-server.service"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"EXISTING STANDARD SERVER\n")
                protected[path] = path.read_bytes()
            command = ["bash", str(REPO / "scripts/funk/install.sh"), "--stage", directory, str(BINARY)]
            subprocess.run(command, check=True)
            config = root / "etc/mumble-funk/server.ini"
            self.assertIn("port=64739\n", config.read_text())
            self.assertIn("voicediagnostics=false\n", config.read_text())
            self.assertIn("simplex=true\n", config.read_text())
            self.assertNotIn("@PASSWORD@", config.read_text())
            self.assertEqual(config.stat().st_mode & 0o777, 0o640)
            config.write_text(config.read_text().replace("voicediagnostics=false", "voicediagnostics=true")
                              .replace("simplex=true", "simplex=false"))
            protected[config] = config.read_bytes()
            db = root / "var/lib/mumble-funk/server.sqlite"
            with sqlite3.connect(db) as connection:
                connection.execute("CREATE TABLE preserved (value TEXT)")
                connection.execute("INSERT INTO preserved VALUES ('EXISTING CUSTOM DATABASE')")
            connection.close()
            protected[db] = db.read_bytes()
            subprocess.run(command, check=True)
            for path, content in protected.items():
                self.assertEqual(path.read_bytes(), content, str(path))
            deployed = root / "opt/mumble-funk/current/mumble-server"
            self.assertEqual(hashlib.sha256(deployed.read_bytes()).digest(), hashlib.sha256(BINARY.read_bytes()).digest())

    def test_wrong_version_and_root_stage_are_rejected_before_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = root / "wrong-server"
            fake.write_text('#!/bin/sh\necho "Mumble server 1.7.0"\n')
            fake.chmod(0o755)
            command = ["bash", str(REPO / "scripts/funk/install.sh"), "--stage"]
            result = subprocess.run(command + [str(root / "stage"), str(fake)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root / "stage").exists())
            result = subprocess.run(command + ["/", str(BINARY)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_new_binary_keeps_previous_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / "stage"
            fake = root / "fake-server"
            command = ["bash", str(REPO / "scripts/funk/install.sh"), "--stage", str(stage), str(fake)]
            fake.write_text('#!/bin/sh\necho "Mumble server 1.5.939 build A"\n')
            fake.chmod(0o755)
            subprocess.run(command, check=True)
            previous = (stage / "opt/mumble-funk/current").resolve()
            fake.write_text('#!/bin/sh\necho "Mumble server 1.5.939 build B"\n')
            subprocess.run(command, check=True)
            self.assertEqual((stage / "opt/mumble-funk/previous").resolve(), previous)
            self.assertNotEqual((stage / "opt/mumble-funk/current").resolve(), previous)
            subprocess.run(command, check=True)
            self.assertEqual((stage / "opt/mumble-funk/previous").resolve(), previous)

    def test_parallel_instances_on_64738_and_64739(self):
        # Refuse to disturb any pre-existing listener, including UDP-only ones.
        for port in (64738, 64739):
            for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
                with socket.socket(socket.AF_INET, kind) as probe:
                    try:
                        probe.bind(("127.0.0.1", port))
                    except OSError:
                        self.skipTest(f"Port {port} already occupied; no server stopped")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            processes = []
            handles = []
            try:
                for port in (64738, 64739):
                    state = root / str(port)
                    state.mkdir()
                    config = (REPO / "scripts/funk/ubuntu/server.ini").read_text()
                    config = config.replace("/var/lib/mumble-funk", str(state))
                    config = config.replace("/var/log/mumble-funk", str(state))
                    config = config.replace("/usr/local/share/mumble-funk/audio", str(REPO/"assets/audio"))
                    config = config.replace("0.0.0.0", "127.0.0.1").replace("port=64739", f"port={port}")
                    config = config.replace("@PASSWORD@", "test-only")
                    config = config.replace("voicediagnostics=false", f"voicediagnostics={'true' if port == 64739 else 'false'}")
                    config = config.replace("simplex=true", f"simplex={'true' if port == 64739 else 'false'}")
                    config = config.replace("#simplexdiagnostics=false", "simplexdiagnostics=true")
                    config = config.replace("#simplexlogfile=", "simplexlogfile=")
                    (state / "server.ini").write_text(config)
                    log = open(state / "server.log", "wb")
                    handles.append(log)
                    processes.append(subprocess.Popen([str(BINARY), "-ini", str(state / "server.ini"), "-fg"], stdout=log, stderr=subprocess.STDOUT))
                deadline = time.monotonic() + 25
                for port, process in zip((64738, 64739), processes):
                    while True:
                        self.assertIsNone(process.poll(), (root / str(port) / "server.log").read_text())
                        try:
                            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                                break
                        except OSError:
                            if time.monotonic() > deadline:
                                self.fail(f"Server on {port} did not start")
                            time.sleep(0.05)
                for port in (64738, 64739):
                    with closing(sqlite3.connect(root / str(port) / "server.sqlite")) as db:
                        self.assertEqual(db.execute("PRAGMA integrity_check").fetchone()[0], "ok")
                self.assertFalse(list((root / "64738").glob("voice.*.jsonl")))
            finally:
                for process in processes:
                    process.terminate()
                for process in processes:
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                for handle in handles:
                    handle.close()
            self.assertTrue(list((root / "64739").glob("voice.*.jsonl")))
            self.assertFalse(list((root / "64738").glob("simplex.*.jsonl")))
            self.assertIn("Simplex enabled:", (root / "64739/server.log").read_text())
            self.assertNotIn("Simplex enabled:", (root / "64738/server.log").read_text())
            self.assertIn("simplex_log_started", (root / "64739/simplex.1.jsonl").read_text())
            for port in (64738, 64739):
                self.assertGreater((root / str(port) / "server.log").stat().st_size, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
