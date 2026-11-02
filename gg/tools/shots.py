#!/usr/bin/env python3
"""Headless screenshots with Mesen's test runner.
Usage: python gg/tools/shots.py <rom> <out_prefix> <frame> [frame...]  -> <out_prefix>_<frame>.png"""
import os, subprocess, sys
GG = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
MESEN = r'C:\Users\soter\OneDrive\Documents\Mesen.exe'
rom, prefix, frames = sys.argv[1], sys.argv[2], sys.argv[3:]
lua = open(os.path.join(GG, 'tools', 'mesen_shot.lua')).read().replace('@FRAMES@', ', '.join(frames))
script = os.path.join(GG, 'build', 'shots_run.lua')
open(script, 'w').write(lua)
r = subprocess.run([MESEN, '--testrunner', rom, script], capture_output=True, text=True, timeout=300)
n = 0
for line in r.stdout.splitlines():
    if line.startswith('SHOT '):
        _, fr, hx = line.split(' ', 2)
        open('%s_%s.png' % (prefix, fr), 'wb').write(bytes.fromhex(hx))
        n += 1
print('saved %d screenshot(s), mesen exit %d' % (n, r.returncode))
