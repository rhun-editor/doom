#!/usr/bin/env python3
"""Reject accidental dependencies on a developer toolchain in release binaries."""
import os
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
target = sys.argv[1]
binary = root / 'bin' / ('doom-' + target + ('.exe' if target == 'windows-x64' else ''))
if target == 'windows-x64':
    compiler = Path(os.environ['CC'])
    text = subprocess.check_output([str(compiler.with_name('objdump.exe')), '-p', str(binary)], text=True)
    imports = re.findall(r'DLL Name: (.+)', text)
    assert imports
    assert not any(n.lower().startswith(('libgcc', 'libstdc++', 'libwinpthread', 'zlib')) for n in imports), imports
elif target == 'macos-arm64':
    text = subprocess.check_output(['otool', '-L', str(binary)], text=True)
    imports = [line.strip().split(' ')[0] for line in text.splitlines()[1:]]
    assert all(n.startswith(('/usr/lib/', '/System/Library/')) for n in imports), imports
else:
    text = subprocess.check_output(['ldd', str(binary)], text=True)
    imports = text.splitlines()
    assert 'not found' not in text and 'libz.so' not in text, text
print('Runtime dependencies:', '\n'.join(imports))
