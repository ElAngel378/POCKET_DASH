#!/usr/bin/env python3
"""Headless performance profile of the Game Gear build.

Builds a DEBUG_PROFILE ROM (god mode, so the run covers the whole level without dying) and runs
it in Mesen's --testrunner with tools/mesen_profile.lua, which prints CPU cycles per main-loop
section and every loop iteration that took longer than one frame.

Usage: python gg/tools/profile.py [frames] [extra -D flags...]
"""
import os
import re
import subprocess
import sys

GG = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
MESEN = r'C:\Users\soter\OneDrive\Documents\Mesen.exe'
TARGET = os.environ.get('GG_TARGET', 'gg')   # set GG_TARGET=sms to profile the Master System build


def main():
    frames = sys.argv[1] if len(sys.argv) > 1 else '4900'
    extra = ' '.join(sys.argv[2:])
    env = dict(os.environ, GG_EXTRA='-DDEBUG_PROFILE -DDEBUG_GODMODE ' + extra, GG_OUT='POCKETDASH_prof', GG_TARGET=TARGET)
    r = subprocess.run(['cmd', '/c', os.path.join(GG, 'build.bat')], env=env, capture_output=True, text=True)
    errors = [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l.lower()]
    if r.returncode or errors:
        sys.exit('\n'.join(errors) or 'build failed')

    mapfile = open(os.path.join(GG, 'build', 'POCKETDASH_prof.map')).read()
    m = re.search(r'([0-9A-F]{8})\s+_gpmark\b', mapfile)
    if not m:
        sys.exit('_gpmark not found in map')
    lua = open(os.path.join(GG, 'tools', 'mesen_profile.lua')).read()
    lua = lua.replace('@MARK_ADDR@', '0x' + m.group(1)).replace('@RUN_FRAMES@', frames)
    script = os.path.join(GG, 'build', 'profile_run.lua')
    open(script, 'w').write(lua)
    r = subprocess.run([MESEN, '--testrunner', os.path.join(GG, 'build', 'POCKETDASH_prof.' + TARGET), script],
                       capture_output=True, text=True, timeout=600)
    if r.returncode not in (0,):
        print('mesen exit code', r.returncode)
    for line in r.stdout.splitlines():
        if 'Uninitialized' not in line:
            print(line)


if __name__ == '__main__':
    main()
