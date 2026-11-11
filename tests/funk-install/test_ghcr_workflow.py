"""Structural security checks for the actual GitHub publication workflow (no publication)."""
from pathlib import Path
import unittest
import yaml

ROOT=Path(__file__).resolve().parents[2]


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        # BaseLoader preserves GitHub's 'on' key instead of YAML 1.1 boolean conversion.
        self.workflow=yaml.load((ROOT/'.github/workflows/publish-dev-image.yml').read_text(),Loader=yaml.BaseLoader)

    def test_only_explicit_tag_push_no_pr_dispatch_or_branch_push(self):
        self.assertEqual(self.workflow['on'],{'push':{'tags':['ghcr-dev-*']}})
        self.assertEqual(self.workflow['permissions'],{'contents':'read'})
        self.assertEqual(self.workflow['concurrency']['cancel-in-progress'],'false')

    def test_write_token_only_in_dependent_publisher(self):
        jobs=self.workflow['jobs']
        self.assertNotIn('permissions',jobs['guard'])
        self.assertNotIn('permissions',jobs['build-and-test'])
        self.assertEqual(jobs['publish']['permissions'],{'contents':'read','packages':'write'})
        self.assertEqual(jobs['publish']['needs'],['guard','build-and-test'])
        for name in ('guard','build-and-test','publish'):
            steps=jobs[name]['steps']
            checkout=next(s for s in steps if s.get('uses','').startswith('actions/checkout@'))
            self.assertEqual(checkout['with']['persist-credentials'],'false')
            self.assertTrue(any('image_release.py verify' in s.get('run','') for s in steps))
            if name!='guard':self.assertEqual(checkout['with']['ref'],'${{ needs.guard.outputs.commit }}')

    def test_exact_artifact_transfer_no_privileged_rebuild(self):
        jobs=self.workflow['jobs']
        build='\n'.join(s.get('run','') for s in jobs['build-and-test']['steps'])
        publish='\n'.join(s.get('run','') for s in jobs['publish']['steps'])
        self.assertIn('image_release.py build',build)
        self.assertIn('integration.py',build)
        self.assertIn('compose.ghcr.yaml',build)
        self.assertIn('docker save',build)
        self.assertIn('docker load',publish)
        self.assertNotIn('docker build',publish)
        self.assertNotIn('image_release.py build',publish)
        self.assertIn('--expected-id "$EXPECTED_IMAGE_ID"',publish)
        self.assertNotIn('GITHUB_TOKEN',build)
        self.assertTrue(any('upload-artifact@' in s.get('uses','') for s in jobs['build-and-test']['steps']))
        self.assertTrue(any('download-artifact@' in s.get('uses','') for s in jobs['publish']['steps']))


if __name__=='__main__':unittest.main(verbosity=2)
