#!/usr/bin/env python3
"""Verify official rhun downloads and a published DOOM binary for integration tests."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def fetch(url, path):
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'rhun-doom-tests'})
            with urllib.request.urlopen(request, timeout=60) as response, path.open('wb') as output:
                shutil.copyfileobj(response, output)
            return
        except (urllib.error.URLError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)


def release(repo, version, filename, checksum_name, folder):
    base = f'https://github.com/{repo}/releases/download/v{version}/'
    archive = folder / filename
    checksums = folder / checksum_name
    fetch(base + checksum_name, checksums)
    expected = {}
    for line in checksums.read_text().splitlines():
        digest, name = line.split()
        expected[name.lstrip('*')] = digest
    if filename not in expected:
        raise ValueError('Release does not include a checksum for ' + filename)
    fetch(base + filename, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected[filename]:
        raise ValueError('Release checksum does not match ' + filename)
    return archive


def unpack(archive, folder, mac=False):
    folder.mkdir()
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                parts = name.replace('\\', '/').split('/')
                if name.startswith(('/', '\\')) or '..' in parts or any(':' in p for p in parts):
                    raise ValueError('Invalid release path: ' + name)
            if not mac:
                z.extractall(folder)
        if mac:
            subprocess.run(['ditto', '-x', '-k', str(archive), str(folder)], check=True)
    else:
        with tarfile.open(archive) as t:
            t.extractall(folder, filter='data')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('target', choices=('macos-arm64', 'linux-x86_64', 'windows-x64'))
    ap.add_argument('--host', type=Path, default=ROOT / 'build/rhun-host')
    ap.add_argument('--published-doom', default='0.1.0')
    args = ap.parse_args()
    host = args.host.resolve()
    version = (host / 'VERSION').read_text().strip()
    for value in (version, args.published_doom):
        if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?', value):
            raise ValueError('Invalid release version: ' + value)
    target = args.target
    suffix = '.tar.gz' if target == 'linux-x86_64' else '.zip'
    host_target = 'windows-x86_64' if target == 'windows-x64' else target
    build = host / 'build'
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='rhun-download-', dir=ROOT / 'build') as tmp:
        work = Path(tmp)
        archive = release('vshvedov/rhun', version, f'rhun-{version}-{host_target}{suffix}',
                          'SHA256SUMS', work)
        extracted = work / 'editor'
        unpack(archive, extracted, mac=target == 'macos-arm64')
        if target == 'macos-arm64':
            app = build / 'rhun.app'
            if app.exists():
                raise FileExistsError('Editor download destination already exists: ' + str(app))
            shutil.move(str(extracted / 'rhun.app'), app)
            subprocess.run(['codesign', '--verify', '--strict', '--deep', str(app)], check=True)
            binary = app / 'Contents/MacOS/rhun'
        elif target == 'windows-x64':
            windows = build / 'windows'
            windows.mkdir()
            binary = windows / 'rhun.exe'
            shutil.copy2(extracted / f'rhun-{version}/rhun.exe', binary)
        else:
            binary = build / 'rhun'
            shutil.copy2(extracted / f'rhun-{version}/bin/rhun', binary)
        actual = subprocess.check_output([str(binary), '--version'], text=True).strip()
        if actual != 'rhun ' + version:
            raise ValueError('Editor download reports ' + actual)
        archive = release('rhun-editor/doom', args.published_doom, f'doom-{target}.tar.gz',
                          'sha256sums.txt', work)
        published = ROOT / 'build/published-doom' / target / 'doom'
        published.parent.mkdir(parents=True, exist_ok=True)
        unpack(archive, published)
    values = f'DOOM_RHUN={binary}\nDOOM_PUBLISHED_ROOT={published}\n'
    if os.environ.get('GITHUB_ENV'):
        with open(os.environ['GITHUB_ENV'], 'a', encoding='utf-8') as f:
            f.write(values)
    print(f'Official {actual} and published DOOM {args.published_doom} verified for {target}.')
    print(values, end='')


if __name__ == '__main__':
    main()
