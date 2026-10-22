"""One-time import of the 1.6 block and slab objects into the object catalog (run 2026-10-22).

The art is levels/objects/source/1_6 objects.chr (Game Boy 2bpp tiles, 16x16 pieces as 2x2
tiles); it holds one orientation of each piece, the others are its flips. Every piece is matched
against Geometry Dash Advance's metatiles (../geometry_dash_advance: src/metatiles.c,
graphics/tileset.chr / tileset.pal): each GDA 1.6 block / slab metatile becomes a catalog object
with the best matching piece (flipped as needed), GDA's name, and GDA's collision mapped to ours
(include/collision.h). The objects keep GDA's name in their gda_name property, for converting GDA
levels later.

    python tools/import_gda_16.py
"""
import re
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
GDA = ROOT.parent / "geometry_dash_advance"
CATALOG = ROOT / "levels" / "objects"
sys.path.insert(0, str(ROOT / "tools"))
from objects_catalog import TSX, ART, load_catalog, write_preview  # noqa: E402

SOURCE = CATALOG / "source" / "1_6 objects.chr"
SOURCE_B = CATALOG / "source" / "1p6blocks.chr"
# Objects from 1p6blocks.chr GDA has as metatiles too: (GDA name, piece, h flip, v flip, collision)
EXTRA = [("1.6_WAVE_DECO_MIDDLE", "2.0", 0, 0, 0), ("1.6_WAVE_DECO_MIDDLE_UD", "2.0", 0, 1, 0)]

# GDA collision -> ours
COL_ALL, COL_TOP, COL_BOTTOM, COL_QUAD_BASE, COL_BOX_BASE = 7, 5, 6, 0x20, 0x40
COLLISION = {
    "COL_NONE": 0, "COL_FULL": COL_ALL,
    "COL_SLAB_TOP": COL_TOP, "COL_SLAB_BOTTOM": COL_BOTTOM,
    "COL_SLAB_LEFT": COL_QUAD_BASE + 0, "COL_SLAB_RIGHT": COL_QUAD_BASE + 1,
    "COL_SLAB_MED_TOP": COL_BOX_BASE + 0, "COL_SLAB_MED_BOTTOM": COL_BOX_BASE + 1,
    "COL_SLAB_MED_LEFT": COL_BOX_BASE + 2, "COL_SLAB_MED_RIGHT": COL_BOX_BASE + 3,
    "COL_SLAB_TINY_TOP": COL_BOX_BASE + 4, "COL_SLAB_TINY_BOTTOM": COL_BOX_BASE + 5,
    "COL_SLAB_TINY_LEFT": COL_BOX_BASE + 6, "COL_SLAB_TINY_RIGHT": COL_BOX_BASE + 7,
}
# Pieces of 1_6 objects.chr GDA has outside its 1.6 set: (GDA name, piece, h flip, v flip, collision)
COL_BREAKABLE = 0x16
EXTRA_A = [("OUTLINE_BLOCK", "4.0", 0, 0, COL_ALL),
           ("OUTLINE_SLAB_TOP", "4.1", 0, 0, COL_TOP), ("OUTLINE_SLAB_BOTTOM", "4.1", 0, 1, COL_BOTTOM),
           ("OUTLINE_SLAB_RIGHT", "5.1", 0, 0, COL_QUAD_BASE + 1), ("OUTLINE_SLAB_LEFT", "5.1", 1, 0, COL_QUAD_BASE + 0),
           ("BREAKABLE_BRICK", "6.0", 0, 0, COL_BREAKABLE), ("BREAKABLE_BRICK_ROT", "6.1", 0, 0, COL_BREAKABLE)]
# Which GDA metatiles: the 1.6 block set and its slabs (not slopes, spikes, Electroman pieces)
WANTED = re.compile(r"1\.6_(SIDE|CORNER|INS_CRN|DBL|BLOCK|FILLER|SID_INS|SLAB)\w*")
MIN_SCORE = {"block": 220, "slab": 165}   # matching pixels of 256
# The slabs' art was redrawn, so the best match alone mixes them up: their pieces, by name
PIECES = [
    (r"1\.6_SLAB_BLOCK_(TOP|BOT)", ["2.4"]), (r"1\.6_SLAB_BLOCK_(LFT|RGT)", ["3.4"]),
    (r"1\.6_SLAB_SIDE_(TOP|BOT)", ["2.6"]), (r"1\.6_SLAB_SIDE_(LFT|RGT)", ["3.6"]),
    (r"1\.6_SLAB_SIDE_H_\w\w", ["2.7"]), (r"1\.6_SLAB_SIDE_V_\w\w", ["3.7"]),
    (r"1\.6_SLAB_SIDE_H_\w\w_ALT", ["4.2"]), (r"1\.6_SLAB_SIDE_V_\w\w_ALT", ["5.2"]),
    (r"1\.6_SLAB_CORNER_H_\w\w", ["4.5"]), (r"1\.6_SLAB_CORNER_V_\w\w", ["5.5"]),
]


def gda_metatiles():
    src = (GDA / "src" / "metatiles.c").read_text()
    consts = {"H": 1, "V": 1}
    out = []
    for m in re.finditer(r'Metatile\("([^"]*)",\s*' + r",\s*".join([r"TILE\(([^)]*)\)"] * 4) + r",\s*(\w+)\)", src):
        tiles = []
        for t in m.groups()[1:5]:
            a = [x.strip() for x in t.split(",")]
            tiles.append((int(a[0], 0), consts.get(a[2], 0) if not a[2].isdigit() else int(a[2]),
                          consts.get(a[3], 0) if not a[3].isdigit() else int(a[3])))
        out.append((m.group(1), tiles, m.group(6)))
    return out


def gda_art(tiles, chr_, pal):
    """16x16 GDA metatile -> our colour indices (outline white -> 3, darkest -> 2, dark -> 1,
    light and background -> 0)"""
    to_index = {(255, 255, 255): 3, (8, 16, 57): 2, (16, 24, 99): 1}
    px = [[0] * 16 for _ in range(16)]
    for q, (tid, h, v) in enumerate(tiles):
        b = chr_[tid * 32:tid * 32 + 32]
        for y in range(8):
            for x in range(8):
                n = (b[y * 4 + x // 2] >> (4 * (x & 1))) & 15
                c = tuple(pal[n * 3:n * 3 + 3]) if n else None
                px[(q >> 1) * 8 + (7 - y if v else y)][(q & 1) * 8 + (7 - x if h else x)] = to_index.get(c, 0)
    return px


def pieces(path):
    d = path.read_bytes()
    n = len(d) // 16
    out = {}
    for r in range(16):
        for c in range(8):
            ts = [2 * r * 16 + 2 * c, 2 * r * 16 + 2 * c + 1, (2 * r + 1) * 16 + 2 * c, (2 * r + 1) * 16 + 2 * c + 1]
            if not any(t < n and any(d[t * 16:t * 16 + 16]) for t in ts):
                continue
            px = [[0] * 16 for _ in range(16)]
            for q, t in enumerate(ts):
                b = d[t * 16:t * 16 + 16]
                for y in range(8):
                    for x in range(8):
                        px[(q >> 1) * 8 + y][(q & 1) * 8 + x] = ((b[2 * y] >> (7 - x)) & 1) | (((b[2 * y + 1] >> (7 - x)) & 1) << 1)
            out["%d.%d" % (r, c)] = px
    return out


def flip(a, h, v):
    return [[a[15 - y if v else y][15 - x if h else x] for x in range(16)] for y in range(16)]


def main():
    cat = load_catalog()
    have = {o.get("gda_name") for o in cat.objects.values()}   # run again: only adds the objects not there yet
    chr_ = (GDA / "graphics" / "tileset.chr").read_bytes()
    pal = (GDA / "graphics" / "tileset.pal").read_bytes()   # palette bank 0: 16 colours
    ours = pieces(SOURCE)
    new, used = [], set()
    for name, tiles, col in gda_metatiles():
        if not WANTED.fullmatch(name) or col not in COLLISION:
            continue
        g = gda_art(tiles, chr_, pal)
        allowed = next((p for r, p in PIECES if re.fullmatch(r, name)), list(ours))
        best = max(((sum(f[y][x] == g[y][x] for y in range(16) for x in range(16)), -(h + v), k, h, v)
                    for k in allowed for h in (0, 1) for v in (0, 1) for f in [flip(ours[k], h, v)]))
        score, _, k, h, v = best
        if score < MIN_SCORE["slab" if "SLAB" in name else "block"]:
            print("  no piece for %-28s (best %s, %d)" % (name, k, score))
            continue
        used.add(k)
        new.append((name, COLLISION[col], flip(ours[k], h, v), "%s%s%s" % (k, " H" if h else "", " V" if v else "")))
    for k in sorted(set(ours) - used):
        print("  piece %s is not used" % k)
    ours_b = pieces(SOURCE_B)
    for name, k, h, v, col in EXTRA:
        new.append((name, col, flip(ours_b[k], h, v), "1p6blocks %s%s%s" % (k, " H" if h else "", " V" if v else "")))
    for name, k, h, v, col in EXTRA_A:
        new.append((name, col, flip(ours[k], h, v), "%s%s%s" % (k, " H" if h else "", " V" if v else "")))
    new = [n for n in new if n[0] not in have]
    if not new:
        raise SystemExit("nothing new to add")

    append_objects(cat, [dict(name=n, collision=c, art=a, src=src, gda_name=n) for n, c, a, src in new])


def append_objects(cat, new):
    """Appends objects to the catalog: their art to objects.png (after the last object), their
    tiles to objects.tsx. new: dicts of name, collision, art (16 rows of 16 colour indices), src
    (printed), and optionally palette, famidash_id, dmg_art, gda_name."""
    first = max(cat.objects) + 1
    art = Image.open(ART).convert("L")
    rows = (first + len(new) + 15) // 16
    img = Image.new("L", (256, max(rows * 16, art.height)), 255)
    img.paste(art, (0, 0))
    px = img.load()
    tiles = []
    for i, o in enumerate(new):
        tid = first + i
        for y in range(16):
            for x in range(16):
                px[(tid % 16) * 16 + x, (tid // 16) * 16 + y] = 255 - 85 * o["art"][y][x]
        tiles += [' <tile id="%d">' % tid, "  <properties>",
                  '   <property name="name" value="%s"/>' % o["name"],
                  '   <property name="collision" type="int" value="%d"/>' % o["collision"],
                  '   <property name="palette" type="int" value="%d"/>' % o.get("palette", 0),
                  '   <property name="famidash_id" type="int" value="%d"/>' % o.get("famidash_id", -1),
                  '   <property name="dmg_art" type="int" value="%d"/>' % o.get("dmg_art", -1)]
        if o.get("gda_name"):
            tiles.append('   <property name="gda_name" value="%s"/>' % o["gda_name"])
        tiles += ["  </properties>", " </tile>"]
        print("  %3d %-28s col %3d  from %s" % (tid, o["name"], o["collision"], o["src"]))
    img.save(ART)
    tsx = TSX.read_text(encoding="utf-8")
    at = tsx.index(" <wangsets>") if " <wangsets>" in tsx else tsx.index("</tileset>")
    TSX.write_text(tsx[:at] + "\n".join(tiles) + "\n" + tsx[at:], encoding="utf-8")
    write_preview()
    print("%d objects added (tiles %d..%d)" % (len(new), first, first + len(new) - 1))

if __name__ == "__main__":
    main()
