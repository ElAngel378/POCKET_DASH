"""Visual regression for graphics/VRAM refactors: frame hashes of a ROM vs a reference ROM.

    python tools/visual_regression.py REF.gb|ref.json NEW.gb [--levels 0-12] [--frames 3000] [--step 10]
    python tools/visual_regression.py REF.gb --save ref.json     (record only)

Both ROMs must be PROFILE=1 builds (if bank 10 overflows: make PROFILE=1 PROF_EXTRA=-DPROF_NO_MARKS,
without the profiler's section markers; the level jump still works) (they start the level directly, god mode). Every level is run as
DMG and CGB in Mesen's test runner (tools/mesen_shots.lua); the screenshot of every STEP-th frame is
hashed. Differing frames are listed (--dump N prints that frame of a model as PNG to out_*.png).
"""
import argparse
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MESEN = Path(r"C:\Users\soter\OneDrive\Documents\Mesen.exe")


def symbol(rom, name):
    import re
    noi = rom.with_suffix(".noi").read_text()
    return int(re.search(r"DEF _%s (0x[0-9A-Fa-f]+)" % name, noi).group(1), 16)


def run(rom, level, model, frames, step, jump, dump=-1):
    lua = (ROOT / "tools" / "mesen_shots.lua").read_text()
    lua = (lua.replace("@LEVEL_ADDR@", hex(symbol(rom, "gplevel"))).replace("@LEVEL@", str(level))
              .replace("@FRAMES@", str(frames)).replace("@STEP@", str(step)).replace("@JUMP@", str(jump))
              .replace("@DUMP@", str(dump)).replace("@ENDSTATE_ADDR@", hex(symbol(rom, "end_anim_state")))
              .replace("@PLAYER_PTR@", hex(symbol(rom, "gpplayer"))))
    script = ROOT / "temp_prof" / f"shots_{rom.stem}_{level}_{model}.lua"
    script.write_text(lua)
    r = subprocess.run([str(MESEN), "--testrunner", str(rom), str(script),
                        "--Gameboy.Model=" + ("Gameboy" if model == "dmg" else "GameboyColor"), "--Gameboy.RamPowerOnState=AllZeros"],
                       capture_output=True, text=True, timeout=1200)
    if r.returncode != 0:
        sys.exit(f"Mesen failed on {rom} level {level} {model}: {r.stdout[-500:]}")
    out = {}
    for l in r.stdout.splitlines():
        if l.startswith("F"):
            f, h = l.split()
            out[f] = h
        elif l.startswith("OAM "):
            (ROOT / "temp_prof" / f"out_{rom.stem}_{level}_{model}.oam").write_text(l[4:])
        elif l.startswith("PNG "):
            (ROOT / "temp_prof" / f"out_{rom.stem}_{level}_{model}.png").write_bytes(bytes.fromhex(l[4:]))
    return out


def collect(rom, levels, frames, step, jump, workers):
    jobs = [(lv, m) for lv in levels for m in ("dmg", "cgb")]
    with ThreadPoolExecutor(workers) as ex:
        res = list(ex.map(lambda j: run(rom, j[0], j[1], frames, step, jump), jobs))
    return {f"{lv}_{m}": r for (lv, m), r in zip(jobs, res)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("new", nargs="?")
    ap.add_argument("--levels", default="0-12")
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--step", type=int, default=10)
    ap.add_argument("--jump", type=int, default=0)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--save")
    a = ap.parse_args()
    lo, _, hi = a.levels.partition("-")
    levels = range(int(lo), int(hi or lo) + 1)
    if a.ref.endswith(".json"):
        ref = json.loads(Path(a.ref).read_text())
    else:
        ref = collect(Path(a.ref).resolve(), levels, a.frames, a.step, a.jump, a.workers)
    if a.save:
        Path(a.save).write_text(json.dumps(ref))
    if not a.new:
        return
    new = collect(Path(a.new).resolve(), levels, a.frames, a.step, a.jump, a.workers)
    bad = 0
    for k in new:
        diff = [f for f in ref[k] if ref[k][f] != new[k].get(f)]
        print(f"{k}: {len(ref[k])} frames, {len(diff)} differ" + (f" first at {diff[:5]}" if diff else ""))
        bad += len(diff)
    print("IDENTICAL" if not bad else f"{bad} frames differ")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
