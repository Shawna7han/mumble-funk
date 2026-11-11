"""Unit checks for Docker-only lifecycle logic, with temporary directories."""
import contextlib
import hashlib
import json
import importlib.util
import io
import os
from pathlib import Path
import sqlite3
import tempfile
import tarfile
import types
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / 'runtime.py'

class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name)
        with patch.dict(os.environ,{'FUNK_TEST_ROOT':str(self.root)}):
            spec=importlib.util.spec_from_file_location('container_runtime',SOURCE)
            self.runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.runtime)
        for directory in self.runtime.DIRECTORIES.values(): directory.mkdir(parents=True)
        (self.runtime.DIRECTORIES['state']/'audio').mkdir()
        self.calls=[]
        def initialize_database(args,**kwargs):
            self.calls.append(args)
            with contextlib.closing(sqlite3.connect(self.runtime.DATABASE)) as db:
                db.execute('CREATE TABLE config(server_id INTEGER,key TEXT,value TEXT)');db.commit()
            return type('Result',(),{'returncode':0})()
        self.database=patch.object(self.runtime.subprocess,'run',side_effect=initialize_database)
        self.database.start();self.addCleanup(self.database.stop)

    def test_first_configuration_secure_and_repeated_start_preserves(self):
        self.runtime.initialize();original=self.runtime.CONFIG.read_bytes()
        values=self.runtime.settings()
        self.assertEqual(values['simplex'],'true');self.assertEqual(values['simplexretrytimeout'],'500')
        self.assertEqual(values['squelch'],'true');self.assertEqual(values['audiofeedback'],'true')
        self.assertEqual(values['squelchopen'],'true');self.assertEqual(values['squelchclose'],'true')
        self.assertEqual(values['squelchclosetimeoutfallback'],'true')
        self.assertEqual(values['feedbackboostdb'],'8')
        self.assertEqual((values['radionoise'],values['radionoisefile'],values['radionoisevolume']),('true','/usr/local/share/mumble-funk/audio/radio-noise.wav','20'))
        self.assertEqual(values['busytonefile'],'/usr/local/share/mumble-funk/audio/busy-tone.wav')
        self.assertEqual(values['squelchopenfile'],'/usr/local/share/mumble-funk/audio/squelch-open.wav')
        self.assertEqual(len(values['serverpassword']),48);self.assertNotIn('serverpassword=',str(self.calls))
        self.assertEqual(self.runtime.CONFIG.stat().st_mode & 0o777,0o600)
        self.assertIn('-disablesu',self.calls[0])
        self.runtime.initialize();self.assertEqual(self.runtime.CONFIG.read_bytes(),original);self.assertEqual(len(self.calls),1)

    def test_existing_synthetic_missing_or_custom_busy_selection_never_migrates(self):
        self.runtime.initialize()
        original=self.runtime.CONFIG.read_text()
        for selection in ('', '/var/lib/mumble-funk/audio/own.wav', None):
            text=original.replace('busytonefile=/usr/local/share/mumble-funk/audio/busy-tone.wav\n',
                                  '' if selection is None else 'busytonefile='+selection+'\n')
            self.runtime.CONFIG.write_text(text)
            before=self.runtime.CONFIG.read_bytes()
            self.runtime.initialize()
            self.assertEqual(self.runtime.CONFIG.read_bytes(),before)

    def test_legacy_missing_disabled_and_custom_audio_survive_initialize_and_restore(self):
        self.runtime.initialize()
        original=self.runtime.CONFIG.read_text()
        audio_keys=('audiofeedback','squelch','squelchopen','squelchclose','busytone','feedbackboostdb',
                    'busytonefile','busytonevolume','squelchopenvolume','squelchclosevolume','squelchclosetimeoutfallback')
        missing='\n'.join(line for line in original.splitlines() if not line.startswith(tuple(k+'=' for k in audio_keys)))+'\n'
        variants=[missing, original.replace('audiofeedback=true','audiofeedback=false')
                  .replace('squelch=true','squelch=false').replace('feedbackboostdb=8','feedbackboostdb=0')
                  .replace('squelchclosetimeoutfallback=true','squelchclosetimeoutfallback=false'),
                  original.replace('busytonevolume=50','busytonevolume=0').replace('feedbackboostdb=8','feedbackboostdb=12')
                  .replace('busytonefile=/usr/local/share/mumble-funk/audio/busy-tone.wav',
                           'busytonefile=/var/lib/mumble-funk/audio/own.wav')]
        own=self.runtime.DIRECTORIES['state']/'audio/own.wav'
        own.write_bytes((SOURCE.parents[2]/'assets/audio/busy-tone.wav').read_bytes())
        for text in variants:
            self.runtime.CONFIG.write_text(text)
            before=self.runtime.CONFIG.read_bytes();audio=own.read_bytes()
            self.runtime.initialize()
            self.assertEqual(self.runtime.CONFIG.read_bytes(),before)
            target=self.empty_target()
            with patch.object(target.sys,'stdin',types.SimpleNamespace(buffer=io.BytesIO(self.capture_backup()))):
                target.restore()
            self.assertEqual(target.CONFIG.read_bytes(),before)
            self.assertEqual((target.DIRECTORIES['state']/'audio/own.wav').read_bytes(),audio)
            import shutil
            shutil.rmtree(self.root/'restored')

    def test_audio_rollback_revision_guard_lock_and_exact_bytes(self):
        self.runtime.initialize()
        original=self.runtime.CONFIG.read_bytes()
        backup=self.runtime.CONFIG.with_name('server.ini.before-audio-change')
        backup.write_bytes(original)
        self.runtime.CONFIG.write_bytes(original.replace(b'radionoise=true',b'radionoise=false'))
        revision=hashlib.sha256(self.runtime.CONFIG.read_bytes()).hexdigest()
        with self.runtime.instance_lock():
            with self.assertRaises(RuntimeError):self.runtime.rollback_audio(revision)
        with self.assertRaises(RuntimeError):self.runtime.rollback_audio('f'*64)
        with self.assertRaises(RuntimeError):self.runtime.rollback_audio('../unsafe')
        self.runtime.rollback_audio(revision)
        self.assertEqual(self.runtime.CONFIG.read_bytes(),original)
        self.assertEqual(self.runtime.CONFIG.stat().st_mode & 0o777,0o600)
        backup.unlink();backup.symlink_to(self.runtime.CONFIG)
        with self.assertRaises(RuntimeError):self.runtime.rollback_audio(hashlib.sha256(original).hexdigest())

    def test_noise_backup_restore_and_existing_missing_settings_preserved(self):
        import wave
        self.runtime.initialize()
        path=self.runtime.DIRECTORIES['state']/'audio/radio-noise.wav'
        with wave.open(str(path),'wb') as wav:
            wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(48000)
            wav.writeframes(b'\x00\x01'*96000)
        for selection in (b'radionoise=true\nradionoisefile=/var/lib/mumble-funk/audio/radio-noise.wav\nradionoisevolume=37\n',
                          b'radionoise=false\nradionoisefile=/var/lib/mumble-funk/audio/radio-noise.wav\nradionoisevolume=0\n', b''):
            lines=self.runtime.CONFIG.read_bytes().splitlines(keepends=True)
            text=b''.join(line for line in lines if not line.startswith((b'radionoise=',b'radionoisefile=',b'radionoisevolume=')))+selection
            self.runtime.CONFIG.write_bytes(text);audio=path.read_bytes()
            self.runtime.initialize();self.assertEqual(self.runtime.CONFIG.read_bytes(),text)
            target=self.empty_target()
            with patch.object(target.sys,'stdin',types.SimpleNamespace(buffer=io.BytesIO(self.capture_backup()))):target.restore()
            self.assertEqual(target.CONFIG.read_bytes(),text)
            self.assertEqual((target.DIRECTORIES['state']/'audio/radio-noise.wav').read_bytes(),audio)
            import shutil
            shutil.rmtree(self.root/'restored')


    def test_missing_config_never_resets_existing_database(self):
        self.runtime.DATABASE.write_bytes(b'protected')
        with self.assertRaises(RuntimeError):self.runtime.initialize()
        self.assertEqual(self.runtime.DATABASE.read_bytes(),b'protected')

    def test_missing_database_without_bootstrap_is_refused(self):
        self.runtime.initialize();self.runtime.DATABASE.unlink()
        with self.assertRaises(RuntimeError):self.runtime.initialize()
        self.assertEqual(len(self.calls),1)

    def test_lock_prevents_two_instances_and_offline_edits(self):
        self.runtime.initialize()
        with self.runtime.instance_lock():
            with self.assertRaises(RuntimeError):
                self.runtime.configure('simplex','false')

    def test_configuration_changes_preserved(self):
        self.runtime.initialize();initial=self.runtime.settings();password=initial['serverpassword']
        self.runtime.CONFIG.write_text(self.runtime.CONFIG.read_text().replace('feedbackboostdb=8', 'feedbackboostdb=9'))
        self.runtime.configure('simplexdiagnostics','false');self.runtime.configure('simplextimeout','700')
        self.runtime.initialize();values=self.runtime.settings()
        self.assertEqual(values['feedbackboostdb'],'9')
        output=io.StringIO()
        with contextlib.redirect_stdout(output): self.runtime.info(as_json=True)
        self.assertEqual(json.loads(output.getvalue()), dict(initial, feedbackboostdb='9',
                         simplexdiagnostics='false', simplextimeout='700'))
        self.assertEqual(values['simplexdiagnostics'],'false');self.assertEqual(values['simplextimeout'],'700')
        self.assertEqual(values['serverpassword'],password)
        with self.assertRaises(RuntimeError):self.runtime.configure('database','/etc/foreign.sqlite')

    def test_reserved_paths_and_duplicates_fail_closed(self):
        self.runtime.initialize()
        original=self.runtime.CONFIG.read_text()
        for text in (original+'port=60000\n',original.replace('port=64739','port=64738'),original.replace('/var/lib/mumble-funk/server.sqlite','/etc/other.sqlite')):
            self.runtime.CONFIG.write_text(text)
            with self.assertRaises(RuntimeError):self.runtime.settings()

    def capture_backup(self):
        buffer=io.BytesIO()
        with patch.object(self.runtime.sys,'stdout',types.SimpleNamespace(buffer=buffer)):
            self.runtime.backup()
        return buffer.getvalue()

    def empty_target(self):
        root=self.root/'restored'
        with patch.dict(os.environ,{'FUNK_TEST_ROOT':str(root)}):
            spec=importlib.util.spec_from_file_location('restore_runtime',SOURCE)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        for directory in module.DIRECTORIES.values():directory.mkdir(parents=True)
        (module.DIRECTORIES['state']/'audio').mkdir()
        return module

    def test_offline_backup_restore_includes_database_config_and_future_audio(self):
        self.runtime.initialize()
        (self.runtime.DIRECTORIES['state']/'audio'/'example.txt').write_text('future audio storage')
        original=self.runtime.DATABASE.read_bytes();config=self.runtime.CONFIG.read_bytes()
        archive=self.capture_backup();target=self.empty_target()
        with patch.object(target.sys,'stdin',types.SimpleNamespace(buffer=io.BytesIO(archive))):target.restore()
        self.assertEqual(target.DATABASE.read_bytes(),original);self.assertEqual(target.CONFIG.read_bytes(),config)
        self.assertEqual((target.DIRECTORIES['state']/'audio'/'example.txt').read_text(),'future audio storage')
        self.assertEqual(target.CONFIG.stat().st_mode & 0o777,0o600)

    def test_restore_never_overwrites_existing_configuration(self):
        self.runtime.initialize();archive=self.capture_backup();original=self.runtime.CONFIG.read_bytes()
        with patch.object(self.runtime.sys,'stdin',types.SimpleNamespace(buffer=io.BytesIO(archive))):
            with self.assertRaises(RuntimeError):self.runtime.restore()
        self.assertEqual(self.runtime.CONFIG.read_bytes(),original)

    def test_unsafe_or_incomplete_archives_never_publish_target_data(self):
        for name,kind in [('config/../../escape',tarfile.REGTYPE),('/etc/escape',tarfile.REGTYPE),
                          ('state/link',tarfile.SYMTYPE),('state/.instance.lock',tarfile.REGTYPE),('config',tarfile.DIRTYPE)]:
            with self.subTest(name=name):
                target=self.empty_target() if not (self.root/'restored').exists() else None
                if target is None:
                    import shutil
                    shutil.rmtree(self.root/'restored');target=self.empty_target()
                buffer=io.BytesIO()
                with tarfile.open(fileobj=buffer,mode='w:gz') as archive:
                    member=tarfile.TarInfo(name);member.type=kind
                    if kind==tarfile.SYMTYPE:member.linkname='/etc/passwd'
                    archive.addfile(member)
                with patch.object(target.sys,'stdin',types.SimpleNamespace(buffer=io.BytesIO(buffer.getvalue()))):
                    with self.assertRaises((RuntimeError,OSError)):target.restore()
                self.assertFalse(target.CONFIG.exists());self.assertFalse(target.DATABASE.exists())
                self.assertFalse((self.root/'escape').exists())

    def test_edit_does_not_change_other_ini_groups(self):
        self.runtime.initialize()
        self.runtime.CONFIG.write_text(self.runtime.CONFIG.read_text()+'\n[Other]\nsimplex=true\n')
        self.runtime.configure('simplex','false')
        self.assertEqual(self.runtime.settings()['simplex'],'false')
        self.assertIn('[Other]\nsimplex=true',self.runtime.CONFIG.read_text())

    def test_admin_password_uses_stdin_and_never_process_arguments(self):
        self.runtime.initialize()
        password='test-only-'+('x'*32)
        with patch.object(self.runtime.getpass,'getpass',side_effect=[password,password]), patch.object(self.runtime.subprocess,'run') as process:
            process.return_value.returncode=0
            self.runtime.admin_password()
            self.assertIn('-readsupw',process.call_args.args[0])
            self.assertNotIn(password,str(process.call_args.args))
            self.assertEqual(process.call_args.kwargs['input'],(password+'\n').encode())
        self.assertNotIn(password,self.runtime.CONFIG.read_text())

if __name__=='__main__':unittest.main()
