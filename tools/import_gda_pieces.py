"""Adds GDA metatiles we do not have to the object catalog, with GDA's art as a placeholder
(tools/gda_pieces.py decides which: by default the pieces GDA's 1.0-1.6 official levels use).

Each new object: GDA's name (colour channel parts dropped), GDA's art in our 4 colours, the
closest collision our engine has (COLLISION), and GDA's own collision type in gda_collision, so
the mechanics still to come (thin slabs, slopes, breakable bricks, fading) know which objects
are approximations. No DMG art of its own: the DMG draws the same art.

    python tools/import_gda_pieces.py          (--all: every piece any GDA level uses)
"""
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda  # noqa: E402
import gda_pieces  # noqa: E402
from objects_catalog import ART, COLUMNS, TSX, load_catalog, write_preview  # noqa: E402

Q = 0x20   # COL_QUAD_BASE: quadrant types, col_quads[] in src/player.c
X = 0x40   # COL_BOX_BASE
S = 0x60   # COL_SLOPE_BASE
# GDA collision -> ours (include/collision.h); missing ones: no collision yet
COLLISION = {
    "COL_NONE": 0, "COL_FULL": 7, "COL_FLOOR_CEIL": 9,
    "COL_SPIKE_TOP": 8, "COL_SPIKE_BOTTOM": 8, "COL_SPIKE_LEFT": 8, "COL_SPIKE_RIGHT": 8,
    "COL_SMALL_SPIKE_BOTTOM": 0x10, "COL_SMALL_SPIKE_TOP": 0x11, "COL_SMALL_SPIKE_RIGHT": 1, "COL_SMALL_SPIKE_LEFT": 2,
    "COL_MEDIUM_SPIKE_BOTTOM": 0x10, "COL_MEDIUM_SPIKE_TOP": 0x11, "COL_MEDIUM_SPIKE_RIGHT": 1, "COL_MEDIUM_SPIKE_LEFT": 2,
    "COL_GROUND_SPIKE_BOTTOM": 0x10, "COL_GROUND_SPIKE_TOP": 0x11, "COL_GROUND_SPIKE_RIGHT": 1, "COL_GROUND_SPIKE_LEFT": 2,
    "COL_GROUND_BUSH_SPIKE_BOTTOM": 0x10, "COL_GROUND_BUSH_SPIKE_TOP": 0x11,
    "COL_GROUND_BUSH_SPIKE_RIGHT": 1, "COL_GROUND_BUSH_SPIKE_LEFT": 2,
    "COL_GROUND_WAVY_SPIKE_BOTTOM": 0x10, "COL_GROUND_WAVY_SPIKE_TOP": 0x11,
    "COL_GROUND_WAVY_SPIKE_RIGHT": 1, "COL_GROUND_WAVY_SPIKE_LEFT": 2,
    "COL_SLAB_TOP": 5, "COL_SLAB_BOTTOM": 6, "COL_SLAB_LEFT": Q + 0, "COL_SLAB_RIGHT": Q + 1,
    # box types (include/collision.h COL_BOX_BASE, col_boxes[] in src/player.c): GDA's rectangles
    "COL_SLAB_MED_TOP": X + 0, "COL_SLAB_MED_BOTTOM": X + 1, "COL_SLAB_MED_LEFT": X + 2, "COL_SLAB_MED_RIGHT": X + 3,
    "COL_SLAB_TINY_TOP": X + 4, "COL_SLAB_TINY_BOTTOM": X + 5, "COL_SLAB_TINY_LEFT": X + 6, "COL_SLAB_TINY_RIGHT": X + 7,
    "COL_EA_SLAB_TOP": X + 8, "COL_EA_SLAB_BOTTOM": X + 9, "COL_EA_SLAB_LEFT": X + 10, "COL_EA_SLAB_RIGHT": X + 11,
    "COL_EA_CORNER_SLAB_TOP_LEFT": X + 12, "COL_EA_CORNER_SLAB_TOP_RIGHT": X + 13,
    "COL_EA_CORNER_SLAB_BOTTOM_LEFT": X + 14, "COL_EA_CORNER_SLAB_BOTTOM_RIGHT": X + 15,
    "COL_EA_CORNER_INSIDE_SLAB_TOP_LEFT": X + 16, "COL_EA_CORNER_INSIDE_SLAB_TOP_RIGHT": X + 17,
    "COL_EA_CORNER_INSIDE_SLAB_BOTTOM_LEFT": X + 18, "COL_EA_CORNER_INSIDE_SLAB_BOTTOM_RIGHT": X + 19,
    "COL_OUTLINE_TOP": X + 20, "COL_OUTLINE_BOTTOM": X + 21, "COL_OUTLINE_LEFT": X + 22, "COL_OUTLINE_RIGHT": X + 23,
    "COL_CENTERED_MINI_BLOCK": X + 24,
    # slopes (include/collision.h COL_SLOPE_BASE, slope_lines[] in src/player.c), in GDA's order
    **{"COL_" + n: S + i for i, n in enumerate(['SLOPE_45_UP', 'SLOPE_45_DOWN', 'SLOPE_45_UP_UD', 'SLOPE_45_DOWN_UD', 'SLOPE_22_UP_1', 'SLOPE_22_UP_2', 'SLOPE_22_DOWN_1', 'SLOPE_22_DOWN_2', 'SLOPE_22_UP_UD_1', 'SLOPE_22_UP_UD_2', 'SLOPE_22_DOWN_UD_1', 'SLOPE_22_DOWN_UD_2', 'SLOPE_66_UP_1', 'SLOPE_66_UP_2', 'SLOPE_66_DOWN_1', 'SLOPE_66_DOWN_2', 'SLOPE_66_UP_UD_1', 'SLOPE_66_UP_UD_2', 'SLOPE_66_DOWN_UD_1', 'SLOPE_66_DOWN_UD_2'])},
    "BREAKABLE_BRICK": 0x16,   # COL_BREAKABLE: solid, breaks when the player runs into it
}
# Not added: GDA's level end wall (our level end is a sprite), and its old ground / ceiling pieces
# (art stamped "OLD": older copies of the ground look, which GROUND_0 / CEILING_0 and ours are)
SKIP = re.compile(r"ENDING_WALL|(GROUND|CEILING)_[1-9A-C]")
# Pieces whose GDA art is an editor marker: drawn as one colour, or with another piece's art
SOLID_ART = {"BLK_FILLER": 2}
ART_OF = {}
# GDA's fading block is drawn from its own frame tiles (level_routines.c fading_block_frames: a
# 1 px white outline, hollow), not a metatile
HOLLOW = {"FADING_BLOCK"}


def palette(name):
    """CGB palette of the object (src/famidash_bg.c): ground and ceiling looks 1, background
    spike bushes 2, fake spikes 5, the rest 0"""
    if re.fullmatch(r"(GROUND|CEILING)_\w", name):
        return 1
    if name.startswith("BUSH_GROUND_SPIKE"):
        return 2
    if name.startswith("FAKE_"):
        return 5
    return 0


def art(mid, name=""):
    """GDA's art of metatile mid in our colour indices: clear 0, white (outlines) 3, black 2, the
    rest (fills) 1"""
    if name in SOLID_ART:
        return [[SOLID_ART[name]] * 16 for _ in range(16)]
    if name in HOLLOW:
        return [[3 if x in (0, 15) or y in (0, 15) else 0 for x in range(16)] for y in range(16)]
    if name in ART_OF:
        mid = next(i for i, (n, _, _) in enumerate(gda.metatiles()) if n == ART_OF[name])
    img = gda.metatile_image(mid)
    px = img.load()
    out = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            r, g, b, a = px[x, y]
            if a < 128:
                continue
            lum = (r * 3 + g * 6 + b) / 10
            out[y][x] = 3 if lum > 200 else 2 if lum < 40 else 1
    return out


def main():
    cat = load_catalog()
    mts = gda.metatiles()
    pieces = gda_pieces.missing("all" if "--all" in sys.argv else None)
    new = [(p, mid) for p, (mid, _) in pieces.items() if not SKIP.fullmatch(p) and p not in cat.by_name]
    if not new:
        raise SystemExit("nothing new to add")
    first = max(cat.objects) + 1
    sheet = Image.open(ART).convert("L")
    rows = (first + len(new) + COLUMNS - 1) // COLUMNS
    if rows * 16 > sheet.height:
        grown = Image.new("L", (sheet.width, rows * 16), 255)
        grown.paste(sheet, (0, 0))
        sheet = grown
    tiles = []
    for k, (name, mid) in enumerate(new):
        tid = first + k
        a = art(mid, name)
        for y in range(16):
            for x in range(16):
                sheet.putpixel(((tid % COLUMNS) * 16 + x, (tid // COLUMNS) * 16 + y), 255 - 85 * a[y][x])
        gcol = mts[mid][2]
        tiles.append(
            ' <tile id="%d">\n  <properties>\n'
            '   <property name="name" value="%s"/>\n'
            '   <property name="collision" type="int" value="%d"/>\n'
            '   <property name="palette" type="int" value="%d"/>\n'
            '   <property name="famidash_id" type="int" value="-1"/>\n'
            '   <property name="dmg_art" type="int" value="-1"/>\n'
            '   <property name="gda_name" value="%s"/>\n'
            '   <property name="gda_collision" value="%s"/>\n%s'
            '  </properties>\n </tile>\n' % (tid, name, COLLISION.get(gcol, 0), palette(name), mts[mid][0], gcol,
                                             # GD 1.6's fading objects (mt_renderer.c fade_*)
                                             '   <property name="fade" type="bool" value="true"/>\n'
                                             if name.startswith("FADING_") else ""))
    sheet.save(ART)
    tsx = TSX.read_text(encoding="utf-8")
    TSX.write_text(tsx.replace("</tileset>", "".join(tiles) + "</tileset>"), encoding="utf-8")
    write_preview()
    print("added %d objects (tiles %d..%d)" % (len(new), first, first + len(new) - 1))
    unknown = sorted({mts[m][2] for _, m in new if mts[m][2] not in COLLISION})
    if unknown:
        print("no collision yet for GDA types:", ", ".join(unknown))


if __name__ == "__main__":
    main()
