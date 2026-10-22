#!/usr/bin/env python3
"""Exercise OS key-release handlers in an isolated native window on Linux and Windows CI."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import socket
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
    if sys.platform != 'win32':
        manifest=ext/'extension.ini'
        manifest.write_text(manifest.read_text().replace('on = command','on = startup'))
    for d in ('config/rhun', 'state', 'tmp', 'project'):
        (w / d).mkdir(parents=True, exist_ok=True)
    (w / 'config/rhun/config').write_text('[ui]\nsidebar = false\nagents_panel = false\n'
                                          '[updates]\ncheck = false\n[git]\nenabled = false\n')
    script = w / 'native.rsc'
    script.write_text('wait 200\ncmd doom.play\nwait 12000\nshot ' +
                      (dest / 'native.ppm').as_posix() + '\nwait 100\nquit\n')
    env = dict(os.environ, HOME=str(w), USERPROFILE=str(w), APPDATA=str(w / 'config'),
               LOCALAPPDATA=str(w / 'state'), XDG_CONFIG_HOME=str(w / 'config'),
               XDG_STATE_HOME=str(w / 'state'), TMPDIR=str(w / 'tmp'), TEMP=str(w / 'tmp'),
               TMP=str(w / 'tmp'), DOOM_DIAGNOSTICS='1', DOOM_AUTOSTART='1')
    logfile = w / 'state/rhun/extensions/rhun-editor--doom.log'
    statsfile = w / 'state/rhun/extensions/rhun-editor--doom/doom-stats.json'
    control=w/'control.sock'
    driver=['--script',str(script)] if sys.platform=='win32' else ['--control',str(control)]
    proc = subprocess.Popen([os.environ['DOOM_RHUN'], str(w / 'project'), '--empty', '--scale', '1',
                             '--extensions', str(w / 'extensions'), *driver],
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
            user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            handles = []
            callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            def visit(hwnd, param):
                pid = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                name = ctypes.create_unicode_buffer(100)
                user.GetClassNameW(hwnd, name, 100)
                if pid.value == proc.pid and name.value == 'rhunWindow': handles.append(hwnd)
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
            def key(name, down): xdo('keydown' if down else 'keyup', 'space' if name=='Space' else name)
        start = until(lambda s: s['focused'] and not s['paused'])
        assert start['paired'], 'The native host did not negotiate paired key events.'
        for name, duration in (('w', .35), ('Right', .3), ('Space', .6)):
            key(name, True); time.sleep(duration); key(name, False)
        released = until(lambda s: s['ammo'] < 50 and s['angle'] != start['angle'])
        time.sleep(.8)
        settled = stats()
        time.sleep(.5)
        still = stats()
        assert (settled['x'], settled['y'], settled['angle'], settled['ammo']) == \
               (still['x'], still['y'], still['angle'], still['ammo']), (settled, still)
        key('p', True)
        if sys.platform == 'win32':
            for _ in range(3): time.sleep(.1); key('p', True)
        time.sleep(.9)
        paused = stats()
        assert paused['paused'], paused
        key('p', False)
        time.sleep(.4)
        assert stats()['paused'], 'Auto-repeat toggled the manual pause.'
        if sys.platform!='win32':
            with socket.socket(socket.AF_UNIX) as client:
                client.connect(str(control))
                client.settimeout(3)
                client.sendall(('shot '+(dest/'native.ppm').as_posix()+'\n').encode())
                client.recv(4096)
                end=time.monotonic()+2
                while not (dest/'native.ppm').exists() and time.monotonic()<end: time.sleep(.05)
                client.sendall(b'quit\n')
                client.shutdown(socket.SHUT_WR)
                while client.recv(4096): pass
        output = proc.communicate(timeout=20)[0].decode(errors='replace')
        (dest / 'editor.log').write_text(output)
        log = logfile.read_text()
        (dest / 'doom.log').write_text(log)
        for name in ('w', 'Right', 'Space', 'p'):
            assert 'key-up - ' + name in log, log
        assert proc.returncode == 0, output
        assert (dest/'native.ppm').is_file(), 'Native screenshot was not produced.'
        print('Native window: movement, turning, firing, releases and held pause passed.')
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        if logfile.exists(): (dest / 'doom.log').write_text(logfile.read_text())
        if statsfile.exists(): (dest / 'stats.json').write_text(statsfile.read_text())
