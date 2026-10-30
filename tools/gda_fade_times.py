"""Fade time markers for our levels' colour triggers from GDA's colour triggers (their Frames).

    python tools/gda_fade_times.py            report only
    python tools/gda_fade_times.py --write    put the markers into levels/chr_data/tmx/*.tmx

Our levels' triggers come from Famidash, which cannot fade (NES): it fakes a slow fade with a
chain of triggers a few blocks apart. GDA has one trigger with a fade time. A chain is kept (its
colours) and each step fades until the next, so the whole chain takes about GDA's time. Our triggers sit ~160 px after GDA's (the
camera lead). Markers: tools/tmx2sprites.py (sprites 246..250); a column's BG and ground triggers
share one marker (the background's wins). A step's time: to the next step of its chain; the last
step: the rest of GDA's time (at least 0.3 s); GDA's instant triggers: instant.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda  # noqa: E402

TMX = ROOT / "levels" / "chr_data" / "tmx"
SPRITE_FIRSTGID = 4097
PX_PER_FRAME = 2.79          # the player's speed (one speed for every level)
LEAD = 160                   # our trigger x - GDA's
WINDOW = (64, 320)           # a chain's first step: GDA x + this range
# marker sprite -> fade frames (tools/tmx2sprites.py; no marker: 18)
MARKERS = {246: 1, None: 18, 247: 30, 248: 60, 249: 120, 250: 240}
GDA_NAMES = {"ultiatedestruction": "ultimatedestruction"}


def nearest_marker(frames):
    # nearest by ratio (a 3 frame fade is closer to instant than to 0.3 s)
    import math
    return min(MARKERS, key=lambda m: abs(math.log(max(frames, 1) / MARKERS[m])))


def gda_triggers(name):
    w, h, layers, objs = gda.read_level(name)
    out = []
    for o in objs:
        # colour triggers by their properties (some levels' sprite tileset has another firstgid)
        if "Frames" in o["props"] and o["props"].get("Channel") in ("BG", "GROUND"):
            out.append((o["x"], "bg" if o["props"]["Channel"] == "BG" else "g", int(o["props"].get("Frames", 0))))
    return sorted(set(out))


def read_sp(path):
    s = path.read_text(encoding="utf-8")
    root_w = int(re.search(r'<map [^>]*\swidth="(\d+)"', s).group(1))
    m = re.search(r'(<layer [^>]*name="SP"[^>]*>\s*<data encoding="csv">)(.*?)(</data>)', s, re.S)
    cells = [int(c) for c in m.group(2).replace("\n", "").replace("\r", "").split(",") if c.strip()]
    return s, m, root_w, cells


def our_triggers(cells, w):
    out = []
    for i, gid in enumerate(cells):
        t = gid - SPRITE_FIRSTGID
        if 128 <= t < 176:
            out.append((i % w, "bg", t))
        elif 192 <= t < 240:
            out.append((i % w, "g", t))
    return sorted(set(out))


def plan(level):
    """{column: marker sprite or None}, report lines"""
    path = TMX / (level + ".tmx")
    s, m, w, cells = read_sp(path)
    ours = our_triggers(cells, w)
    gt = gda_triggers(GDA_NAMES.get(level, level))
    want = {}   # (column, channel) -> frames
    lines = []
    for ch in ("bg", "g"):
        g = [t for t in gt if t[1] == ch]
        o = [t for t in ours if t[1] == ch]
        used = set()
        for k, (gx, _, frames) in enumerate(g):
            nxt = g[k + 1][0] if k + 1 < len(g) else 1e9
            # the chain: our steps from GDA x + WINDOW[0] until the next GDA trigger's window
            span = gx + LEAD + frames * PX_PER_FRAME   # GDA's fade end, in our x
            # a long fade's chain may start well after GDA's trigger (Famidash spreads the steps)
            steps = [c for c, _, _ in o if gx + WINDOW[0] <= c * 16 < min(nxt + WINDOW[0], max(span, gx + WINDOW[1]) + WINDOW[1])
                     and c not in used]
            if not steps:
                lines.append("  %-6s GDA x %6d %4d frames: no trigger of ours" % (ch, gx, frames))
                continue
            used.update(steps)
            first = steps[0] * 16
            end_x = (first if first <= gx + WINDOW[1] else gx + LEAD) + frames * PX_PER_FRAME
            desc = []
            for i, c in enumerate(steps):
                if frames == 0:
                    f = 1
                elif i + 1 < len(steps):
                    f = (steps[i + 1] - c) * 16 / PX_PER_FRAME   # reaches the next step
                else:
                    f = (end_x - c * 16) / PX_PER_FRAME          # the rest of GDA's time
                    if f < MARKERS[None]:
                        f = MARKERS[None]   # the chain ran past GDA's time: the usual 0.3 s
                want[(c, ch)] = f
                desc.append("%d:%d" % (c, round(f)))
            lines.append("  %-6s GDA x %6d %4d frames -> %s" % (ch, gx, frames, " ".join(desc)))
        for c, _, t in o:
            if c not in used:
                lines.append("  %-6s ours col %d (id %d): no GDA trigger, left at 0.3 s" % (ch, c, t))
    cols = {}
    for (c, ch), f in want.items():
        if ch == "bg" or c not in cols:
            cols[c] = nearest_marker(f)
    return path, cols, lines


def write(path, cols):
    s, m, w, cells = read_sp(path)
    h = len(cells) // w
    for i, gid in enumerate(cells):   # old markers out
        if 246 <= gid - SPRITE_FIRSTGID <= 250:
            cells[i] = 0
    for c, mk in cols.items():
        if mk is None:
            continue
        row = next(r for r in range(h) if cells[r * w + c] == 0)
        cells[row * w + c] = SPRITE_FIRSTGID + mk
    text = "\n" + ",\n".join(",".join(str(v) for v in cells[r * w:(r + 1) * w]) for r in range(h)) + "\n"
    s = s[:m.start(2)] + text + s[m.end(2):]
    path.write_text(s, encoding="utf-8", newline="")


def main():
    do_write = "--write" in sys.argv
    gda_levels = set(gda.level_names())
    for p in sorted(TMX.glob("*.tmx")):
        level = p.stem
        if level == "test" or GDA_NAMES.get(level, level) not in gda_levels:
            continue
        path, cols, lines = plan(level)
        n = sum(1 for v in cols.values() if v is not None)
        print("%s: %d markers" % (level, n))
        for l in lines:
            print(l)
        if do_write:
            write(path, cols)


if __name__ == "__main__":
    main()
