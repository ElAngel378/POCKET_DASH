"""Headless per-section cycle profile of the gameplay loop with Mesen.

    python tools/profile.py --level 9 --dmg --frames 3000

Builds bin/POCKETDASH_prof.gb (make PROFILE=1: section markers, god mode, starts the level
directly; the normal ROM is never touched), runs it as a DMG (--dmg) or CGB in
tools/mesen_profile.lua in Mesen's test runner and prints the report: dropped frames, average /
max cycles per section, the slowest iterations and the camera x of every dropped frame.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MESEN = Path(r"C:\Users\soter\OneDrive\Documents\Mesen.exe")
ROM = ROOT / "bin" / "POCKETDASH_prof.gb"


def build():
    r = subprocess.run(["make", "-j8", "PROFILE=1"], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode != 0 or not ROM.exists():
        sys.exit(r.stdout[-3000:] + r.stderr[-3000:])


def symbol(name):
    noi = (ROOT / "bin" / "POCKETDASH_prof.noi").read_text()
    m = re.search(r"DEF _%s (0x[0-9A-Fa-f]+)" % name, noi)
    if not m:
        sys.exit(f"symbol {name} not found")
    return int(m.group(1), 16)


def calls_lua(path, offset):
    """Lua: input A from a per-joypad()-call file, counted from the first gameplay iteration."""
    it = open(path).read().strip()
    return """
local CALLS, CALL_OFS, ncalls, cstarted = "%s", %d, 0, false
emu.addMemoryCallback(function() if cstarted then ncalls = ncalls + 1 end end, emu.callbackType.exec, %d)
emu.addMemoryCallback(function(addr, value) if value == 9 then cstarted = true end end, emu.callbackType.write, %d)
emu.addEventCallback(function()
  local c = ncalls + CALL_OFS
  emu.setInput({a = cstarted and c >= 1 and c <= #CALLS and CALLS:sub(c, c) == "1"}, 0)
end, emu.eventType.inputPolled)
""" % (it, offset, symbol("joypad"), symbol("gpmark"))


def trace_lua(path):
    """Lua: writes the recorded player state for the current camera x every frame."""
    import json
    t = json.load(open(path))
    rows = "\n".join("[%d]={%s}," % (int(k), ",".join(map(str, v))) for k, v in sorted(t.items(), key=lambda kv: int(kv[0])))
    pp, cx = symbol("gpplayer"), symbol("gpcamx")
    return """
local TRACE = {
%s
}
emu.addEventCallback(function()
  local M = emu.memType.gameboyMemory
  local x = emu.read(%d, M) + 256 * emu.read(%d, M)
  local p = emu.read(%d, M) + 256 * emu.read(%d, M)
  local r = TRACE[x]
  if r and p >= 0xC000 then
    emu.write(p + 24, r[1] %% 256, M); emu.write(p + 25, math.floor(r[1] / 256), M)
    emu.write(p + 3, r[2], M); emu.write(p + 2, 0, M)
    emu.write(p + 9, r[3], M); emu.write(p + 19, r[4], M); emu.write(p + 8, r[5], M)
    emu.write(p + 4, r[6], M); emu.write(p + 5, r[7], M)
  end
end, emu.eventType.endFrame)
""" % (rows, cx, cx + 1, pp, pp + 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", type=int, default=0)
    ap.add_argument("--dmg", action="store_true")
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--skip", type=int, default=400, help="frames of level loading to ignore")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--no-music", action="store_true", help="switch the music setting off (music cost)")
    ap.add_argument("--mirror", type=int, default=0, help="flip player.reversed every N frames (profiles mirror_reload; 0: off)")
    ap.add_argument("--jump", type=int, default=0, help="press A for 6 frames every N frames (0: no input)")
    ap.add_argument("--calls", help="A held (1) or not (0) at each joypad() call from the level start: replays a "
                    "recorded playthrough exactly (inputs follow the game's own reads, whatever the frame timing)")
    ap.add_argument("--calls-offset", type=int, default=0, help="--calls: input index of the first counted call")
    ap.add_argument("--trace", help="JSON {camera x: [y_base, y, mode, mini, gravity, vel lo, vel hi]}: "
                    "the player follows a recorded playthrough (god mode), so the camera moves like a real one")
    a = ap.parse_args()

    if not a.no_build:
        build()
    rom = ROM
    lua = (ROOT / "tools" / "mesen_profile.lua").read_text()
    lua = (lua.replace("@MARK_ADDR@", hex(symbol("gpmark"))).replace("@CAMX_ADDR@", hex(symbol("gpcamx")))
              .replace("@RUN_FRAMES@", str(a.frames)).replace("@SKIP_FRAMES@", str(a.skip))
              .replace("@LEVEL@", str(a.level)).replace("@LEVEL_ADDR@", hex(symbol("gplevel")))
              .replace("@MUSIC_ADDR@", hex(symbol("setting_music_enabled")) if a.no_music else "nil")
              .replace("@JUMP@", str(a.jump)).replace("@MIRROR@", str(a.mirror))
              .replace("@PLAYER_PTR_ADDR@", hex(symbol("gpplayer"))))
    if a.trace:
        lua += trace_lua(a.trace)
    if a.calls:
        lua += calls_lua(a.calls, a.calls_offset)
    script = ROOT / "temp_prof" / ("profile_run_%d_%s.lua" % (a.level, "dmg" if a.dmg else "cgb"))
    script.write_text(lua)
    # The model is a command line setting override (the user's own Mesen settings are not changed)
    model = "--Gameboy.Model=" + ("Gameboy" if a.dmg else "GameboyColor")
    r = subprocess.run([str(MESEN), "--testrunner", str(rom), str(script), model], capture_output=True,
                       text=True, timeout=900)
    out = [l for l in r.stdout.splitlines() if "Uninitialized" not in l]
    print("\n".join(out))
    if r.returncode != 0:
        sys.exit(f"Mesen exit code {r.returncode} (-1 = Lua error)")


if __name__ == "__main__":
    main()
