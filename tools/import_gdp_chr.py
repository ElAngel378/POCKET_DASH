"""One-time import of the Theory of Everything objects into the object catalog (run 2026-10-22).

The art is levels/objects/source/GDP TOE.chr and GDP Pulsing Obj.chr (Game Boy 2bpp tiles, drawn
by us for Pocket Dash; the pieces sit on an 8 px grid, so they are cut at their own positions):
    - BG squares (GDA's BIG / MEDIUM / SMALL_BG_SQUARES): background decorations, 3 groups of
      pieces placed side by side
    - the Theory of Everything brick blocks: solid, named by the sides that have the white outline
    - bush ground spikes (GDA's GROUND_BUSH_SPIKE_*): on the floor and (flipped) on the ceiling,
      deadly in their half of the cell like the pit spikes (GDA: an 8 x 10 box at the bottom)
    - a bush decoration (GDA's BUSH_DECO_*)
The pulsing shapes of both files (circle, ring, heart, square, triangle, X, arrow, !, ?) are
sprite decorations, not catalog objects: tools/build_sprite_tiles.py (PULSE_GDP) and
src/sprites/famidash_sprites.c.

    python tools/import_gdp_chr.py
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from objects_catalog import load_catalog  # noqa: E402
from import_gda_16 import append_objects, flip  # noqa: E402

SOURCE = ROOT / "levels" / "objects" / "source"
TOE = SOURCE / "GDP TOE.chr"
PULSE = SOURCE / "GDP Pulsing Obj.chr"
COL_NONE, COL_ALL, COL_DEATH_TOP_HALF, COL_DEATH_BOTTOM_HALF = 0, 7, 0x10, 0x11
DECO_PALETTE = 2
# (name, file, x, y of the 16 x 16 piece's top left, collision, palette)
PIECES = [("BG_SQUARES_A_%d" % (k + 1), TOE, 16 * k, 24, COL_NONE, DECO_PALETTE) for k in range(4)] + \
         [("BG_SQUARES_B_%d" % (k + 1), TOE, 72 + 16 * k, 8, COL_NONE, DECO_PALETTE) for k in range(3)] + \
         [("BG_SQUARES_C_%d" % (k + 1), TOE, 72 + 16 * k, 32, COL_NONE, DECO_PALETTE) for k in range(3)] + \
         [("TOE_BRICK", TOE, x, y, COL_ALL, 0) for x, y in ((0, 56), (24, 56), (48, 56), (72, 56), (0, 80), (24, 80), (48, 80))] + \
         [("BUSH_SPIKE_%d" % (k + 1), TOE, x, 104, COL_DEATH_TOP_HALF, 0) for k, x in enumerate((16, 40, 56))] + \
         [("BUSH_DECO", PULSE, 16, 40, COL_NONE, DECO_PALETTE)]
OUTLINE = 1   # the bricks' white outline colour


def piece(data, x0, y0):
    def px(x, y):
        t = (y // 8) * 16 + x // 8
        lo, hi = data[t * 16 + 2 * (y % 8)], data[t * 16 + 2 * (y % 8) + 1]
        b = 7 - x % 8
        return ((lo >> b) & 1) | (((hi >> b) & 1) << 1)
    return [[px(x0 + x, y0 + y) for x in range(16)] for y in range(16)]


def outline_sides(a):
    """the sides of a brick with the outline colour all along them: TOP, BOTTOM, LEFT, RIGHT"""
    edges = {"TOP": a[0], "BOTTOM": a[15], "LEFT": [r[0] for r in a], "RIGHT": [r[15] for r in a]}
    sides = [s for s, e in edges.items() if sum(v == OUTLINE for v in e) >= 12]
    # a lone outline pixel in a corner no side covers (an inside corner): <corner>_DOT
    for v, h, y, x in (("TOP", "LEFT", 0, 0), ("TOP", "RIGHT", 0, 15), ("BOTTOM", "LEFT", 15, 0), ("BOTTOM", "RIGHT", 15, 15)):
        if v not in sides and h not in sides and a[y][x] == OUTLINE:
            sides.append("%s_%s_DOT" % (v, h))
    return sides


def main():
    cat = load_catalog()
    data = {f: f.read_bytes() for f in (TOE, PULSE)}
    new = []
    for name, f, x, y, col, pal in PIECES:
        a = piece(data[f], x, y)
        if not any(any(r) for r in a):
            continue
        if name == "TOE_BRICK":
            sides = outline_sides(a)
            name += "_" + ("_".join(sides) if sides else "NO_OUTLINE")
        src = "%s (%d, %d)" % (f.name, x, y)
        new.append(dict(name=name, collision=col, palette=pal, art=a, src=src))
        if name.startswith("BUSH_SPIKE"):
            new.append(dict(name=name + "_TOP", collision=COL_DEATH_BOTTOM_HALF, palette=pal,
                            art=flip(a, 0, 1), src=src + " V"))
    names = [o["name"] for o in new]
    assert len(set(names)) == len(names), names
    new = [o for o in new if o["name"] not in cat.by_name]
    if not new:
        raise SystemExit("nothing new to add")
    append_objects(cat, new)


if __name__ == "__main__":
    main()
