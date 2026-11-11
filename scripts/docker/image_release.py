#!/usr/bin/env python3
"""Guarded Dev image publication. No registry access except the explicit publish command."""
import argparse
import base64
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

REPOSITORY = 'Shawna7han/mumble-funk'
IMAGE = 'ghcr.io/shawna7han/mumble-funk'
ROOT = Path(__file__).resolve().parents[2]
SHA = re.compile(r'[0-9a-f]{40}')
DIGEST = re.compile(r'sha256:[0-9a-f]{64}')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def verify_source(root, repository, event, ref, event_sha):
    """Tag and payload must identify the exact clean commit already contained in Dev."""
    match = re.fullmatch(r'refs/tags/ghcr-dev-([0-9a-f]{40})', ref)
    if repository != REPOSITORY or event != 'push' or not match:
        raise RuntimeError('Only an explicit ghcr-dev-<full SHA> tag push in the project is allowed.')
    commit = git(root, 'rev-parse', 'HEAD^{commit}')
    if match[1] != commit:
        raise RuntimeError('Tag name does not identify the checked-out commit.')
    if git(root, 'cat-file', '-t', ref) != 'tag' or git(root, 'rev-parse', ref+'^{commit}') != commit:
        raise RuntimeError('An annotated publication tag on exactly this commit is required.')
    tag_object = git(root, 'rev-parse', ref)
    if event_sha not in (commit, tag_object):
        raise RuntimeError('GitHub event SHA does not identify this tag/commit.')
    # A feature commit descending FROM dev is insufficient: it must be IN dev.
    git(root, 'rev-parse', '--verify', 'refs/remotes/origin/dev^{commit}')
    if subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor',
                       commit, 'refs/remotes/origin/dev']).returncode:
        raise RuntimeError('Publication source is not contained in the fetched origin/dev.')
    if subprocess.run(['git', '-C', str(root), 'diff', '--quiet', 'HEAD', '--']).returncode:
        raise RuntimeError('Tracked publication source has local modifications.')
    return commit


def metadata(commit, created=None):
    if not SHA.fullmatch(commit):
        raise ValueError('A full lowercase commit SHA is required.')
    created = created or datetime.now(timezone.utc).isoformat(timespec='seconds').replace('+00:00', 'Z')
    if not re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z', created):
        raise ValueError('Build time must use UTC RFC3339 seconds.')
    datetime.strptime(created, '%Y-%m-%dT%H:%M:%SZ')
    version = 'dev-'+commit
    labels = {
        'org.opencontainers.image.source': 'https://github.com/'+REPOSITORY,
        'org.opencontainers.image.revision': commit,
        'org.opencontainers.image.version': version,
        'org.opencontainers.image.created': created,
        # Project source + shipped audio; dependencies retain their own notices.
        'org.opencontainers.image.licenses': 'BSD-3-Clause AND CC0-1.0',
    }
    return {'commit': commit, 'version': version, 'image': IMAGE+':'+version, 'labels': labels}


def read_metadata(path):
    value = json.loads(path.read_text())
    expected = metadata(value['commit'], value['labels']['org.opencontainers.image.created'])
    if value != expected:
        raise RuntimeError('Unexpected publication metadata, image name or labels.')
    return value


def build_command(value, proxy_ca=None):
    command = ['docker', 'build', '--platform', 'linux/amd64', '--tag', value['image']]
    for key, item in value['labels'].items():
        command += ['--label', key+'='+item]
    if proxy_ca:
        command += ['--secret', 'id=proxy_ca,src='+str(proxy_ca)]
    return command+['.']


def inspect_image(value, expected_id=None):
    info = json.loads(subprocess.check_output(['docker', 'image', 'inspect', value['image']], text=True))[0]
    config = info['Config']
    if (info['Architecture'] != 'amd64' or info['Os'] != 'linux'
            or config['User'] != '10001:10001' or not config.get('Healthcheck')
            or any(config.get('Labels', {}).get(k) != v for k,v in value['labels'].items())):
        raise RuntimeError('Image architecture, user, healthcheck or OCI provenance mismatch.')
    if not DIGEST.fullmatch(info['Id']) or (expected_id and info['Id'] != expected_id):
        raise RuntimeError('Image differs from the artifact tested by the read-only build job.')
    return info


def registry_token(actor, credential):
    if not actor or not credential:
        raise RuntimeError('GitHub actor/token missing; no registry access.')
    # Credentials stay in memory/HTTP headers; never arguments, logs or image layers.
    authorization = base64.b64encode((actor+':'+credential).encode()).decode()
    request = Request('https://ghcr.io/token?service=ghcr.io&scope=repository:shawna7han/mumble-funk:pull,push',
                      headers={'Authorization': 'Basic '+authorization})
    with urlopen(request, timeout=30) as response:
        value = json.loads(response.read(65536))
    token = value.get('token') or value.get('access_token')
    if not isinstance(token, str) or not token:
        raise RuntimeError('GHCR authentication did not return a token.')
    return token


def manifest_digest(version, token):
    if not re.fullmatch(r'dev-[0-9a-f]{40}', version):
        raise ValueError('Only commit-specific Dev manifests are permitted.')
    request = Request('https://ghcr.io/v2/shawna7han/mumble-funk/manifests/'+version, method='HEAD',
                      headers={'Authorization': 'Bearer '+token,
                               'Accept': 'application/vnd.oci.image.manifest.v1+json, application/vnd.docker.distribution.manifest.v2+json'})
    try:
        with urlopen(request, timeout=30) as response:
            digest = response.headers.get('Docker-Content-Digest', '')
    except HTTPError as error:
        if error.code == 404:
            return None
        raise RuntimeError('GHCR manifest check failed (HTTP '+str(error.code)+'); publication stopped.') from None
    if not DIGEST.fullmatch(digest):
        raise RuntimeError('GHCR did not return a valid immutable manifest digest.')
    return digest


def publish(value, expected_id, actor, credential):
    inspect_image(value, expected_id)
    token = registry_token(actor, credential)
    if manifest_digest(value['version'], token) is not None:
        raise RuntimeError('This Dev image tag already exists; refusing to overwrite it.')
    # Authenticate only after the tested image and remote absence are verified.
    subprocess.run(['docker', 'login', 'ghcr.io', '--username', actor, '--password-stdin'],
                   input=credential+'\n', text=True, capture_output=True, check=True)
    subprocess.run(['docker', 'push', value['image']], check=True)
    digest = manifest_digest(value['version'], token)
    if digest is None:
        raise RuntimeError('Push returned but the remote manifest could not be verified.')
    info = inspect_image(value, expected_id)
    pinned = IMAGE+'@'+digest
    if pinned not in info.get('RepoDigests', []):
        raise RuntimeError('Remote manifest digest differs from the pushed local image.')
    return dict(value, digest=digest, pinned_image=pinned)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('verify', 'metadata', 'build', 'inspect', 'publish'))
    parser.add_argument('--file', type=Path, default=Path('build/ghcr/metadata.json'))
    parser.add_argument('--expected-id')
    parser.add_argument('--proxy-ca', type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == 'verify':
            commit = verify_source(ROOT, os.environ.get('GITHUB_REPOSITORY', ''),
                                   os.environ.get('GITHUB_EVENT_NAME', ''), os.environ.get('GITHUB_REF', ''),
                                   os.environ.get('GITHUB_SHA', ''))
            # Only validated hex/name values enter workflow outputs.
            output = os.environ.get('GITHUB_OUTPUT')
            if output:
                with open(output, 'a') as target:
                    target.write('commit='+commit+'\nimage='+IMAGE+':dev-'+commit+'\n')
            print('Verified Dev source: '+commit)
        elif args.command == 'metadata':
            value = metadata(git(ROOT, 'rev-parse', 'HEAD^{commit}'))
            args.file.parent.mkdir(parents=True, exist_ok=True)
            args.file.write_text(json.dumps(value, indent=2)+'\n')
        else:
            value = read_metadata(args.file)
            if args.command == 'build':
                subprocess.run(build_command(value, args.proxy_ca), cwd=ROOT, check=True)
            elif args.command == 'inspect':
                print(inspect_image(value, args.expected_id)['Id'])
            else:
                if not args.expected_id or not DIGEST.fullmatch(args.expected_id):
                    raise RuntimeError('The tested image ID is required before publication.')
                if verify_source(ROOT, os.environ.get('GITHUB_REPOSITORY', ''), os.environ.get('GITHUB_EVENT_NAME', ''),
                                 os.environ.get('GITHUB_REF', ''), os.environ.get('GITHUB_SHA', '')) != value['commit']:
                    raise RuntimeError('Artifact source differs from the publication tag.')
                result = publish(value, args.expected_id, os.environ.get('GITHUB_ACTOR', ''),
                                 os.environ.get('GITHUB_TOKEN', ''))
                destination = args.file.with_name('publication.json')
                destination.write_text(json.dumps(result, indent=2)+'\n')
                summary = os.environ.get('GITHUB_STEP_SUMMARY')
                if summary:
                    with open(summary, 'a') as target:
                        target.write('## Verified experimental Dev image\n\nSource: `'+value['commit']+'`\n\n'
                                     'Image: `'+value['image']+'`\n\nDigest pin: `'+result['pinned_image']+'`\n')
                print('Verified published digest: '+result['pinned_image'])
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError, URLError):
        # Do not print exception/request representations: they may contain credentials.
        print('Dev image operation failed. Source, metadata, artifact or registry verification was refused.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
