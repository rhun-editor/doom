#!/usr/bin/env python3
"""Build the exact extension layout used by installation and gameplay tests."""
import argparse
import hashlib
import io
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ('macos-arm64', 'linux-x86_64', 'windows-x64')


def layout(target, dest):
    binary = 'doom-' + target + ('.exe' if target == 'windows-x64' else '')
    dest.mkdir(parents=True, exist_ok=True)
    for name in ('extension.ini', 'README.md', 'LICENSE', 'THIRD_PARTY.md'):
        shutil.copy2(ROOT / name, dest / name)
    (dest / 'data').mkdir(exist_ok=True)
    for name in ('doom1.wad', 'TimGM6mb.sf2', 'SHAREWARE-LICENSE.txt',
                 'SOUNDFONT-LICENSE.txt', 'README.md'):
        shutil.copy2(ROOT / 'data' / name, dest / 'data' / name)
    (dest / 'bin').mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'bin' / binary, dest / 'bin' / binary)
    (dest / 'bin' / binary).chmod(0o755)
    shutil.copytree(ROOT / 'licenses', dest / 'licenses', dirs_exist_ok=True)
    for library in ('miniaudio', 'tinysoundfont', 'zlib'):
        shutil.copy2(ROOT / 'vendor' / library / 'LICENSE', dest / 'licenses' / (library + '.txt'))
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    (dest / 'SOURCE.txt').write_text(
        f'Corresponding source: https://github.com/rhun-editor/doom/tree/{commit}\n'
        'The release also includes doom-source.tar.gz with the complete source and build scripts.\n'
        'The shareware WAD and SoundFont have separate notices in data/.\n', encoding='utf-8')
    expected = '1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771'
    assert hashlib.sha256((dest / 'data/doom1.wad').read_bytes()).hexdigest() == expected
    assert hashlib.sha256((dest / 'data/TimGM6mb.sf2').read_bytes()).hexdigest() == \
        'c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854'
    for p in dest.rglob('*'):
        if p.is_symlink():
            raise ValueError('Package contains a symlink: ' + str(p))


def archive(folder, filename):
    with tarfile.open(filename, 'w:gz') as tar:
        for p in sorted(folder.rglob('*')):
            if p.is_file():
                tar.add(p, arcname=p.relative_to(folder).as_posix(), recursive=False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('target', choices=TARGETS)
    ap.add_argument('--layout', type=Path)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist')
    args = ap.parse_args()
    stage = args.layout or ROOT / 'build/package' / args.target / 'doom'
    if stage.exists():
        shutil.rmtree(stage)
    layout(args.target, stage)
    args.out.mkdir(parents=True, exist_ok=True)
    output = args.out / ('doom-' + args.target + '.tar.gz')
    archive(stage, output)
    print(output)


if __name__ == '__main__':
    main()
