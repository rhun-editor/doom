#!/usr/bin/env python3
"""Ship complete corresponding source beside binary downloads."""
import argparse
import gzip
from pathlib import Path
import subprocess
import tarfile

root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--host', type=Path, default=root / 'build/rhun-host')
args = ap.parse_args()
out = root / 'dist'
out.mkdir(exist_ok=True)
with gzip.open(out / 'doom-source.tar.gz', 'wb') as f:
    f.write(subprocess.check_output(['git', 'archive', '--format=tar', '--prefix=doom-source/', 'HEAD'],
                                   cwd=root))
with tarfile.open(out / 'rhun-preview-source.tar.gz', 'w:gz') as t:
    files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=args.host).decode().split('\0')
    for name in sorted(n for n in files if n):
        path = args.host / name
        if path.is_file():
            t.add(path, arcname='rhun-preview-source/' + name, recursive=False)
print('Complete extension and patched editor source archives are ready.')
