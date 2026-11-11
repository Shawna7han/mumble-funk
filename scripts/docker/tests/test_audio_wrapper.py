"""Check audio management orchestration without touching a Docker installation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[3]
STUB = '''#!/usr/bin/env python3
import json, os, sys
args=sys.argv[1:]
if os.environ.get('EXPECTED_COMPOSE'):
    assert os.environ.get('COMPOSE_FILE')==os.environ['EXPECTED_COMPOSE']
    assert os.environ.get('FUNK_IMAGE')==os.environ['EXPECTED_IMAGE']
with open(os.environ['CALLS'], 'a') as log: log.write(json.dumps(args)+'\\n')
if 'ps' in args: print('test-container' if os.environ.get('RUNNING')=='yes' else '')
if 'exec' in args and os.environ.get('UNHEALTHY')=='yes': sys.exit(1)
if 'run' in args:
    if '-v' in args:
        from pathlib import Path
        source=args[args.index('-v')+1].split(':',1)[0]
        assert Path(source).read_bytes()==b'private-local-fixture'
    if os.environ.get('FAIL')=='yes': sys.exit(7)
    print('AUDIO-REVISION='+'a'*64 if 'audio' in args and args[-1]!='status' else '{}')
'''


class AudioWrapperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'scripts/docker').mkdir(parents=True)
        shutil.copy2(REPO/'scripts/docker/audio.sh', self.root/'scripts/docker/audio.sh')
        self.script = self.root/'scripts/docker/audio.sh'
        self.env_file = self.root/'.env'
        self.env_file.write_text('FUNK_HOST_PORT=65123\nCOMPOSE_PROJECT_NAME=operator-name\n')
        self.original = self.env_file.read_bytes()
        bindir = self.root/'bin'; bindir.mkdir()
        docker = bindir/'docker'; docker.write_text(STUB); docker.chmod(0o755)
        self.calls_file = self.root/'calls'
        self.env = dict(os.environ, PATH=str(bindir)+':'+os.environ['PATH'], CALLS=str(self.calls_file), RUNNING='yes')

    def run_command(self, *args, ok=True):
        result = subprocess.run(['bash', str(self.script), *args], env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, ok, result.stderr)
        self.assertEqual(self.env_file.read_bytes(), self.original)
        return [json.loads(line) for line in self.calls_file.read_text().splitlines()] if self.calls_file.exists() else []

    def test_status_never_stops_or_starts_service(self):
        calls = self.run_command('status')
        self.assertEqual(calls, [['compose', 'run', '--rm', '--no-deps', '-T', 'mumble-funk', 'audio', 'status']])

    def test_mutations_resume_only_previously_running_service(self):
        calls = self.run_command('disable-squelch')
        self.assertIn(['compose', 'stop', '--timeout', '30', 'mumble-funk'], calls)
        self.assertIn(['compose', 'start', 'mumble-funk'], calls)
        self.assertEqual(calls[-1], ['compose', 'exec', '-T', 'mumble-funk', 'funk-container', 'health'])
        for command in ('enable-timeout-close', 'disable-timeout-close', 'use-standard-busy-tone', 'reset-busy-tone', 'enable-radio-noise', 'disable-radio-noise', 'use-standard-radio-noise'):
            self.calls_file.unlink()
            calls = self.run_command(command)
            self.assertIn(['compose', 'run', '--rm', '--no-deps', '-T', 'mumble-funk', 'audio', command], calls)
            self.assertIn(['compose', 'start', 'mumble-funk'], calls)
            self.assertEqual(calls[-1], ['compose', 'exec', '-T', 'mumble-funk', 'funk-container', 'health'])
        self.calls_file.unlink(); self.env['RUNNING'] = 'no'
        calls = self.run_command('open-volume', '80')
        self.assertFalse(any('start' in call for call in calls))

    def test_boost_validates_before_stopping_and_preserves_env(self):
        for value in ('0','3','6','9','12'):
            if self.calls_file.exists(): self.calls_file.unlink()
            calls = self.run_command('boost', value)
            self.assertIn(['compose','run','--rm','--no-deps','-T','mumble-funk','audio','boost',value], calls)
            self.assertIn(['compose','start','mumble-funk'], calls)
        self.calls_file.unlink()
        for value in ('13','100','-1','1.5','NaN','inf','01','+6'):
            self.assertEqual(self.run_command('boost',value,ok=False), [])
        self.env['RUNNING']='no'
        calls=self.run_command('boost','6')
        self.assertFalse(any('start' in call for call in calls))

    def test_failed_import_resumes_and_removes_temporary_copy(self):
        asset = self.root/'my tone.wav'; asset.write_bytes(b'private-local-fixture')
        self.env['FAIL'] = 'yes'
        calls = self.run_command('set-open', str(asset), ok=False)
        imported = next(call for call in calls if '-v' in call)
        mount = imported[imported.index('-v')+1]
        self.assertTrue(mount.endswith(':/tmp/funk-import.wav:ro'))
        self.assertFalse(Path(mount.split(':', 1)[0]).exists())
        self.assertIn(['compose', 'start', 'mumble-funk'], calls)
        self.assertEqual(calls[-1], ['compose', 'exec', '-T', 'mumble-funk', 'funk-container', 'health'])
        self.assertEqual(asset.read_bytes(), b'private-local-fixture')

    def test_radio_noise_import_volume_and_bounds_preserve_env(self):
        asset=self.root/'noise.wav';asset.write_bytes(b'private-local-fixture')
        calls=self.run_command('set-radio-noise',str(asset))
        self.assertTrue(any('set-radio-noise' in call and '-v' in call for call in calls))
        for value in ('0','20','100'):
            self.calls_file.unlink()
            calls=self.run_command('radio-noise-volume',value)
            self.assertIn(['compose','run','--rm','--no-deps','-T','mumble-funk','audio','radio-noise-volume',value],calls)
        self.calls_file.unlink()
        for value in ('101','-1','NaN','1.5'):
            self.assertEqual(self.run_command('radio-noise-volume',value,ok=False),[])

    def test_failed_restart_attempts_guarded_config_rollback(self):
        # No real waits: replace sleep only in this temporary fixture.
        sleep=self.root/'bin/sleep';sleep.write_text('#!/bin/sh\nexit 0\n');sleep.chmod(0o755)
        self.env['UNHEALTHY']='yes'
        calls=self.run_command('enable-radio-noise',ok=False)
        self.assertIn(['compose','run','--rm','--no-deps','-T','mumble-funk','audio-rollback','a'*64],calls)
        self.assertEqual(sum(call==['compose','start','mumble-funk'] for call in calls),2)


    def test_prebuilt_compose_selection_preserved_by_existing_commands(self):
        selected=str(self.root/'compose.ghcr.yaml')
        image='ghcr.io/shawna7han/mumble-funk@sha256:'+'a'*64
        self.env.update(COMPOSE_FILE=selected,EXPECTED_COMPOSE=selected,FUNK_IMAGE=image,EXPECTED_IMAGE=image)
        for command in ('status','disable-radio-noise','use-standard-radio-noise'):
            self.run_command(command)

    def test_invalid_commands_do_not_stop_anything(self):
        for args in [('open-volume', '101'), ('disable-squelch', 'extra'), ('set-close', '/missing'), ('unknown',)]:
            self.assertEqual(self.run_command(*args, ok=False), [])


if __name__ == '__main__':
    unittest.main()
