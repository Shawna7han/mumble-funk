"""Setup safety tests: real staged files, isolated orchestration, explicit service mocks."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import subprocess
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("setup_support", REPO / "scripts/funk/setup_support.py")
SUPPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUPPORT)


class SupportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.installation = SUPPORT.Installation(self.root)
        self.installation.config.parent.mkdir(parents=True)

    def configure(self, text="", port=64739):
        self.installation.create_config(REPO / "scripts/funk/ubuntu/server.ini", port, True)
        if text:
            self.installation.config.write_text(self.installation.config.read_text() + text)

    def test_new_install_defaults_and_random_password(self):
        self.configure()
        content = self.installation.config.read_text()
        for setting in ("simplex=true", "simplextimeout=500", "simplexretrytimeout=500",
                        "simplexdiagnostics=true", "voicediagnostics=false"):
            self.assertIn(setting + "\n", content)
        values = self.installation.settings()
        self.assertRegex(values["serverpassword"], r"^[a-f0-9]{32}$")
        self.assertEqual(self.installation.config.stat().st_mode & 0o777, 0o640)
        first = values["serverpassword"]
        self.installation.config.unlink()
        self.configure()
        self.assertNotEqual(first, self.installation.settings()["serverpassword"])

    def test_custom_configuration_password_and_port_never_changed(self):
        self.configure(port=64740)
        original = self.installation.config.read_bytes() + b"\n# personal settings\nusers=7\n"
        self.installation.config.write_bytes(original)
        self.installation.create_config(REPO / "scripts/funk/ubuntu/server.ini", 65000, True)
        self.assertEqual(self.installation.config.read_bytes(), original)
        self.assertEqual(self.installation.preflight(), 64740)
        with self.assertRaisesRegex(SUPPORT.SetupError, "behält ihren Port"):
            self.installation.preflight("65000")

    def test_database_overrides_read_only_and_foreign_database_rejected(self):
        self.configure()
        database = self.root / "var/lib/mumble-funk/server.sqlite"
        database.parent.mkdir(parents=True)
        with sqlite3.connect(database) as db:
            db.execute("CREATE TABLE config (server_id INTEGER, key TEXT, value TEXT)")
            db.execute("INSERT INTO config VALUES (1, 'port', '64745')")
            db.execute("INSERT INTO config VALUES (1, 'serverpassword', 'saved-db-password')")
        db.close()
        original = database.read_bytes()
        self.assertEqual(self.installation.preflight(), 64745)
        self.assertEqual(self.installation.settings()["serverpassword"], "saved-db-password")
        self.assertEqual(database.read_bytes(), original)
        self.installation.config.write_text("database=/var/lib/mumble-server/server.sqlite\n")
        with self.assertRaisesRegex(SUPPORT.SetupError, "fremder Daten"):
            self.installation.preflight()

    def test_orphan_state_never_generates_a_new_password(self):
        state = self.root / "var/lib/mumble-funk"
        state.mkdir(parents=True)
        (state / "server.sqlite").write_bytes(b"protected")
        with self.assertRaisesRegex(SUPPORT.SetupError, "server.ini fehlt"):
            self.installation.preflight()
        self.assertFalse(self.installation.config.exists())

    def test_ini_duplicates_fail_closed_and_quoted_password(self):
        self.installation.config.write_text('port=64740\nserverpassword="quoted\\\\secret"\n')
        self.assertEqual(self.installation.settings()["serverpassword"], "quoted\\secret")
        self.installation.config.write_text("port=64739\nport=65000\n")
        with self.assertRaisesRegex(SUPPORT.SetupError, "mehrfach"):
            self.installation.preflight()

    def test_port_limits(self):
        for value in ("0", "80", "64738", "65536", "-1", "a", "1;reboot"):
            with self.subTest(value=value), self.assertRaises(SUPPORT.SetupError):
                SUPPORT.valid_port(value)
        for value in (1024, 64739, 65535):
            self.assertEqual(SUPPORT.valid_port(value), value)

    def test_foreign_tcp_and_udp_listener_and_own_update(self):
        for transport in ("tcp", "udp"):
            result = subprocess.CompletedProcess([], 0, f'{transport} LISTEN 0 0 0.0.0.0:64739 *:* users:(("other",pid=12,fd=1))\n', "")
            with patch.object(SUPPORT.subprocess, "run", return_value=result):
                with self.assertRaisesRegex(SUPPORT.SetupError, "bereits"):
                    self.installation.check_port(64739)
                self.installation.check_port(64739, 12)
                with self.assertRaises(SUPPORT.SetupError):
                    self.installation.check_port(64739, 13)
        opaque = subprocess.CompletedProcess([], 0, 'tcp LISTEN 0 0 *:64739 *:*\n', "")
        with patch.object(SUPPORT.subprocess, "run", return_value=opaque), self.assertRaises(SUPPORT.SetupError):
            self.installation.check_port(64739, 12)

    def test_bound_but_not_listening_socket_is_protected(self):
        empty = subprocess.CompletedProcess([], 0, "", "")
        for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
            with socket.socket(socket.AF_INET, kind) as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
                with patch.object(SUPPORT.subprocess, "run", return_value=empty), self.assertRaises(SUPPORT.SetupError):
                    self.installation.check_port(port)

    def test_foreign_service_name_rejected(self):
        with patch.object(SUPPORT, "run", side_effect=["/etc/systemd/system/mumble-funk.service", "/usr/bin/other-server"]):
            with self.assertRaisesRegex(SUPPORT.SetupError, "Fremder Dienst"):
                self.installation.check_service_owner()

    def test_verify_requires_new_startup_and_both_transports(self):
        self.configure()
        self.installation.log.parent.mkdir(parents=True)
        old = "Simplex enabled: old\nServer listening on old\n"
        self.installation.log.write_text(old)
        responses = lambda *args, **kwargs: "active" if args[1] == "is-active" else "enabled"
        with patch.object(SUPPORT, "run", side_effect=responses), \
                patch.object(self.installation, "service_pid", return_value=12), \
                patch.object(self.installation, "check_port") as check:
            with self.assertRaisesRegex(SUPPORT.SetupError, "neue Startmeldung"):
                self.installation.verify(len(old), deadline=0.01)
            self.installation.log.write_text(old + "Simplex enabled: new\nServer listening on new\n")
            self.installation.verify(len(old))
            check.assert_called_with(64739, 12, require_listener=True)
        one = subprocess.CompletedProcess([], 0, 'tcp LISTEN 0 0 *:64739 *:* users:(("mumble",pid=12,fd=1))\n', "")
        with patch.object(SUPPORT.subprocess, "run", return_value=one), self.assertRaisesRegex(SUPPORT.SetupError, "TCP und UDP"):
            self.installation.check_port(64739, 12, require_listener=True)

    def test_info_password_hidden_in_setup_and_multiple_addresses(self):
        self.configure()
        secret = self.installation.settings()["serverpassword"]
        with patch.object(SUPPORT, "run", side_effect=["active", "127.0.0.1 10.0.0.2 192.168.1.2 fe80::1"]):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.installation.info(hide_password=True)
            self.assertNotIn(secret, output.getvalue())
            self.assertIn("Mehrere Adressen", output.getvalue())
        with patch.object(SUPPORT, "run", side_effect=["active", "10.0.0.2"]):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.installation.info()
            self.assertIn(secret, output.getvalue())


STUB = r'''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys
name=pathlib.Path(sys.argv[0]).name
args=sys.argv[1:]
repo=pathlib.Path(os.environ['MOCK_REPO'])
root=pathlib.Path(os.environ['MOCK_ROOT'])
fail=os.environ.get('FAIL_STAGE','')
with (repo/'calls.jsonl').open('a') as f: f.write(json.dumps([name]+args)+'\n')
if name=='df':
    print('Filesystem 1024-blocks Used Available Capacity Mounted')
    print('test 33554432 1000 '+os.environ.get('MOCK_FREE_KIB','16777216')+' 1% /')
elif name=='git':
    if '--show-current' in args: print(os.environ.get('MOCK_BRANCH','funk-v0.1'))
    elif 'config' in args: print('https://github.com/Shawna7han/mumble-funk.git')
    elif '--git-path' in args: print(repo/'.git/funk-setup.lock')
    elif 'rev-parse' in args: print('a'*40)
    elif 'ls-remote' in args: print('a'*40+'\trefs/heads/'+os.environ.get('MOCK_BRANCH','feature/squelch-audio'))
    if 'submodule' in args and fail=='network': sys.exit(128)
elif name=='systemctl': print('259')
elif name=='dpkg-query':
    if args[-1] in ('cmake','python3'):
        sys.exit(1)
    print('install ok installed',end='')
elif name=='apt-get':
    if fail=='apt-update' and args[0]=='update': sys.exit(100)
    if fail=='apt-install' and args[0]=='install': sys.exit(100)
elif name=='cmake':
    if fail=='build': sys.exit(2)
elif name=='ctest':
    if fail=='tests': sys.exit(8)
elif name=='sudo':
    if args==['-v']: sys.exit(0)
    if args[0]=='env':
        args=args[1:]
        while args and '=' in args[0]: args=args[1:]
    elif args[0]=='python3': args+=['--root',str(root)]
    elif args[0]=='bash' and args[1].endswith('install.sh'):
        args+=['--stage',str(root),str(repo/'fake-server')]
    elif args[0]=='/usr/local/sbin/mumble-funk-info':
        args=['python3',str(repo/'scripts/funk/setup_support.py'),'--root',str(root)]+args[1:]
    sys.exit(subprocess.run(args).returncode)
'''


class OrchestrationTests(unittest.TestCase):
    def setUp(self):
        if os.geteuid() == 0:
            self.skipTest("Orchestrierung muss als normaler Benutzer laufen")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name) / "repo"
        self.repo.mkdir()
        shutil.copytree(REPO / "scripts", self.repo / "scripts")
        shutil.copytree(REPO / "assets/audio", self.repo / "assets/audio")
        (self.repo / ".git").mkdir()
        (self.repo / "test-os-release").write_text('ID=ubuntu\nVERSION_ID="26.04"\n')
        (self.repo / "test-meminfo").write_text("MemTotal: 8388608 kB\nSwapTotal: 0 kB\n")
        setup = self.repo / "scripts/funk/setup.sh"
        # Test-only I/O redirection in a temporary copy; no OS bypass in production.
        setup.write_text(setup.read_text().replace(". /etc/os-release", '. "$repo/test-os-release"')
                         .replace("/proc/meminfo", '"$repo/test-meminfo"'))
        binary = self.repo / "fake-server"
        binary.write_text('#!/bin/sh\necho "Mumble server 1.5.939"\n')
        binary.chmod(0o755)
        bindir = self.repo / "mock-bin"
        bindir.mkdir()
        for name in ("sudo", "apt-get", "git", "dpkg-query", "systemctl", "cmake", "ctest", "df"):
            path = bindir / name
            path.write_text(STUB)
            path.chmod(0o755)
        self.env = dict(os.environ, PATH=str(bindir) + ":" + os.environ["PATH"],
                        MOCK_REPO=str(self.repo), MOCK_ROOT=str(self.repo / "stage"))

    def execute(self, fail="", args=()):
        return subprocess.run(["bash", str(self.repo / "scripts/funk/setup.sh"), *args],
                              env=dict(self.env, FAIL_STAGE=fail), capture_output=True, text=True)

    def calls(self):
        path = self.repo / "calls.jsonl"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def test_new_install_and_repeat_preserve_custom_state(self):
        first = self.execute(args=("--port", "64740"))
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        config = self.repo / "stage/etc/mumble-funk/server.ini"
        data = config.read_bytes().replace(b"simplex=true", b"simplex=false") + b"\nusers=5\n"
        config.write_bytes(data)
        second = self.execute()
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        self.assertEqual(config.read_bytes(), data)
        self.assertIn("Deaktiviert", second.stdout)
        self.assertIn("sudo mumble-funk-info", second.stdout)
        secret = SUPPORT.ini_values(config)["serverpassword"]
        self.assertNotIn(secret, first.stdout + second.stdout)
        for call in self.calls():
            if call[0] == "sudo":
                self.assertFalse(any(arg.endswith("build.sh") for arg in call))

    def test_restrictive_umask_keeps_program_paths_traversable(self):
        result = subprocess.run(["bash", "-c", 'umask 077; exec bash "$1"', "setup-test",
                                 str(self.repo / "scripts/funk/setup.sh")],
                                env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        base = self.repo / "stage/opt/mumble-funk"
        for directory in (base, base / "releases", (base / "current").resolve()):
            self.assertEqual(directory.stat().st_mode & 0o777, 0o755, str(directory))

    def test_experimental_ubuntu24_install_requires_and_forwards_opt_in(self):
        self.env['MOCK_BRANCH'] = 'feature/squelch-audio'
        (self.repo / 'test-os-release').write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
        refused = self.execute()
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn('--experimental-squelch', refused.stderr)
        self.assertFalse((self.repo / 'stage/etc/mumble-funk/server.ini').exists())
        result = self.execute(args=('--experimental-squelch',))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('EXPERIMENTELL', result.stderr)
        installers = [call for call in self.calls() if call[0] == 'sudo' and any(arg.endswith('install.sh') for arg in call)]
        self.assertTrue(installers)
        self.assertIn('--experimental-squelch', installers[-1])
        info = (self.repo / 'stage/opt/mumble-funk/current/BUILD-INFO').read_text()
        self.assertIn('experimental=true', info)

    def test_dev_ubuntu24_opt_in_and_update_preserve_existing_state(self):
        self.env['MOCK_BRANCH'] = 'dev'
        (self.repo / 'test-os-release').write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
        for args in ((), ('--experimental-audio-defaults',)):
            refused = self.execute(args=args)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn('--experimental-dev', refused.stderr)
            self.assertFalse((self.repo / 'stage').exists())
            self.assertFalse(any(c[0] in ('cmake', 'apt-get', 'sudo') for c in self.calls()))
        result = self.execute(args=('--experimental-dev',))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('EXPERIMENTELL: dev-Testinstallation', result.stderr)
        # Build runs the same guard a second time, proving setup forwarded opt-in.
        self.assertGreaterEqual(result.stderr.count('dev-Testinstallation'), 2)
        installers = [c for c in self.calls() if c[0] == 'sudo' and any(a.endswith('install.sh') for a in c)]
        self.assertIn('--experimental-dev', installers[-1])
        info = (self.repo / 'stage/opt/mumble-funk/current/BUILD-INFO').read_text()
        self.assertIn('branch=dev\n', info)
        self.assertIn('revision=' + 'a' * 40 + '\n', info)
        self.assertIn('experimental=true\n', info)
        config = self.repo / 'stage/etc/mumble-funk/server.ini'
        config.write_text(config.read_text().replace('audiofeedback=true', 'audiofeedback=false')
                          .replace('squelchclosetimeoutfallback=true\n', ''))
        database = self.repo / 'stage/var/lib/mumble-funk/server.sqlite'
        with contextlib.closing(sqlite3.connect(database)) as db:
            db.execute('CREATE TABLE config (server_id INTEGER, key TEXT, value TEXT)')
            db.execute("INSERT INTO config VALUES (1, 'port', '64739')")
            db.commit()
        custom_wav = self.repo / 'stage/etc/mumble-funk/sounds/custom.wav'
        custom_wav.write_bytes(b'preserved custom audio')
        before = {p: p.read_bytes() for p in (config, database, custom_wav)}
        result = self.execute(args=('--experimental-dev',))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for path, content in before.items():
            self.assertEqual(path.read_bytes(), content, str(path))

    def test_direct_dev_build_requires_opt_in_before_cmake(self):
        self.env['MOCK_BRANCH'] = 'dev'
        command = ['bash', str(self.repo / 'scripts/funk/build.sh')]
        refused = subprocess.run(command, env=self.env, capture_output=True, text=True)
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn('--experimental-dev', refused.stderr)
        self.assertFalse(any(c[0] in ('cmake', 'ctest') for c in self.calls()))
        result = subprocess.run(command + ['--experimental-dev'], env=self.env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(any(c[0] == 'cmake' and '--build' in c for c in self.calls()))
        self.assertTrue(any(c[0] == 'ctest' for c in self.calls()))
        self.assertIn('install.sh --experimental-dev', result.stdout)

    def test_radio_noise_ubuntu24_opt_in_and_update_without_private_asset(self):
        self.env['MOCK_BRANCH'] = 'feature/radio-noise'
        (self.repo / 'test-os-release').write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
        for args in ((), ('--experimental-dev',), ('--experimental-squelch',)):
            refused = self.execute(args=args)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn('--experimental-radio-noise', refused.stderr)
            self.assertFalse((self.repo / 'stage').exists())
            self.assertFalse(any(c[0] in ('cmake', 'apt-get', 'sudo') for c in self.calls()))
        result = self.execute(args=('--experimental-radio-noise',))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreaterEqual(result.stderr.count('EXPERIMENTELL: Audio-Vorschau'), 2)
        installers = [c for c in self.calls() if c[0] == 'sudo' and any(a.endswith('install.sh') for a in c)]
        self.assertIn('--experimental-radio-noise', installers[-1])
        root = self.repo / 'stage'
        info = (root / 'opt/mumble-funk/current/BUILD-INFO').read_text()
        self.assertIn('branch=feature/radio-noise\n', info)
        self.assertIn('experimental=true\n', info)
        config = root / 'etc/mumble-funk/server.ini'
        self.assertIn('radionoise=true\n', config.read_text())
        self.assertEqual((root/'usr/local/share/mumble-funk/audio/radio-noise.wav').read_bytes(),
                         (REPO/'assets/audio/radio-noise.wav').read_bytes())
        config.write_text(config.read_text().replace('audiofeedback=true', 'audiofeedback=false')
                          .replace('radionoise=true', 'radionoise=false')
                          .replace('radionoisefile=/usr/local/share/mumble-funk/audio/radio-noise.wav',
                                   'radionoisefile=/etc/mumble-funk/sounds/radio-noise.wav')
                          .replace('radionoisevolume=20', 'radionoisevolume=10'))
        database = root / 'var/lib/mumble-funk/server.sqlite'
        with contextlib.closing(sqlite3.connect(database)) as db:
            db.execute('CREATE TABLE config (server_id INTEGER, key TEXT, value TEXT)')
            db.commit()
        own = root / 'etc/mumble-funk/sounds/radio-noise.wav'
        own.write_bytes(b'operator audio fixture')
        before = {p: p.read_bytes() for p in (config, database, own)}
        result = self.execute(args=('--experimental-radio-noise',))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for path, content in before.items():
            self.assertEqual(path.read_bytes(), content, str(path))

    def test_direct_radio_noise_build_and_install_require_and_forward_opt_in(self):
        self.env['MOCK_BRANCH'] = 'feature/radio-noise'
        installer = self.repo / 'scripts/funk/install.sh'
        installer.write_text(installer.read_text()
                             .replace('[[ $EUID == 0 ]]', 'true')
                             .replace('. /etc/os-release', '. "$repo/test-os-release"')
                             .replace('/run/lock/mumble-funk-install.lock', str(self.repo / 'install.lock')))
        flock = self.repo / 'mock-bin/flock'
        flock.write_text('#!/bin/sh\necho TEST_STOP_AFTER_SOURCE_GUARD >&2\nexit 73\n')
        flock.chmod(0o755)
        for name in ('build.sh', 'install.sh'):
            command = ['bash', str(self.repo / 'scripts/funk' / name)]
            refused = subprocess.run(command, env=self.env, capture_output=True, text=True)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn('--experimental-radio-noise', refused.stderr)
            result = subprocess.run(command + ['--experimental-radio-noise'], env=self.env,
                                    capture_output=True, text=True)
            if name == 'build.sh':
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('install.sh --experimental-radio-noise', result.stdout)
            else:
                self.assertIn('TEST_STOP_AFTER_SOURCE_GUARD', result.stderr)
            self.assertIn('EXPERIMENTELL', result.stderr)
        self.assertFalse((self.repo / 'stage').exists())

    def test_direct_dev_install_forwards_opt_in_to_source_guard(self):
        self.env['MOCK_BRANCH'] = 'dev'
        installer = self.repo / 'scripts/funk/install.sh'
        # Exercise production parsing/guard in this disposable copy, as a normal
        # user. Stop at the lock immediately after validation, before deployment.
        installer.write_text(installer.read_text()
                             .replace('[[ $EUID == 0 ]]', 'true')
                             .replace('. /etc/os-release', '. "$repo/test-os-release"')
                             .replace('/run/lock/mumble-funk-install.lock',
                                      str(self.repo / 'install.lock')))
        flock = self.repo / 'mock-bin/flock'
        flock.write_text('#!/bin/sh\necho TEST_STOP_AFTER_SOURCE_GUARD >&2\nexit 73\n')
        flock.chmod(0o755)
        command = ['bash', str(installer)]
        for args in ((), ('--experimental-audio-defaults',)):
            result = subprocess.run(command + list(args), env=self.env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn('--experimental-dev', result.stderr)
            self.assertFalse((self.repo / 'install.lock').exists())
        result = subprocess.run(command + ['--experimental-dev'], env=self.env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('TEST_STOP_AFTER_SOURCE_GUARD', result.stderr)
        self.assertIn('EXPERIMENTELL: dev-Testinstallation', result.stderr)
        self.assertFalse((self.repo / 'stage').exists())

    def test_audio_volume_ubuntu24_install_and_update_preserve_boost(self):
        self.env['MOCK_BRANCH'] = 'feature/audio-volume'
        (self.repo/'test-os-release').write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
        for args in ((), ('--experimental-squelch',)):
            result = self.execute(args=args)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('--experimental-audio-volume', result.stderr)
            self.assertFalse((self.repo/'stage/etc/mumble-funk/server.ini').exists())
        result = self.execute(args=('--experimental-audio-volume',))
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        installers = [c for c in self.calls() if c[0]=='sudo' and any(a.endswith('install.sh') for a in c)]
        self.assertIn('--experimental-audio-volume', installers[-1])
        info = (self.repo/'stage/opt/mumble-funk/current/BUILD-INFO').read_text()
        self.assertIn('branch=feature/audio-volume', info)
        self.assertIn('experimental=true', info)
        config = self.repo/'stage/etc/mumble-funk/server.ini'
        self.assertIn('feedbackboostdb=8\n', config.read_text())
        config.write_text(config.read_text().replace('feedbackboostdb=8', 'feedbackboostdb=9'))
        original = config.read_bytes()
        result = self.execute(args=('--experimental-audio-volume',))
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertEqual(config.read_bytes(), original)

    def test_busy_asset_experimental_install_and_legacy_selection_preservation(self):
        self.env['MOCK_BRANCH']='feature/busy-tone-asset'
        (self.repo/'test-os-release').write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
        for args in ((), ('--experimental-squelch',), ('--experimental-audio-volume',)):
            result=self.execute(args=args)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('--experimental-busy-tone',result.stderr)
            self.assertFalse((self.repo/'stage/etc/mumble-funk/server.ini').exists())
        result=self.execute(args=('--experimental-busy-tone',))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        config=self.repo/'stage/etc/mumble-funk/server.ini'
        self.assertIn('busytonefile=/usr/local/share/mumble-funk/audio/busy-tone.wav\n',config.read_text())
        info=(self.repo/'stage/opt/mumble-funk/current/BUILD-INFO').read_text()
        self.assertIn('branch=feature/busy-tone-asset',info);self.assertIn('experimental=true',info)
        installers=[c for c in self.calls() if c[0]=='sudo' and any(a.endswith('install.sh') for a in c)]
        self.assertIn('--experimental-busy-tone',installers[-1])
        config.write_text(config.read_text().replace('busytonefile=/usr/local/share/mumble-funk/audio/busy-tone.wav','busytonefile='))
        original=config.read_bytes()
        result=self.execute(args=('--experimental-busy-tone',))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(config.read_bytes(),original)

    def test_audio_defaults_ubuntu24_requires_own_flag_and_preserves_disabled_update(self):
        self.env['MOCK_BRANCH']='feature/audio-defaults'
        (self.repo/'test-os-release').write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
        for args in ((), ('--experimental-busy-tone',)):
            result=self.execute(args=args)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('--experimental-audio-defaults',result.stderr)
        result=self.execute(args=('--experimental-audio-defaults',))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        config=self.repo/'stage/etc/mumble-funk/server.ini'
        for line in ('simplex=true', 'audiofeedback=true', 'squelch=true', 'feedbackboostdb=8'):
            self.assertIn(line+'\n',config.read_text())
        info=(self.repo/'stage/opt/mumble-funk/current/BUILD-INFO').read_text()
        self.assertIn('branch=feature/audio-defaults',info);self.assertIn('experimental=true',info)
        config.write_text(config.read_text().replace('audiofeedback=true','audiofeedback=false')
                          .replace('squelch=true','squelch=false').replace('feedbackboostdb=8\n',''))
        before=config.read_bytes()
        result=self.execute(args=('--experimental-audio-defaults',))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(config.read_bytes(),before)

    def test_failure_before_deployment(self):
        for failure in ("apt-update", "apt-install", "build", "tests", "network"):
            with self.subTest(failure=failure):
                result = self.execute(failure)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("fehlgeschlagen", result.stderr)
                expected = 'git' if failure == 'network' else 'ctest' if failure == 'tests' else 'cmake' if failure == 'build' else 'apt-get'
                self.assertTrue(any(call[0] == expected for call in self.calls()))
                self.assertNotIn("INSTALLATION ERFOLGREICH", result.stdout)
                self.assertFalse((self.repo / "stage/etc/mumble-funk/server.ini").exists())
                self.assertFalse(any(call[0] == "sudo" and any(arg.endswith("install.sh") for arg in call)
                                     for call in self.calls()))

    def test_unsupported_os_and_low_resources_stop_early(self):
        (self.repo / "test-os-release").write_text('ID=debian\nVERSION_ID="13"\n')
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Ubuntu 24.04", result.stderr)
        self.assertEqual(self.calls(), [])
        (self.repo / "test-os-release").write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
        (self.repo / "test-meminfo").write_text("MemTotal: 1048576 kB\nSwapTotal: 0 kB\n")
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mindestens 2 GiB", result.stderr)
        self.assertFalse(any(call[0] == "sudo" for call in self.calls()))

    def test_invalid_or_existing_different_port_stops(self):
        for port in ("64738", "80", "bad"):
            result = self.execute(args=("--port", port))
            self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assertEqual(self.execute().returncode, 0)
        (self.repo / "calls.jsonl").unlink()
        result = self.execute(args=("--port", "65000"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("behält ihren Port", result.stderr)
        self.assertFalse(any(call[0] == "cmake" for call in self.calls()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
