"""Native staged installation and shared native/container audio administration."""
import hashlib
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

BINARY = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('native_audio', REPO/'scripts/funk/audio.py')
AUDIO = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIO)


class SquelchAdminTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.install = ['bash', str(REPO/'scripts/funk/install.sh'), '--setup', '--stage', str(self.root), str(BINARY)]
        subprocess.run(self.install, check=True, capture_output=True)
        self.config = self.root/'etc/mumble-funk/server.ini'
        self.control = self.root/'usr/local/sbin/mumble-funk-audio'
        self.sound = self.root/'usr/local/share/mumble-funk/audio'
        self.sounds = self.root/'etc/mumble-funk/sounds'
        self.binary_hash = hashlib.sha256(BINARY.read_bytes()).digest()

    def command(self, *args, ok=True):
        result = subprocess.run([sys.executable, str(self.control), *args, '--root', str(self.root), '--binary', str(BINARY)], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, ok, result.stdout+result.stderr)
        return result

    def status(self):
        return json.loads(self.command('status').stdout)

    def test_defaults_assets_permissions_and_safe_status(self):
        state = self.status()
        self.assertEqual(state['busytonefile'], AUDIO.BUSY_STANDARD)
        self.assertEqual(state['squelch'], 'true')
        self.assertEqual(state['audiofeedback'], 'true')
        self.assertEqual(state['squelchopen'], 'true')
        self.assertEqual(state['squelchclose'], 'true')
        self.assertEqual(state['squelchclosetimeoutfallback'], 'true')
        for name in ('busy-tone.wav', 'squelch-open.wav', 'squelch-close.wav', 'radio-noise.wav', 'LICENSE.md'):
            path = self.sound/name
            self.assertEqual(path.read_bytes(), (REPO/'assets/audio'/name).read_bytes())
            self.assertEqual(path.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.sounds.stat().st_mode & 0o777, 0o750)
        self.assertEqual(self.config.stat().st_mode & 0o777, 0o640)
        secret = [line for line in self.config.read_text().splitlines() if line.startswith('serverpassword=')][0].split('=', 1)[1]
        self.assertNotIn(secret, self.command('status').stdout)
        info = (self.root/'opt/mumble-funk/current/BUILD-INFO').read_text()
        # PR CI stages an isolated install from a detached synthetic merge commit.
        # Staging must report its actual source, not pretend to be the feature head.
        branch = subprocess.check_output(['git', '-C', str(REPO), 'branch', '--show-current'], text=True).strip()
        self.assertIn('branch='+branch+'\n', info)
        self.assertIn('experimental='+str(branch in ('dev', 'feature/squelch-audio', 'feature/audio-volume', 'feature/busy-tone-asset', 'feature/audio-defaults', 'feature/radio-noise')).lower()+'\n', info)
        revision = subprocess.check_output(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], text=True).strip()
        self.assertIn('revision='+revision+'\n', info)
        self.assertFalse((self.root/'etc/systemd/system/mumble-funk-audio-engine.service').exists())

    def test_noise_status_missing_values_preserves_all_legacy_fields(self):
        # Explicit pre-existing schema: new fields must only extend this object.
        legacy = {
            'feedbackboostdb': '8', 'audiofeedback': 'false', 'busytone': 'false',
            'busytonefile': '/etc/mumble-funk/sounds/custom-busy.wav', 'busytonevolume': '31',
            'squelch': 'false', 'squelchopen': 'false', 'squelchclose': 'true',
            'squelchclosetimeoutfallback': 'false',
            'squelchopenfile': '/etc/mumble-funk/sounds/custom-open.wav',
            'squelchclosefile': '/etc/mumble-funk/sounds/custom-close.wav',
            'squelchopenvolume': '17', 'squelchclosevolume': '83',
        }
        self.config.write_text('[General]\n'+''.join(f'{key}={value}\n' for key,value in legacy.items()))
        before = self.config.read_bytes()
        self.assertEqual(self.status(), dict(legacy, radionoise='false', radionoisefile='', radionoisevolume='20'))
        self.assertEqual(self.config.read_bytes(), before)

    def test_noise_status_enabled_disabled_and_full_custom_file(self):
        # Deliberately not a loadable WAV: status describes INI, not runtime success.
        path = '/etc/mumble-funk/sounds/Eigenes Rauschen ä=Test.wav'
        for enabled, volume in [('true','20'), ('false','77'), ('true','0')]:
            with self.subTest(enabled=enabled, volume=volume):
                self.config.write_text('[General]\nradionoise='+enabled+'\nradionoisefile='+json.dumps(path)+'\nradionoisevolume='+volume+'\n')
                state = self.status()
                self.assertEqual((state['radionoise'], state['radionoisefile'], state['radionoisevolume']),
                                 (enabled, path, volume))
                self.assertNotIn('loaded', state)
                self.assertTrue(all(isinstance(value, str) for value in state.values()))

    def test_noise_status_read_only_secret_whitelist_native_and_container(self):
        secrets = {'serverpassword': 'TEST_ONLY_password', 'icesecret': 'TEST_ONLY_ice',
                   'sqlpassword': 'TEST_ONLY_sql', 'sslkey': 'TEST_ONLY_key'}
        # These inert sentinels exercise secret filtering, not authentication.
        # Keep the INI in memory: no credential-shaped fixture reaches disk.
        fixture = ('radionoise=false\nradionoisefile=/missing/kept.wav\nradionoisevolume=29\n'
                   + ''.join(f'{key}={value}\n' for key,value in secrets.items())
                   + '[Other]\nradionoise=true\nradionoisefile=/wrong-section.wav\n')
        before, metadata = self.config.read_bytes(), self.config.stat()
        # Exercise the production branch with a temporary root, never the host's /etc.
        # A status request must bypass all service, lock, validation and write actions.
        for container in (False, True):
            output, errors = io.StringIO(), io.StringIO()
            with mock.patch.object(AUDIO.Path, 'resolve', return_value=Path('/')), \
                 mock.patch.object(AUDIO.os, 'geteuid', return_value=0), \
                 mock.patch.object(AUDIO.Path, 'read_bytes', autospec=True,
                                   return_value=fixture.encode('utf-8')) as config_read, \
                 mock.patch.object(AUDIO.COMMON, 'checked', side_effect=AssertionError('Unexpected external command')), \
                 mock.patch.object(AUDIO.COMMON, 'atomic_config', side_effect=AssertionError('Unexpected config write')), \
                 mock.patch.object(AUDIO, 'installation_lock', side_effect=AssertionError('Unexpected install lock')), \
                 contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                self.assertEqual(AUDIO.main(['status', '--root', str(self.root)], container=container), 0)
            config_read.assert_called_once_with(self.config)
            state = json.loads(output.getvalue())
            self.assertEqual(state, dict(AUDIO.DEFAULTS, radionoise='false',
                                        radionoisefile='/missing/kept.wav', radionoisevolume='29'))
            self.assertEqual(errors.getvalue(), '')
            for key, value in secrets.items():
                self.assertNotIn(key, state)
                self.assertNotIn(value, output.getvalue()+errors.getvalue())
        self.assertEqual(self.config.read_bytes(), before)
        self.assertEqual(self.config.stat().st_mtime_ns, metadata.st_mtime_ns)
        self.assertFalse((self.config.parent/'server.ini.before-audio-change').exists())
        self.assertFalse((self.root/'run/lock/mumble-funk-install.lock').exists())

    def container_command(self,*args,ok=True):
        out,err=io.StringIO(),io.StringIO()
        with contextlib.redirect_stdout(out),contextlib.redirect_stderr(err):
            code=AUDIO.main([*args,'--root',str(self.root),'--binary',str(BINARY)],container=True)
        self.assertEqual(code==0,ok,out.getvalue()+err.getvalue())
        return out.getvalue()

    def test_container_noise_import_larger_than_short_assets_and_management(self):
        import wave
        source=self.root/'own noise.wav'
        with wave.open(str(source),'wb') as wav:
            wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(48000)
            wav.writeframes(b'\x00\x01'*96000)  # 2s / 192044 bytes, above old 128 KiB limit.
        self.container_command('disable-radio-noise')
        before=self.status()
        self.container_command('set-radio-noise',str(source))
        state=self.status();selected=state['radionoisefile']
        self.assertTrue(selected.startswith('/var/lib/mumble-funk/audio/radio-noise-'))
        target=self.root/selected.lstrip('/')
        self.assertEqual(target.read_bytes(),source.read_bytes())
        self.assertEqual(target.stat().st_mode & 0o777,0o640)
        self.assertEqual(state['radionoise'],'false')  # Import never enables effects.
        self.container_command('set-radio-noise',str(source))  # idempotent immutable copy
        self.container_command('enable-radio-noise')
        self.assertEqual(self.status()['radionoise'],'true')
        for value in ('0','20','100'):
            self.container_command('radio-noise-volume',value)
            self.assertEqual(self.status()['radionoisevolume'],value)
        self.container_command('disable-radio-noise')
        after=self.status()
        for key in ('radionoise','radionoisefile','radionoisevolume'):after.pop(key);before.pop(key)
        self.assertEqual(after,before)  # Busy, Simplex and Squelch not modified.
        self.assertEqual(target.read_bytes(),source.read_bytes())
        target.write_bytes(b'operator modified existing file')
        original=self.config.read_bytes()
        self.container_command('set-radio-noise',str(source),ok=False)
        self.assertEqual(self.config.read_bytes(),original)
        self.assertEqual(target.read_bytes(),b'operator modified existing file')

    def test_container_noise_invalid_files_values_and_enable_do_not_change_settings(self):
        import wave
        # Legacy INI with no file must still refuse enable, despite new-install defaults.
        self.config.write_bytes(AUDIO.COMMON.update_ini(self.config.read_text(),
                               {'radionoise': 'false', 'radionoisefile': ''}).encode())
        original=self.config.read_bytes()
        self.container_command('enable-radio-noise',ok=False)
        for name,channels,rate,frames in [('stereo',2,48000,960),('rate',1,44100,960),('short',1,48000,959),('long',1,48000,2880001)]:
            source=self.root/(name+'.wav')
            with wave.open(str(source),'wb') as wav:
                wav.setnchannels(channels);wav.setsampwidth(2);wav.setframerate(rate);wav.writeframes(b'\x00\x01'*(frames*channels))
            self.container_command('set-radio-noise',str(source),ok=False)
            self.assertEqual(self.config.read_bytes(),original)
        source=self.root/'huge.wav'
        with source.open('wb') as wav:wav.truncate(8388609)
        self.container_command('set-radio-noise',str(source),ok=False)
        link=self.root/'link.wav';link.symlink_to(REPO/'assets/audio/squelch-close.wav')
        self.container_command('set-radio-noise',str(link),ok=False)
        for value in ('101','-1','NaN','1.5'):
            self.container_command('radio-noise-volume',value,ok=False)
        self.assertEqual(self.config.read_bytes(),original)
        self.container_command('set-radio-noise',str(REPO/'assets/audio/squelch-close.wav'))
        self.config.write_text(self.config.read_text().replace('audiofeedback=true','audiofeedback=false'))
        # Legacy INI with no file must still refuse enable, despite new-install defaults.
        self.config.write_bytes(AUDIO.COMMON.update_ini(self.config.read_text(),
                               {'radionoise': 'false', 'radionoisefile': ''}).encode())
        original=self.config.read_bytes()
        self.container_command('enable-radio-noise',ok=False)
        self.assertEqual(self.config.read_bytes(),original)


    def test_standard_noise_selection_native_container_and_preservation(self):
        for container in (False, True):
            with self.subTest(container=container):
                run = self.container_command if container else self.command
                run('disable-radio-noise')
                run('set-radio-noise', str(REPO/'assets/audio/radio-noise.wav'))
                selected = self.status()['radionoisefile']
                custom = self.root/selected.lstrip('/')
                saved = custom.read_bytes()
                run('radio-noise-volume', '0')
                before = self.status()
                run('use-standard-radio-noise')
                self.assertEqual(self.status(), dict(before, radionoisefile=AUDIO.NOISE_STANDARD))
                self.assertEqual(custom.read_bytes(), saved)
                run('enable-radio-noise')
                self.assertEqual(self.status()['radionoise'], 'true')
                self.assertEqual(self.status()['radionoisevolume'], '0')
                # A missing/damaged package must not replace a valid selection.
                asset = self.sound/'radio-noise.wav'
                original_asset = asset.read_bytes()
                asset.write_bytes(b'invalid')
                original = self.config.read_bytes()
                run('use-standard-radio-noise', ok=False)
                self.assertEqual(self.config.read_bytes(), original)
                asset.write_bytes(original_asset)

    def test_master_and_independent_switches_keep_busy_settings(self):
        self.command('enable-squelch')
        self.assertEqual(self.status()['squelch'], 'true')
        self.assertEqual(self.status()['audiofeedback'], 'true')
        self.command('disable-open'); self.command('enable-close')
        self.assertEqual(self.status()['squelchopen'], 'false')
        self.assertEqual(self.status()['squelchclose'], 'true')
        self.command('disable-close'); self.command('enable-open')
        self.assertEqual(self.status()['squelchclose'], 'false')
        self.assertEqual(self.status()['squelchopen'], 'true')
        self.command('disable-squelch')
        self.assertEqual(self.status()['squelch'], 'false')
        self.assertEqual(self.status()['audiofeedback'], 'true')
        self.assertEqual(self.status()['busytone'], 'true')

    def test_independent_volumes_and_invalid_values(self):
        for cmd, key, value in [('open-volume', 'squelchopenvolume', '17'), ('close-volume', 'squelchclosevolume', '83'), ('volume', 'busytonevolume', '31')]:
            self.command(cmd, value)
            self.assertEqual(self.status()[key], value)
        before = self.config.read_bytes()
        for cmd in ('open-volume', 'close-volume', 'volume'):
            for value in ('101', '-1', '1.5', 'loud', '1000'):
                self.command(cmd, value, ok=False)
                self.assertEqual(self.config.read_bytes(), before)

    def test_boost_all_values_preserves_settings_assets_and_updates(self):
        self.assertEqual(self.status()['feedbackboostdb'], '8')
        # Missing key is the legacy default; status must not migrate the INI.
        self.config.write_text(self.config.read_text().replace('feedbackboostdb=8\n', ''))
        original = self.config.read_bytes()
        self.assertEqual(self.status()['feedbackboostdb'], '0')
        self.assertEqual(self.config.read_bytes(), original)
        before = self.status(); before.pop('feedbackboostdb')
        for value in range(13):
            self.command('boost', str(value))
            state = self.status()
            self.assertEqual(state.pop('feedbackboostdb'), str(value))
            self.assertEqual(state, before)  # Does not enable disabled effects.
        for cmd, name in [('set-open','squelch-open.wav'),('set-close','squelch-close.wav'),('set-busy-tone','squelch-close.wav')]:
            self.command(cmd, str(REPO/'assets/audio'/name))
        self.command('enable-timeout-close')
        original = self.config.read_bytes()
        files = {p: p.read_bytes() for p in self.sounds.glob('*.wav')}
        subprocess.run(self.install, check=True, capture_output=True)
        self.assertEqual(self.config.read_bytes(), original)
        for p, content in files.items(): self.assertEqual(p.read_bytes(), content)
        for value in ('13','100','-1','1.5','NaN','inf','loud','01','+6','6e0'):
            self.command('boost', value, ok=False)
            self.assertEqual(self.config.read_bytes(), original)
        self.command('boost','0')
        self.assertEqual(self.status()['feedbackboostdb'],'0')

    def test_timeout_close_opt_in_preserves_other_settings_and_updates(self):
        before = self.status()
        self.command('enable-timeout-close')
        enabled = self.status()
        self.assertEqual(enabled.pop('squelchclosetimeoutfallback'), 'true')
        before.pop('squelchclosetimeoutfallback')
        self.assertEqual(enabled, before)  # Does not enable Squelch, master feedback or alter Busy.
        original = self.config.read_bytes()
        subprocess.run(self.install, check=True, capture_output=True)
        self.assertEqual(self.config.read_bytes(), original)
        self.command('disable-timeout-close')
        self.assertEqual(self.status()['squelchclosetimeoutfallback'], 'false')

    def test_custom_wavs_and_update_preservation_without_recompilation(self):
        for signal in ('open', 'close'):
            self.command('set-'+signal, str(REPO/'assets/audio'/('squelch-'+signal+'.wav')))
            value = self.status()['squelch'+signal+'file']
            target = self.root/value.lstrip('/')
            self.assertEqual(target.read_bytes(), (REPO/'assets/audio'/('squelch-'+signal+'.wav')).read_bytes())
            self.assertEqual(target.stat().st_mode & 0o777, 0o640)
        self.command('open-volume', '73'); self.command('enable-squelch')
        before = self.config.read_bytes()
        custom = {p: p.read_bytes() for p in self.sounds.glob('*.wav')}
        subprocess.run(self.install, check=True, capture_output=True)
        self.assertEqual(self.config.read_bytes(), before)
        for p, data in custom.items(): self.assertEqual(p.read_bytes(), data)
        self.assertEqual(hashlib.sha256(BINARY.read_bytes()).digest(), self.binary_hash)
        for signal in ('open', 'close'):
            self.command('reset-'+signal)
            self.assertEqual(self.status()['squelch'+signal+'file'], AUDIO.DEFAULTS['squelch'+signal+'file'])
        # Reset does not delete the imported files; a previous configuration can use them again.
        for p, data in custom.items(): self.assertEqual(p.read_bytes(), data)

    def test_busy_parser_and_fallback_unchanged(self):
        before = self.config.read_bytes()
        self.command('set-busy-tone', str(REPO/'assets/audio/squelch-open.wav'), ok=False)  # <20ms remains invalid for Busy.
        self.assertEqual(self.config.read_bytes(), before)
        self.command('set-busy-tone', str(REPO/'assets/audio/squelch-close.wav'))
        self.assertTrue(self.status()['busytonefile'].startswith('/etc/mumble-funk/sounds/busy-'))
        self.command('reset-busy-tone')
        self.assertEqual(self.status()['busytonefile'], '')
        self.command('enable'); self.command('disable')
        self.assertEqual(self.status()['audiofeedback'], 'false')

    def test_standard_legacy_custom_and_reset_preserve_all_other_settings(self):
        before = self.status(); before.pop('busytonefile')
        self.command('reset-busy-tone')
        self.assertEqual(self.status()['busytonefile'], '')  # Historical reset remains synthetic.
        original = self.config.read_bytes()
        subprocess.run(self.install, check=True, capture_output=True)
        self.assertEqual(self.config.read_bytes(), original)
        self.config.write_bytes(original.replace(b'busytonefile=\n', b''))
        original = self.config.read_bytes()
        self.assertEqual(self.status()['busytonefile'], '')  # Missing is synthetic too.
        subprocess.run(self.install, check=True, capture_output=True)
        self.assertEqual(self.config.read_bytes(), original)
        self.command('use-standard-busy-tone')
        self.assertEqual(self.status()['busytonefile'], AUDIO.BUSY_STANDARD)
        selected = self.status(); selected.pop('busytonefile')
        self.assertEqual(selected, before)  # Never auto-enables or changes volumes/boost/Squelch.
        self.command('set-busy-tone', str(REPO/'assets/audio/busy-tone.wav'))
        custom = self.status()['busytonefile']
        saved = self.root/custom.lstrip('/')
        self.assertEqual(saved.read_bytes(), (REPO/'assets/audio/busy-tone.wav').read_bytes())
        self.command('boost','12'); self.command('volume','100')
        original = self.config.read_bytes()
        subprocess.run(self.install, check=True, capture_output=True)
        self.assertEqual(self.config.read_bytes(), original)
        self.assertEqual(saved.read_bytes(), (REPO/'assets/audio/busy-tone.wav').read_bytes())
        self.command('reset-busy-tone'); self.assertEqual(self.status()['busytonefile'],'')
        self.command('use-standard-busy-tone'); self.assertEqual(self.status()['busytonefile'],AUDIO.BUSY_STANDARD)
        original = self.config.read_bytes()
        (self.sound/'busy-tone.wav').write_bytes(b'invalid')
        self.command('use-standard-busy-tone', ok=False)
        self.assertEqual(self.config.read_bytes(), original)
        self.assertEqual(saved.read_bytes(), (REPO/'assets/audio/busy-tone.wav').read_bytes())

    def test_existing_file_or_symlink_at_new_standard_path_is_not_overwritten(self):
        target=self.sound/'busy-tone.wav'
        target.write_bytes(b'operator-owned recording')
        before={p:p.read_bytes() for p in self.root.rglob('*') if p.is_file() and not p.is_symlink()}
        result=subprocess.run(self.install,capture_output=True)
        self.assertNotEqual(result.returncode,0)
        for p,content in before.items(): self.assertEqual(p.read_bytes(),content)
        own=self.root/'own.wav';own.write_bytes(b'own file')
        target.unlink();target.symlink_to(own)
        result=subprocess.run(self.install,capture_output=True)
        self.assertNotEqual(result.returncode,0)
        self.assertTrue(target.is_symlink());self.assertEqual(own.read_bytes(),b'own file')

    def test_bad_or_unsafe_files_do_not_change_configuration(self):
        original = self.config.read_bytes()
        bad = self.root/'bad.wav'; bad.write_bytes(b'corrupt')
        huge = self.root/'huge.wav'; huge.write_bytes(b'x'*131073)
        link = self.root/'link.wav'; link.symlink_to(REPO/'assets/audio/squelch-open.wav')
        fifo = self.root/'pipe.wav'; os.mkfifo(fifo)
        for path in (bad, huge, link, fifo, self.root/'missing.wav', self.root):
            for signal in ('open', 'close'):
                self.command('set-'+signal, str(path), ok=False)
                self.assertEqual(self.config.read_bytes(), original)

    def test_duplicates_and_other_sections_preserved(self):
        self.config.write_text(self.config.read_text()+'\n[Other]\nsquelch=true\nkeep=value\n')
        self.command('disable-squelch')
        self.assertIn('[Other]\nsquelch=true\nkeep=value\n', self.config.read_text())
        self.config.write_text('squelchopenfile=/duplicate\n'+self.config.read_text())
        before = self.config.read_bytes()
        self.command('set-open', str(REPO/'assets/audio/squelch-open.wav'), ok=False)
        self.assertEqual(self.config.read_bytes(), before)

    def test_enable_requires_simplex_and_valid_assets(self):
        self.config.write_text(self.config.read_text().replace('simplex=true', 'simplex=false'))
        original = self.config.read_bytes()
        self.command('enable-squelch', ok=False)
        self.assertEqual(self.config.read_bytes(), original)

        self.config.write_bytes(original.replace(b'simplex=false', b'simplex=true').replace(b'squelch-open.wav', b'missing.wav'))
        original = self.config.read_bytes()
        self.command('enable-squelch', ok=False)
        self.assertEqual(self.config.read_bytes(), original)
        self.command('enable-open', ok=False)
        self.assertEqual(self.config.read_bytes(), original)

    def test_container_storage_and_status_without_systemd(self):
        code = "import importlib.util,sys;s=importlib.util.spec_from_file_location('audio',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);sys.exit(m.main(sys.argv[2:],container=True))"
        command = [sys.executable, '-c', code, str(REPO/'scripts/funk/audio.py'), 'set-open', str(REPO/'assets/audio/squelch-open.wav'), '--root', str(self.root), '--binary', str(BINARY)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        value = self.status()['squelchopenfile']
        boost = [sys.executable, '-c', code, str(REPO/'scripts/funk/audio.py'), 'boost', '9', '--root', str(self.root), '--binary', str(BINARY)]
        result = subprocess.run(boost, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.status()['feedbackboostdb'], '9')
        self.assertTrue(value.startswith('/var/lib/mumble-funk/audio/open-'))
        self.assertEqual((self.root/value.lstrip('/')).stat().st_mode & 0o777, 0o640)


if __name__ == '__main__':
    unittest.main(verbosity=2)
