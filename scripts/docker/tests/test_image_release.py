"""Publication guards and provenance; no actual registry login, tag push or publication."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError

SOURCE=Path(__file__).resolve().parents[1]/'image_release.py'
SPEC=importlib.util.spec_from_file_location('image_release',SOURCE)
RELEASE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(RELEASE)


class SourceGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.git('init','--quiet')
        self.git('config','user.name','Fixture')
        self.git('config','user.email','fixture@example.invalid')
        self.file=self.root/'source';self.file.write_text('tracked source')
        self.git('add','source');self.git('commit','--quiet','-m','Initial fixture')
        self.sha=self.git('rev-parse','HEAD')
        self.git('update-ref','refs/remotes/origin/dev',self.sha)
        self.tag='ghcr-dev-'+self.sha
        self.git('tag','-a',self.tag,'-m','Explicit fixture approval')
        self.ref='refs/tags/'+self.tag

    def git(self,*args):
        return subprocess.check_output(['git','-C',str(self.root),*args],text=True,stderr=subprocess.DEVNULL).strip()

    def verify(self,**kwargs):
        args=dict(root=self.root,repository=RELEASE.REPOSITORY,event='push',ref=self.ref,event_sha=self.sha)
        args.update(kwargs)
        return RELEASE.verify_source(**args)

    def test_exact_commit_and_annotated_tag_object_payload(self):
        self.assertEqual(self.verify(),self.sha)
        self.assertEqual(self.verify(event_sha=self.git('rev-parse',self.ref)),self.sha)

    def test_wrong_repository_event_ref_name_and_payload_refused(self):
        cases=[{'repository':'someone/fork'},{'event':'pull_request'},{'event':'pull_request_target'},
               {'event':'workflow_dispatch'},{'ref':'refs/heads/dev'},{'ref':'refs/tags/latest'},
               {'ref':'refs/tags/stable'},{'ref':'refs/tags/funk-v0.3.0'},
               {'ref':'refs/tags/ghcr-dev-'+self.sha[:12]},{'ref':'refs/tags/ghcr-dev-'+'a'*40},
               {'event_sha':'b'*40}]
        for value in cases:
            with self.subTest(value=value),self.assertRaises(RuntimeError):self.verify(**value)

    def test_lightweight_tag_refused(self):
        self.git('tag','-d',self.tag);self.git('tag',self.tag)
        with self.assertRaises(RuntimeError):self.verify()

    def test_descendant_feature_is_not_membership_in_dev(self):
        self.file.write_text('unreviewed feature')
        self.git('commit','--quiet','-am','Feature only')
        commit=self.git('rev-parse','HEAD');tag='ghcr-dev-'+commit
        self.git('tag','-a',tag,'-m','Not merged')
        with self.assertRaises(RuntimeError):self.verify(ref='refs/tags/'+tag,event_sha=commit)

    def test_tag_target_mismatch_refused(self):
        self.file.write_text('new revision');self.git('commit','--quiet','-am','New')
        with self.assertRaises(RuntimeError):self.verify()

    def test_missing_dev_and_dirty_tracked_source_refused(self):
        self.file.write_text('modified')
        with self.assertRaises(RuntimeError):self.verify()
        self.file.write_text('tracked source')
        self.git('update-ref','-d','refs/remotes/origin/dev')
        with self.assertRaises(subprocess.CalledProcessError):self.verify()


class ImageTests(unittest.TestCase):
    def setUp(self):
        self.value=RELEASE.metadata('a'*40,'2026-10-26T00:00:00Z')
        self.ident='sha256:'+'b'*64
        self.digest='sha256:'+'c'*64
        self.info={'Id':self.ident,'Architecture':'amd64','Os':'linux',
                   'Config':{'User':'10001:10001','Healthcheck':{'Test':['CMD','health']},
                             'Labels':self.value['labels']},
                   'RepoDigests':[RELEASE.IMAGE+'@'+self.digest]}

    def test_metadata_and_unchanged_native_dockerfile_build(self):
        self.assertEqual(self.value['version'],'dev-'+'a'*40)
        command=RELEASE.build_command(self.value)
        self.assertEqual(command[:5],['docker','build','--platform','linux/amd64','--tag'])
        self.assertNotIn('--push',command)
        self.assertEqual(command[-1],'.')
        for key,value in self.value['labels'].items():self.assertIn(key+'='+value,command)
        self.assertIn('id=proxy_ca,src=/test/ca',RELEASE.build_command(self.value,'/test/ca'))
        for commit in ('a'*12,'A'*40,'latest','a'*39+';'):
            with self.assertRaises(ValueError):RELEASE.metadata(commit)

    def test_malformed_or_mismatched_metadata_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'metadata.json'
            path.write_text(json.dumps(self.value))
            self.assertEqual(RELEASE.read_metadata(path),self.value)
            for key,value in [('image','ghcr.io/other/image:latest'),('version','stable'),('commit','b'*40)]:
                wrong=dict(self.value,**{key:value});path.write_text(json.dumps(wrong))
                with self.assertRaises(RuntimeError):RELEASE.read_metadata(path)
        with self.assertRaises(ValueError):RELEASE.metadata('a'*40,'not a time')

    def test_artifact_id_architecture_user_healthcheck_and_labels(self):
        with patch.object(RELEASE.subprocess,'check_output',return_value=json.dumps([self.info])):
            self.assertEqual(RELEASE.inspect_image(self.value,self.ident),self.info)
            with self.assertRaises(RuntimeError):RELEASE.inspect_image(self.value,'sha256:'+'d'*64)
        for field,value in [('Architecture','arm64'),('Os','windows'),('Id','bad')]:
            wrong=copy.deepcopy(self.info);wrong[field]=value
            with patch.object(RELEASE.subprocess,'check_output',return_value=json.dumps([wrong])),self.assertRaises(RuntimeError):
                RELEASE.inspect_image(self.value)
        for field,value in [('User','0'),('Healthcheck',{}),('Labels',{})]:
            wrong=copy.deepcopy(self.info);wrong['Config'][field]=value
            with patch.object(RELEASE.subprocess,'check_output',return_value=json.dumps([wrong])),self.assertRaises(RuntimeError):
                RELEASE.inspect_image(self.value)

    def response(self,digest):
        response=unittest.mock.MagicMock()
        response.__enter__.return_value.headers={'Docker-Content-Digest':digest}
        return response

    def test_registry_authentication_stays_in_headers_and_memory(self):
        response=unittest.mock.MagicMock()
        response.__enter__.return_value.read.return_value=b'{"token":"fixture-bearer"}'
        with patch.object(RELEASE,'urlopen',return_value=response) as request:
            self.assertEqual(RELEASE.registry_token('fixture','not-a-credential'),'fixture-bearer')
            actual=request.call_args.args[0]
            self.assertTrue(actual.get_header('Authorization').startswith('Basic '))
            self.assertNotIn('not-a-credential',actual.full_url)
            response.__enter__.return_value.read.assert_called_once_with(65536)
        with patch.object(RELEASE,'urlopen') as request,self.assertRaises(RuntimeError):
            RELEASE.registry_token('fixture','')
        request.assert_not_called()

    def test_failure_output_cannot_expose_registry_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'metadata.json';path.write_text(json.dumps(self.value))
            output=io.StringIO()
            with patch.object(RELEASE,'verify_source',return_value=self.value['commit']), \
                 patch.object(RELEASE,'publish',side_effect=URLError('private-failure-sentinel')), \
                 contextlib.redirect_stderr(output):
                self.assertEqual(RELEASE.main(['publish','--file',str(path),'--expected-id',self.ident]),1)
            self.assertNotIn('private-failure-sentinel',output.getvalue())

    def test_manifest_not_found_only_and_valid_digest(self):
        with patch.object(RELEASE,'urlopen',return_value=self.response(self.digest)):
            self.assertEqual(RELEASE.manifest_digest(self.value['version'],'in-memory-sentinel'),self.digest)
        for code in (401,403,404,429,500):
            error=HTTPError('https://ghcr.io',code,'fixture',None,None)
            with self.subTest(code=code),patch.object(RELEASE,'urlopen',side_effect=error):
                if code==404:self.assertIsNone(RELEASE.manifest_digest(self.value['version'],'in-memory-sentinel'))
                else:
                    with self.assertRaises(RuntimeError):RELEASE.manifest_digest(self.value['version'],'in-memory-sentinel')
        with patch.object(RELEASE,'urlopen',return_value=self.response('invalid')),self.assertRaises(RuntimeError):
            RELEASE.manifest_digest(self.value['version'],'in-memory-sentinel')
        with patch.object(RELEASE,'urlopen',side_effect=URLError('offline')),self.assertRaises(URLError):
            RELEASE.manifest_digest(self.value['version'],'in-memory-sentinel')
        for version in ('latest','stable','dev-a','funk-v0.2.1'):
            with self.assertRaises(ValueError):RELEASE.manifest_digest(version,'in-memory-sentinel')

    def test_existing_tag_and_network_errors_never_log_in_or_push(self):
        for result in (self.digest,RuntimeError('denied'),URLError('offline')):
            with patch.object(RELEASE,'inspect_image',return_value=self.info), \
                 patch.object(RELEASE,'registry_token',return_value='in-memory-sentinel'), \
                 patch.object(RELEASE,'manifest_digest',side_effect=result if isinstance(result,Exception) else None,return_value=result), \
                 patch.object(RELEASE.subprocess,'run') as run:
                with self.assertRaises((RuntimeError,URLError)):
                    RELEASE.publish(self.value,self.ident,'fixture','not-a-credential')
                run.assert_not_called()

    def test_publish_uses_tested_image_stdin_auth_and_verifies_digest(self):
        with patch.object(RELEASE,'inspect_image',return_value=self.info), \
             patch.object(RELEASE,'registry_token',return_value='in-memory-sentinel'), \
             patch.object(RELEASE,'manifest_digest',side_effect=[None,self.digest]), \
             patch.object(RELEASE.subprocess,'run') as run:
            result=RELEASE.publish(self.value,self.ident,'fixture','not-a-credential')
            self.assertEqual(result['pinned_image'],RELEASE.IMAGE+'@'+self.digest)
            self.assertEqual([c.args[0][1] for c in run.call_args_list],['login','push'])
            self.assertNotIn('not-a-credential',' '.join(run.call_args_list[0].args[0]))
            self.assertEqual(run.call_args_list[0].kwargs['input'],'not-a-credential\n')

    def test_digest_mismatch_and_missing_pushed_manifest_fail(self):
        info=dict(self.info,RepoDigests=[])
        for digest in (None,self.digest):
            with patch.object(RELEASE,'inspect_image',return_value=info), \
                 patch.object(RELEASE,'registry_token',return_value='in-memory-sentinel'), \
                 patch.object(RELEASE,'manifest_digest',side_effect=[None,digest]), \
                 patch.object(RELEASE.subprocess,'run'),self.assertRaises(RuntimeError):
                RELEASE.publish(self.value,self.ident,'fixture','not-a-credential')


if __name__=='__main__':unittest.main()
