#!/usr/bin/env python3
"""Install the real release archive through rhun, approve its declared needs, then play."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
target = {'darwin': 'macos-arm64', 'win32': 'windows-x64'}.get(sys.platform, 'linux-x86_64')
archive = root / 'dist' / ('doom-' + target + '.tar.gz')
dest = root / 'build/test-results/install'
dest.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='doom-install-') as folder:
    w = Path(folder).resolve()
    for name in ('config/rhun', 'state', 'project', 'extensions', 'tmp'):
        (w / name).mkdir(parents=True, exist_ok=True)
    (w / 'config/rhun/config').write_text('[updates]\ncheck = false\n[ui]\nsidebar = false\nagents_panel = false\n')
    downloads = w / 'downloads'
    downloads.mkdir()
    local_archive = downloads / archive.name
    shutil.copy2(archive, local_archive)
    (downloads / 'sha256sums.txt').write_text(
        hashlib.sha256(local_archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')
    script = w / 'install.rsc'
    script.write_text('ext-install-archive ' + local_archive.as_posix() + '\nwait-install\nprint-dialog\n'
                      'dialog yes\nwait 200\ncmd doom.play\nwait 2200\nprint-views\n'
                      'print-ext-versions rhun-editor/doom\nprint-toast\nquit\n')
    env = dict(os.environ, HOME=str(w), USERPROFILE=str(w), APPDATA=str(w / 'config'),
               LOCALAPPDATA=str(w / 'state'), XDG_CONFIG_HOME=str(w / 'config'),
               XDG_STATE_HOME=str(w / 'state'), TMPDIR=str(w / 'tmp'), TEMP=str(w / 'tmp'), TMP=str(w / 'tmp'))
    result = subprocess.run([os.environ['DOOM_RHUN'], str(w / 'project'), '--headless', '1100x800',
                             '--scale', '1', '--extensions', str(w / 'extensions'), '--script', str(script)],
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    output = result.stdout.decode(errors='replace')
    (dest / 'install.log').write_text(output)
    assert result.returncode == 0, output
    assert 'title=DOOM' in output, output
    assert 'source=archive' in output, output
    installed = w / 'extensions/rhun-editor/doom'
    assert (installed / 'data/SHAREWARE-LICENSE.txt').is_file(), output
    assert (installed / 'data/SOUNDFONT-LICENSE.txt').is_file(), output
    assert hashlib.sha256((installed / 'data/doom1.wad').read_bytes()).hexdigest() == \
        '1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771'
    print('Release archive installed through rhun and started the game with bundled assets.')
