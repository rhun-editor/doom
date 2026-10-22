#!/usr/bin/env python3
"""Extract a release download and run its launcher, without a compiler or user config."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]
target = {'darwin': 'macos-arm64', 'win32': 'windows-x64'}.get(sys.platform, 'linux-x86_64')
suffix = '.tar.gz' if sys.platform.startswith('linux') else '.zip'
archive = root / 'dist' / ('rhun-doom-preview-' + target + suffix)
dest = root / 'build/test-results/preview'
dest.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='Doom preview é ') as tmp:
    if suffix == '.zip':
        with zipfile.ZipFile(archive) as z:
            z.extractall(tmp)
            if sys.platform != 'win32':
                for info in z.infolist():
                    if info.external_attr >> 16:
                        (Path(tmp) / info.filename).chmod(info.external_attr >> 16)
    else:
        with tarfile.open(archive) as t:
            t.extractall(tmp, filter='data')
    stage = Path(tmp) / 'rhun-doom-preview'
    wad = stage / 'extensions/rhun-editor/doom/data/doom1.wad'
    assert hashlib.sha256(wad.read_bytes()).hexdigest() == \
        '1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771'
    script = Path(tmp) / 'launch.rsc'
    script.write_text('wait 2000\nclick 550 400\nkey Space\nwait 600\nkey-up Space\n'
                      'wait 300\nprint-views\nprint-extensions\nshot ' +
                      (dest / 'launched.ppm').as_posix() + '\nquit\n')
    extra = ['--headless', '1100x800', '--scale', '1', '--script', str(script)]
    if sys.platform == 'win32':
        args = ['cmd', '/d', '/c', str(stage / 'Play DOOM.cmd'), *extra]
    else:
        launcher = 'Play DOOM.command' if sys.platform == 'darwin' else 'Play DOOM.sh'
        args = ['sh', str(stage / launcher), *extra]
    result = subprocess.run(args, cwd=stage, env=dict(os.environ, DOOM_DIAGNOSTICS='1'),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
    output = result.stdout.decode(errors='replace')
    (dest / 'launcher.log').write_text(output)
    state = stage / 'state/rhun/extensions/rhun-editor--doom'
    logfile = state.with_suffix('.log')
    log = logfile.read_text() if logfile.exists() else ''
    (dest / 'doom.log').write_text(log)
    assert result.returncode == 0, output + log
    assert 'title=DOOM' in output, output + log
    assert 'DOOM audio device: 0' in log, log
    assert 'DOOM music synth: 0' in log, log
    a = [json.loads(line) for line in (state / 'doom-stats.jsonl').read_text().splitlines()]
    assert max(s['music_samples'] for s in a) > 1000, a
    assert min(s['ammo'] for s in a) < 50, a
    print('Extracted preview launcher, Unicode paths, bundled assets, firing and music: passed.')
