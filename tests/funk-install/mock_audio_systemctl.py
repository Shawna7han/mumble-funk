#!/usr/bin/env python3
"""Disposable-container test double only: actual binary, no systemd boot/supervision."""
import json
import os
from pathlib import Path
import pwd
import signal
import subprocess
import sys
import time

if not Path('/.dockerenv').exists():
    sys.exit('Only for disposable test containers')
args = sys.argv[1:]
command = next((arg for arg in args if not arg.startswith('-')), '')
if any('mumble-funk' in arg and 'mumble-funk-audio-engine' not in arg for arg in args) or any('mumble-server' in arg for arg in args):
    sys.exit('Test failure: reference service addressed')
with Path('/tmp/audio-systemctl-calls').open('a') as file:
    file.write(' '.join(args)+'\n')
statefile = Path('/tmp/audio-service-state.json')
state = json.loads(statefile.read_text()) if statefile.exists() else {'pid': 0, 'enabled': False}

def stop():
    if state['pid']:
        try:
            os.kill(state['pid'], signal.SIGTERM)
        except ProcessLookupError:
            pass
        for _ in range(150):
            if not Path(f"/proc/{state['pid']}").exists():
                break
            time.sleep(.02)
        else:
            sys.exit('Preview did not stop cleanly')
    state['pid'] = 0


def start():
    user = pwd.getpwnam('mumble-funk-audio-engine')
    with Path('/var/log/mumble-funk-audio-engine/server.log').open('ab') as output:
        process = subprocess.Popen(['/opt/mumble-funk-audio-engine/current/mumble-server', '-ini', '/etc/mumble-funk-audio-engine/server.ini', '-fg'],
                                   user=user.pw_uid, group=user.pw_gid, extra_groups=[], cwd='/var/lib/mumble-funk-audio-engine',
                                   stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
    state['pid'] = process.pid

if command == 'show':
    prop = next((arg.split('=',1)[1] for arg in args if arg.startswith('--property=')), '')
    print({'Version':'container-test-double', 'MainPID':str(state['pid']),
           'FragmentPath':'/etc/systemd/system/mumble-funk-audio-engine.service' if Path('/etc/systemd/system/mumble-funk-audio-engine.service').exists() else '',
           'ExecStart':'/opt/mumble-funk-audio-engine/current/mumble-server -ini /etc/mumble-funk-audio-engine/server.ini -fg'}.get(prop,''))
elif command == 'enable':
    state['enabled'] = True
elif command in ('stop', 'start', 'restart', 'disable'):
    if command in ('stop','restart','disable'):
        stop()
    if command in ('start','restart'):
        start()
    if command == 'disable':
        state['enabled'] = False
elif command == 'is-enabled':
    print('enabled' if state['enabled'] else 'disabled')
    sys.exit(0 if state['enabled'] else 1)
elif command in ('is-active', 'status'):
    try:
        if not state['pid']:
            raise ProcessLookupError
        os.kill(state['pid'],0)
    except ProcessLookupError:
        print('inactive')
        sys.exit(3)
    print('active')
elif command != 'daemon-reload':
    sys.exit('Unsupported test invocation '+command)
if os.geteuid() == 0:
    statefile.write_text(json.dumps(state))
    statefile.chmod(0o644)
