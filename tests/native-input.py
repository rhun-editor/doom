#!/usr/bin/env python3
"""Exercise OS key-release handlers in an isolated native window on Linux and Windows CI."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[1]
target = 'windows-x64' if sys.platform == 'win32' else 'linux-x86_64'
package = root / 'build/package' / target / 'doom'
dest = root / 'build/test-results/native-input'
dest.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='doom-native-') as folder:
    w = Path(folder).resolve()
    ext = w / 'extensions/rhun-editor/doom'
    shutil.copytree(package, ext)
    for d in ('config/rhun', 'state', 'tmp', 'project'):
        (w / d).mkdir(parents=True, exist_ok=True)
    (w / 'config/rhun/config').write_text('[ui]\nsidebar = false\nagents_panel = false\n'
                                          '[updates]\ncheck = false\n[git]\nenabled = false\n')
    script = w / 'native.rsc'
    script.write_text('wait 200\ncmd doom.play\nwait 12000\nshot ' +
                      (dest / 'native.ppm').as_posix() + '\nquit\n')
    env = dict(os.environ, HOME=str(w), USERPROFILE=str(w), APPDATA=str(w / 'config'),
               LOCALAPPDATA=str(w / 'state'), XDG_CONFIG_HOME=str(w / 'config'),
               XDG_STATE_HOME=str(w / 'state'), TMPDIR=str(w / 'tmp'), TEMP=str(w / 'tmp'),
               TMP=str(w / 'tmp'), DOOM_DIAGNOSTICS='1')
    logfile = w / 'state/rhun/extensions/rhun-editor--doom.log'
    statsfile = w / 'state/rhun/extensions/rhun-editor--doom/doom-stats.json'
    proc = subprocess.Popen([os.environ['DOOM_RHUN'], str(w / 'project'), '--empty', '--scale', '1',
                             '--extensions', str(w / 'extensions'), '--script', str(script)],
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    def stats():
        try:
            return json.loads(statsfile.read_text())
        except (OSError, ValueError):
            return {}
    def until(condition):
        end = time.monotonic() + 7
        while time.monotonic() < end and proc.poll() is None:
            value = stats()
            if value and condition(value): return value
            time.sleep(.05)
        raise AssertionError('Native input did not reach the game. ' + (logfile.read_text() if logfile.exists() else ''))
    try:
        until(lambda s: s['frames'] > 0)
        if sys.platform == 'win32':
            user = ctypes.WinDLL('user32', use_last_error=True)
            from ctypes import wintypes
            user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
            handles = []
            callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            def visit(hwnd, param):
                pid = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == proc.pid: handles.append(hwnd)
                return True
            user.EnumWindows(callback(visit), 0)
            assert handles, 'The native window was not created.'
            hwnd = handles[0]
            def post(message, key=0, data=0):
                assert user.PostMessageW(hwnd, message, key, data)
            post(6, 1)
            post(0x201, 1, 550 | (400 << 16)); post(0x202, 0, 550 | (400 << 16))
            codes = {'w': 87, 'Right': 39, 'Space': 32, 'p': 80}
            def key(name, down): post(0x100 if down else 0x101, codes[name], 1 if down else (3 << 30) | 1)
        else:
            def xdo(*args):
                return subprocess.check_output(['xdotool', *args], text=True).strip()
            window = xdo('search', '--onlyvisible', '--class', '^rhun$').splitlines()[-1]
            xdo('windowfocus', '--sync', window)
            xdo('mousemove', '--window', window, '550', '400'); xdo('click', '1')
            def key(name, down): xdo('keydown' if down else 'keyup', name)
        start = until(lambda s: s['focused'] and not s['paused'])
        for name, duration in (('w', .35), ('Right', .3), ('Space', .6)):
            key(name, True); time.sleep(duration); key(name, False)
        released = until(lambda s: s['ammo'] < 50 and s['angle'] != start['angle'])
        time.sleep(.8)
        settled = stats()
        time.sleep(.5)
        still = stats()
        assert (settled['x'], settled['y'], settled['angle'], settled['ammo']) == \
               (still['x'], still['y'], still['angle'], still['ammo']), (settled, still)
        key('p', True); time.sleep(.9)
        paused = stats()
        assert paused['paused'], paused
        key('p', False)
        time.sleep(.4)
        assert stats()['paused'], 'Auto-repeat toggled the manual pause.'
        output = proc.communicate(timeout=20)[0].decode(errors='replace')
        (dest / 'editor.log').write_text(output)
        log = logfile.read_text()
        (dest / 'doom.log').write_text(log)
        for name in ('w', 'Right', 'Space', 'p'):
            assert 'key-up - ' + name in log, log
        assert proc.returncode == 0, output
        print('Native window: movement, turning, firing, releases and held pause passed.')
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
