#!/usr/bin/env python3
"""A separate editor preview plus the installed extension. No user settings are touched."""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pack', ROOT / 'tools/package.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('target', choices=pack.TARGETS)
    ap.add_argument('--host', type=Path, default=ROOT / 'build/rhun-host')
    args = ap.parse_args()
    stage = ROOT / 'build/preview' / args.target
    if stage.exists():
        shutil.rmtree(stage)
    ext = stage / 'extensions/rhun-editor/doom'
    pack.layout(args.target, ext)
    manifest = ext / 'extension.ini'
    manifest.write_text(manifest.read_text().replace('on = command', 'on = startup'), encoding='utf-8')
    (stage / 'playground').mkdir()
    config = stage / 'config/rhun/config'
    config.parent.mkdir(parents=True)
    config.write_text('[ui]\nagents_panel = false\nsidebar = false\n[updates]\ncheck = false\n'
                      '[files]\nrestore_session = false\nrestore_project = false\n[git]\nenabled = false\n')
    if args.target == 'macos-arm64':
        shutil.copytree(args.host / 'build/rhun.app', stage / 'rhun.app')
        executable = 'rhun.app/Contents/MacOS/rhun'
    elif args.target == 'windows-x64':
        shutil.copy2(args.host / 'build/windows/rhun.exe', stage / 'rhun.exe')
        executable = 'rhun.exe'
    else:
        shutil.copy2(args.host / 'build/rhun', stage / 'rhun')
        executable = 'rhun'
    if args.target == 'windows-x64':
        (stage / 'Play DOOM.cmd').write_text(
            '@echo off\ncd /d "%~dp0"\nset "HOME=%~dp0state"\nset "USERPROFILE=%~dp0state"\n'
            'set "XDG_CONFIG_HOME=%~dp0config"\nset "XDG_STATE_HOME=%~dp0state"\n'
            'set "DOOM_AUTOSTART=1"\n'
            '"%~dp0rhun.exe" "%~dp0playground" --empty --extensions "%~dp0extensions" %*\n')
    else:
        launcher = stage / ('Play DOOM.command' if args.target == 'macos-arm64' else 'Play DOOM.sh')
        launcher.write_text(
            '#!/bin/sh\nset -eu\ncd "$(dirname "$0")"\nroot=$PWD\nmkdir -p "$root/state"\n'
            'HOME="$root/state" XDG_CONFIG_HOME="$root/config" XDG_STATE_HOME="$root/state" '
            'DOOM_AUTOSTART=1 exec "$root/' + executable + '" "$root/playground" '
            '--empty --extensions "$root/extensions" "$@"\n')
        launcher.chmod(0o755)
    shutil.copy2(args.host / 'LICENSE', stage / 'EDITOR-LICENSE.txt')
    shutil.copy2(args.host / 'assets/licenses/LICENSE-Unicode.txt', stage / 'EDITOR-UNICODE-LICENSE.txt')
    (stage / 'README.txt').write_text(
        'DOOM in rhun: separate editor preview\n\n'
        'Extract this entire folder, then run Play DOOM. No compiler or runtime is required.\n'
        'On Linux, run ./"Play DOOM.sh". On macOS, the preview editor is not notarized.\n'
        'Your regular editor is not replaced. Preview settings stay in config/ and state/.\n'
        'Click the game. WASD move, arrows turn, Space fires, E opens doors, P pauses.\n\n'
        'The bundled editor provides paired-key and audio support.\n'
        'Windows extensions are not sandboxed by rhun. Linux audio permission includes local\n'
        'audio-service access. This extension uses playback only.\n'
        'See extensions/rhun-editor/doom/data/ for the shareware and SoundFont notices.\n'
        'Corresponding sources are doom-source.tar.gz and rhun-preview-source.tar.gz\n'
        'in the same GitHub release.\n', encoding='utf-8')
    out = ROOT / 'dist'
    out.mkdir(exist_ok=True)
    if args.target in ('macos-arm64', 'windows-x64'):
        filename = out / ('rhun-doom-preview-' + args.target + '.zip')
        with zipfile.ZipFile(filename, 'w', zipfile.ZIP_DEFLATED) as z:
            for p in sorted(stage.rglob('*')):
                if p.is_file():
                    z.write(p, 'rhun-doom-preview/' + p.relative_to(stage).as_posix())
    else:
        filename = out / ('rhun-doom-preview-' + args.target + '.tar.gz')
        with tarfile.open(filename, 'w:gz') as t:
            t.add(stage, arcname='rhun-doom-preview')
    print(filename)


if __name__ == '__main__':
    main()
