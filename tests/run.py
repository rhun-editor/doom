#!/usr/bin/env python3
"""Run real Doom inside a sandboxed, isolated, headless rhun. No user config is used."""
import json
import os
from pathlib import Path
import re
import shutil
import sys
import signal
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
TARGET={'darwin':'macos-arm64','win32':'windows-x64'}.get(sys.platform,'linux-x86_64')
BINARY='doom-'+TARGET+('.exe' if sys.platform=='win32' else '')
PACKAGE=Path(os.environ.get('DOOM_PACKAGE_ROOT',ROOT))
BIN=PACKAGE/'bin'/BINARY
RHUN=os.environ.get('DOOM_RHUN')
if not RHUN and (ROOT/'.local-rhun').exists(): RHUN=(ROOT/'.local-rhun').read_text().strip()
RHUN=RHUN or shutil.which('rhun')
ARTIFACTS=Path(os.environ.get('DOOM_TEST_RESULTS',ROOT/'build/test-results')).resolve()
CONFIG='''[ui]
agents_panel = false
sidebar = false
[editor]
cursor_blink = false
[files]
restore_session = false
restore_project = false
[updates]
check = false
[git]
enabled = false
'''

class Gameplay(unittest.TestCase):
    def check_confinement(self,out):
        if sys.platform=='win32':
            self.assertIn('confined=0',out)  # rhun currently has no Windows extension sandbox
        elif os.environ.get('DOOM_ALLOW_UNCONFINED'):
            self.assertRegex(out,r'confined=[012]')  # Docker only; native CI requires confinement
        else:
            self.assertRegex(out,r'confined=[12]')

    def run_case(self,name,actions,*,audio=True,wad='valid',audio_backend=None):
        self.assertTrue(RHUN and Path(RHUN).is_file(),'Set DOOM_RHUN to a rhun 0.18.2 or newer executable.')
        with tempfile.TemporaryDirectory(prefix='rhun-doom-') as folder:
            w=Path(folder).resolve()
            for d in ['config/rhun','state','tmp','project','extensions/rhun-editor/doom/bin','extensions/rhun-editor/doom/data']:
                (w/d).mkdir(parents=True,exist_ok=True)
            ext=w/'extensions/rhun-editor/doom'
            manifest=(PACKAGE/'extension.ini').read_text()
            (ext/'extension.ini').write_text(manifest if audio else manifest.replace('audio = yes\n',''))
            shutil.copy2(BIN,ext/'bin'/BINARY)
            for asset in ('TimGM6mb.sf2','SOUNDFONT-LICENSE.txt','SHAREWARE-LICENSE.txt'):
                shutil.copy2(PACKAGE/'data'/asset,ext/'data'/asset)
            if wad=='valid':shutil.copy2(PACKAGE/'data/doom1.wad',ext/'data/doom1.wad')
            elif wad=='invalid':(ext/'data/doom1.wad').write_bytes(b'not a game')
            (w/'config/rhun/config').write_text(CONFIG)
            (w/'project/note.txt').write_text('Back to coding.\n')
            dest=ARTIFACTS/name;dest.mkdir(parents=True,exist_ok=True)
            script='\n'.join(['wait 100',*actions,'quit'])+'\n'
            script=script.replace('{shot}',dest.as_posix()).replace('{project}',(w/'project').as_posix())
            (w/'script.rsc').write_text(script)
            env=dict(os.environ,HOME=str(w),XDG_CONFIG_HOME=str(w/'config'),XDG_STATE_HOME=str(w/'state'),
                     TMPDIR=str(w/'tmp'),TEMP=str(w/'tmp'),TMP=str(w/'tmp'),
                     USERPROFILE=str(w),APPDATA=str(w/'config'),LOCALAPPDATA=str(w/'state'),
                     DOOM_DIAGNOSTICS='1')
            if audio_backend=='native': env.pop('DOOM_AUDIO_BACKEND',None)
            args=[str(RHUN),str(w/'project'),'--headless','1100x800','--scale','1',
                  '--extensions',str(w/'extensions'),'--script',str(w/'script.rsc')]
            proc=subprocess.Popen(args,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=sys.platform!='win32')
            try:out,err=proc.communicate(timeout=40)
            except subprocess.TimeoutExpired:
                proc.kill() if sys.platform=='win32' else os.killpg(proc.pid,signal.SIGKILL)
                proc.wait();self.fail('The editor or game stopped responding.')
            logpath=w/'state/rhun/extensions/rhun-editor--doom.log'
            log=logpath.read_text() if logpath.exists() else ''
            output=(out+err).decode(errors='replace')
            (dest/'editor.log').write_text(output);(dest/'doom.log').write_text(log);(dest/'actions.rsc').write_text(script)
            state=w/'state/rhun/extensions/rhun-editor--doom'
            records=[]
            if (state/'doom-stats.jsonl').exists():
                records=[json.loads(s) for s in (state/'doom-stats.jsonl').read_text().splitlines()]
                (dest/'stats.json').write_text(json.dumps(records,indent=2)+'\n')
            self.assertEqual(proc.returncode,0,output+'\n'+log)
            self.assertNotIn('unknown command',output)
            self.assertNotRegex(output,r'state=(crashed|stopped|failed)')
            self.assertNotIn('DOOM frame:',log)
            self.assertNotIn('DOOM draw:',log)
            return records,output,log

    def test_gameplay_controls_audio_pause_reopen_and_resize(self):
        actions=['cmd doom.play','wait 2200','shot {shot}/start.ppm','click 550 400','wait 200',
                 'key w','wait 800','key-up w','wait 300','key Right','wait 450','key-up Right','wait 150',
                 'key Space','wait 1000','key-up Space','wait 450','shot {shot}/playing.ppm',
                 'key p','key-up p','wait 500','focus out','wait 300','focus in','wait 300','shot {shot}/paused.ppm',
                 'key p','key-up p','wait 200','open {project}/note.txt','wait 400','cmd close_tab','wait 100',
                 'click 550 400','wait 200','cmd close_tab','wait 300','cmd doom.play','wait 500',
                 'click 550 400','wait 400','resize 860 620','wait 200','shot {shot}/resized.ppm',
                 'wait 5000','print-extensions','print-views','print-toast']
        a,out,log=self.run_case('gameplay',actions)
        self.assertTrue(any(s['paired'] for s in a),'This test needs the optional view-key-state editor patch.')
        self.assertIn('DOOM audio device: 0',log)
        self.assertIn('DOOM music synth: 0',log)
        playing=[s for s in a if s['focused'] and not s['paused']]
        self.assertGreater(len(playing),20)
        self.assertGreater(len({(s['x'],s['y']) for s in playing}),5,'Movement did not advance.')
        self.assertGreater(len({s['angle'] for s in playing}),3,'Turning did not advance.')
        self.assertLess(min(s['ammo'] for s in playing),50,'Firing did not use ammunition.')
        self.assertGreater(max(s['sounds'] for s in a),0)
        self.assertGreater(max(s['music_samples'] for s in a),1000,'The music synthesizer did not render samples.')
        self.assertGreater(max(s['samples'] for s in a),1000,'The real audio callback did not mix sound.')
        # A manual pause must survive losing and regaining window focus.
        groups=[];current=[]
        for s in a:
            if s['paused'] and s['frames']>25:current.append(s)
            elif current:groups.append(current);current=[]
        if current:groups.append(current)
        long_pause=[g for g in groups if g[-1]['wall_ms']-g[0]['wall_ms']>=700]
        self.assertTrue(long_pause,'The manual pause did not remain active.')
        self.assertTrue(any(g[-1]['ticks_ms']==g[0]['ticks_ms'] for g in long_pause),'Game time advanced while paused.')
        self.assertRegex(out,r'view=2 title=DOOM .*size=860x518')
        self.check_confinement(out)
        last=playing[-1];prior=next(s for s in reversed(playing) if s['wall_ms']<=last['wall_ms']-3000)
        fps=(last['frames']-prior['frames'])*1000/(last['wall_ms']-prior['wall_ms'])
        self.assertGreater(fps,12,'Frame delivery is too slow to play.')
        summary={'steady_fps':round(fps,1),'frames':a[-1]['frames'],'sounds':a[-1]['sounds'],'mixed_samples':a[-1]['samples']}
        (ARTIFACTS/'gameplay/summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        print('Gameplay:',json.dumps(summary))

    @unittest.skipUnless(sys.platform=='darwin' and not os.environ.get('DOOM_AUDIO_BACKEND'),
                         'speaker sandbox enforcement needs a physical macOS audio device')
    def test_audio_is_not_granted_without_the_manifest_permission(self):
        _,out,log=self.run_case('no-audio',['cmd doom.play','wait 2200','print-extensions'],audio=False)
        self.assertRegex(log,r'DOOM audio device: -[0-9]+')
        self.assertIn('DOOM ready:',log)
        self.check_confinement(out)

    @unittest.skipUnless(os.environ.get('DOOM_AUDIO_BACKEND'), 'native audio is already used by this run')
    def test_native_audio_initializes_or_fails_without_breaking_the_game(self):
        a,out,log=self.run_case('native-audio',['cmd doom.play','wait 2200','print-views'],audio_backend='native')
        match=re.search(r'DOOM audio device: (-?\d+) \(([^)]+)\)',log)
        self.assertIsNotNone(match,log)
        self.assertNotEqual(match[2],'Null')
        self.assertIn('DOOM ready:',log)
        self.assertIn('title=DOOM',out)
        print('Native audio backend:',match[0])

    def test_released_controls_stop_movement_turning_and_firing(self):
        actions=['cmd doom.play','wait 2200','click 550 400','wait 100',
                 'key w','wait 400','key-up w','wait 800',
                 'key Right','wait 400','key-up Right','wait 500',
                 'key Space','wait 800','key-up Space','wait 1800']
        a,_,log=self.run_case('key-release',actions)
        active=[s for s in a if s['focused'] and not s['paused']]
        self.assertGreater(len({(s['x'],s['y']) for s in active}),3)
        self.assertGreater(len({s['angle'] for s in active}),3)
        self.assertGreater(active[-1]['ammo'],0)
        self.assertLess(active[-1]['ammo'],50)
        quiet=[s for s in active if s['wall_ms']>=active[-1]['wall_ms']-1000]
        self.assertGreaterEqual(len(quiet),6)
        self.assertEqual(len({(s['x'],s['y'],s['angle'],s['ammo']) for s in quiet}),1,
                         'Movement, turning or firing continued after releasing the controls.')
        for key in ('w','Right','Space'):
            self.assertIn('key-up - '+key,log)

    def test_quitting_from_the_game_menu_can_be_reopened(self):
        actions=['cmd doom.play','wait 2200','click 550 400','wait 100',
                 'key Escape','key-up Escape','wait 100','key Up','key-up Up','wait 100',
                 'key Return','key-up Return','wait 100','key y','key-up y','wait 600',
                 'print-views','print-extensions','cmd doom.play','wait 500','print-views']
        _,out,log=self.run_case('quit-menu',actions)
        self.assertNotRegex(out,r'view=1 title=DOOM')
        self.assertRegex(out,r'view=2 title=DOOM')
        self.assertNotIn('audio device: -',log)

    def test_missing_game_data_has_a_useful_message(self):
        _,out,_=self.run_case('missing-data',['cmd doom.play','wait 300','print-toast','print-extensions'],wad='missing')
        self.assertIn('game data is missing',out)

    def test_invalid_game_data_is_rejected_before_starting_the_engine(self):
        _,out,log=self.run_case('invalid-data',['cmd doom.play','wait 300','print-toast','print-extensions'],wad='invalid')
        self.assertIn('Invalid DOOM IWAD',out)
        self.assertNotIn('DOOM ready:',log)

if __name__=='__main__':unittest.main()
