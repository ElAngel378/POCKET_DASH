"""One-time generator of the object catalog from the Famidash based data (run 2026-10-22).

It needs tools/build_bg_tiles.py as it was before the catalog took over (commit 8c739e1: the
LAYOUT it recorded per level); kept for reference. levels/objects/ is now edited in Tiled and
image editors, not regenerated.

Every object a level can show (a Famidash metatile as it looks in that level: its CGB art,
DMG art, CGB palette and collision) becomes one tile of a Tiled tileset:

    levels/objects/objects.png   16 tiles (256 px) wide, one 16x16 object per tile; the 4 greys
                                 are colour indices (white 0 .. black 3), the CGB art. After the
                                 objects: DMG-only art tiles (see dmg_art).
    levels/objects/objects.tsx   the tileset, with per tile properties:
        name        unique object name (Famidash metatile name, + a variant suffix)
        collision   collision type (include/collision.h COL_*)
        palette     CGB background palette
        famidash_id the Famidash metatile id (the engine still knows some of these: saw parts,
                    ground looks, pit spikes; see docs/PLAN_POST_1.5_OBJECTS.md)
        dmg_art     tile id of its DMG art when it differs from the CGB art (-1: the same)
        dmg_only    true on those DMG art tiles (not objects)
    levels/objects/famidash_ids.json  per level: Famidash id -> object tile id (for the TMX
                                 converter)

A Famidash metatile looks different in some levels (block set B, spike sets B / C, the DMG fake
spikes, Xstep's diamond on the big saw centre id): each look is its own object. Unused Famidash
metatiles get an object with the base sheet's art, so Tiled still offers them.
"""
import json
import re
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_bg_tiles as bgt  # noqa: E402

OUT = ROOT / "levels" / "objects"
COLS = 16


def rows_of(data):
    """16 bytes of 2bpp tile data -> 8 rows of 8 colour indices"""
    return tuple(tuple(((data[2 * y] >> (7 - x)) & 1) | (((data[2 * y + 1] >> (7 - x)) & 1) << 1)
                       for x in range(8)) for y in range(8))


def level_art(lay, g):
    """(cgb, dmg) art of every metatile the level uses: 4 quarters of 8x8 colour indices"""
    base, extra = g["base"], g["extra"]
    cgb_vram = list(base)
    for bank, dst, count, first in lay["cgb_loads"]:
        if bank & 1:
            continue                       # VRAM bank 1: the saws (their art: cgb_saws)
        for j in range(count):
            if dst + j < len(cgb_vram):
                cgb_vram[dst + j] = bytes(extra[(first + j) * 32:(first + j) * 32 + 16])
    dmg_vram = dict(enumerate(base))
    for bank, dst, count, first in lay["dmg_loads"]:
        for j in range(count):
            dmg_vram[dst + j] = bytes(extra[(first + j) * 32:(first + j) * 32 + 16])
    for dst, src in lay["moves"]:
        dmg_vram[dst] = dmg_vram[src]
    for dst, src in lay["fakes"]:
        dmg_vram[dst] = bgt.fake_tile(dmg_vram[src])
    saw0 = g["cgb_saws"][0]
    art = {}
    for m in lay["mts"]:
        cq, dq = [], []
        for q in range(4):
            saw = lay["saw_mts"].get(m, (None,) * 4)[q]
            if saw is not None:
                cq.append(tuple(tuple(r) for r in saw0[saw]))
            else:
                cq.append(rows_of(cgb_vram[lay["cgb_mt"][m * 4 + q]]))
            dq.append(rows_of(dmg_vram[lay["dmg_mt"][m * 4 + q]]))
        art[m] = (tuple(cq), tuple(dq))
    return art


def default_art(m, g):
    q = tuple(rows_of(g["base"][t]) for t in g["sheet_mt"][m])
    return q, q


def label(lay, m, default):
    if default:
        return ""
    sets = lay["sets"]
    if m == bgt.SAW_CENTER_MT and not (lay["mts"] & bgt.BIG_SAW_PARTS):
        return "_DIAMOND"
    for key, tag in (("blocks", "B"), ("spikes", "S")):
        if sets.get(key):
            return "_%s%s" % (tag, sets[key])
    return "_V"


def main():
    print_ = print
    bgt.main.__globals__["print"] = lambda *a, **k: None
    bgt.main()
    bgt.main.__globals__["print"] = print_
    g = bgt.LAYOUT["_"]
    levels = {n: l for n, l in bgt.LAYOUT.items() if n != "_"}
    names = {int(v): k for k, v in re.findall(r"#define FAMIDASH_MT_(\w+) (\d+)",
                                              (ROOT / "include" / "famidash_metatiles.c").read_text())}

    # objects: key (famidash id, cgb art, dmg art) -> index; the default look of each id first
    objects, index = [], {}

    def add(m, cgb, dmg, suffix):
        key = (m, cgb, dmg)
        if key not in index:
            name = names[m] + suffix
            while any(o["name"] == name for o in objects):
                name += "_"
            index[key] = len(objects)
            objects.append(dict(name=name, m=m, cgb=cgb, dmg=dmg))
        return index[key]

    # the look most levels give an id is its default (no suffix)
    looks = {}
    arts = {n: level_art(l, g) for n, l in levels.items()}
    for n, art in arts.items():
        for m, a in art.items():
            looks.setdefault(m, {}).setdefault(a, []).append(n)
    for m in range(256):
        if m in looks:
            best = max(looks[m].items(), key=lambda kv: len(kv[1]))[0]
            add(m, best[0], best[1], "")
        else:
            add(m, *default_art(m, g), "")
    famidash_ids = {}
    for n, art in arts.items():
        famidash_ids[n] = {}
        for m, (cgb, dmg) in sorted(art.items()):
            default = (m, cgb, dmg) in index and objects[index[(m, cgb, dmg)]]["name"] == names[m]
            famidash_ids[n][m] = add(m, cgb, dmg, label(levels[n], m, default))

    # DMG art tiles (after the objects) where it differs from the CGB art
    dmg_tiles = []
    for o in objects:
        o["dmg_art"] = -1
        if o["dmg"] != o["cgb"]:
            o["dmg_art"] = len(objects) + len(dmg_tiles)
            dmg_tiles.append(o["dmg"])

    tiles = [o["cgb"] for o in objects] + dmg_tiles
    rows = (len(tiles) + COLS - 1) // COLS
    img = Image.new("L", (COLS * 16, rows * 16), 255)
    px = img.load()
    for i, quarters in enumerate(tiles):
        ox, oy = (i % COLS) * 16, (i // COLS) * 16
        for q, rows8 in enumerate(quarters):
            qx, qy = (q & 1) * 8, (q >> 1) * 8
            for y in range(8):
                for x in range(8):
                    px[ox + qx + x, oy + qy + y] = 255 - 85 * rows8[y][x]
    OUT.mkdir(parents=True, exist_ok=True)
    img.save(OUT / "objects.png")

    X = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<tileset version="1.10" tiledversion="1.11.2" name="objects" tilewidth="16" tileheight="16" '
         'tilecount="%d" columns="%d">' % (len(tiles), COLS),
         ' <image source="objects.png" width="%d" height="%d"/>' % (img.width, img.height)]
    for i, o in enumerate(objects):
        X += [' <tile id="%d">' % i, '  <properties>',
              '   <property name="name" value="%s"/>' % o["name"],
              '   <property name="collision" type="int" value="%d"/>' % g["mt_col"][o["m"]],
              '   <property name="palette" type="int" value="%d"/>' % g["mt_pal"][o["m"]],
              '   <property name="famidash_id" type="int" value="%d"/>' % o["m"],
              '   <property name="dmg_art" type="int" value="%d"/>' % o["dmg_art"],
              '  </properties>', ' </tile>']
    for i in range(len(objects), len(tiles)):
        X += [' <tile id="%d">' % i, '  <properties>',
              '   <property name="dmg_only" type="bool" value="true"/>', '  </properties>', ' </tile>']
    X += ['</tileset>', '']
    (OUT / "objects.tsx").write_text("\n".join(X), encoding="utf-8")
    (OUT / "famidash_ids.json").write_text(json.dumps(famidash_ids, indent=1), encoding="utf-8")
    variants = len(objects) - 256
    print("%d objects (%d level variants), %d DMG-only art tiles" % (len(objects), variants, len(dmg_tiles)))


if __name__ == "__main__":
    main()
