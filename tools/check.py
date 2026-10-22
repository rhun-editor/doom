#!/usr/bin/env python3
"""Run the pinned protocol checker on the packaged extension on every OS."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
checker = root / 'build/checker'
commit = '396830ce2b5295416f59d58a0599bde3aa663ba6'
def run(*args, **kw):
    return subprocess.run(args, check=True, **kw)
if not (checker / '.git').exists():
    run('git', 'init', '-q', str(checker))
    run('git', '-C', str(checker), 'fetch', '-q', '--depth', '1',
        'https://github.com/rhun-editor/extension-protocol.git', commit)
    run('git', '-C', str(checker), 'checkout', '-q', '--detach', 'FETCH_HEAD')
patch = str(root / 'tests/checker.patch')
check = subprocess.run(['git', '-C', str(checker), 'apply', '--check', patch], capture_output=True)
if check.returncode == 0:
    run('git', '-C', str(checker), 'apply', patch)
else:
    run('git', '-C', str(checker), 'apply', '--reverse', '--check', patch)
run('go', 'run', './conformance/ext', os.environ.get('DOOM_PACKAGE_ROOT', str(root)), cwd=checker)
