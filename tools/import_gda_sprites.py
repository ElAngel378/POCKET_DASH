"""Adds GDA's sprite objects that have no match of ours (the official levels' spikes, outline saws,
slab and decorations placed off the grid) to the object catalog as background pieces, with GDA's
art as a placeholder: the Game Boy has 40 sprites (10 a line) and most of these are big or many.

A GDA sprite object sits anywhere and may be flipped. Each one is drawn at the 16 px grid, and at
the 8 px offsets and flips the official levels use (VARIANTS), and cut into 16x16 cells: a variant
is a block of catalog objects laid out in objects.png as it looks, so Tiled can stamp it in one go
(select the block in the tileset). Collision per cell, as GDA's (src/sprite_routines.c obj_hitbox):

  hazards     a deadly rectangle or circle, checked against the player's box (src/player.c
              shape_hazard), not at the spike probe points (a 2 px spike core falls between them)
  slab        BASIC_SLAB_OBJ: the top half of a GDA metatile (its "metatile ID appearance", 43 a
              slab by default), solid where that metatile is
  decorations none

The shapes are catalog properties ("shape": kind,a,b,c,d) with collision ids from COL_SHAPE_BASE;
include/col_shapes.h (their table) is written from the whole catalog, so running this again keeps
the ids. The 3 pulsing decorations (circle, heart, square) are sprites: not here.

    python tools/import_gda_sprites.py
"""
import collections
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda  # noqa: E402
import gda_match  # noqa: E402
from import_gda_pieces import art as metatile_art  # noqa: E402
from objects_catalog import ART, COLUMNS, TSX, load_catalog, write_preview  # noqa: E402

SHAPES_H = ROOT / "include" / "col_shapes.h"
COL_SHAPE_BASE = 0x80
SHAPE_SOLID, SHAPE_DEADLY, SHAPE_CIRCLE = 0, 1, 2
SPRITES = {"PULSING_CIRCUNFERENCE", "PULSING_HEARTH", "PULSING_SQUARE"}   # sprites, not here
SKIP = {"SPEED_PORTAL_HALF"}   # only in GDA's ToE (not RobTop's)
SLAB = "BASIC_SLAB_OBJ"
SLAB_DEFAULT_MT = 43
# GDA metatile collision -> the solid part of the metatile (x0, x1, y0, y1)
SLAB_SOLID = {"COL_FULL": (0, 16, 0, 16), "COL_SLAB_TOP": (0, 16, 0, 8), "COL_FLOOR_CEIL": (0, 16, 0, 16)}
DECO_MIN = 10   # a decoration gets an offset / flip variant when the levels use it this often
HAZARD_MIN = 3  # a hazard / the slab: this often (room for 128 collision shapes)


def our_name(t):
    return t.replace("ONE_POINT_SIX_", "1.6_")


def hitboxes():
    """[(kind, values)] by ObjTypes value from src/sprite_routines.c obj_hitbox: ("rect", (w, h,
    xoff, yoff)) or ("circle", (r, cx, cy)) relative to the object's top left"""
    src = (gda.GDA / "src" / "sprite_routines.c").read_text(encoding="latin1")
    body = re.search(r"obj_hitbox\[\]\[6\] = \{(.*?)\n\};", src, re.S).group(1)
    out = []
    for m in re.finditer(r'Object_Hitbox_(Rectangle|Circle)\("[^"]*"\s*,\s*([^)]*)\)', body):
        v = [x.strip() for x in m.group(2).split(",")]
        if m.group(1) == "Rectangle":
            w, h, xo, yo = (int(x) for x in v[:4])
            out.append(("rect", (w, h, xo, yo)) if w and h else (None, None))
        else:
            r, xo, yo, cx, cy = (int(x) for x in v[1:6])
            out.append(("circle", (r, cx + xo, cy + yo)) if r else (None, None))
    return out


def usage(types):
    """{type: Counter((x8, y8, hflip, vflip))} of the official levels' placements, offsets rounded
    to 0 / 8; and {type: Counter(appearance metatile)} for the slab"""
    use, looks = collections.defaultdict(collections.Counter), collections.defaultdict(collections.Counter)
    for name in gda_match.SHARED:
        _, _, _, objs = gda.read_level(name)
        for o in objs:
            if o["gid"] < gda.SPRITE_GID:
                continue
            t = types[o["gid"] - gda.SPRITE_GID]
            p = o["props"]
            ox = (int(round((o["x"] % 16) / 8)) * 8) % 16
            oy = (int(round((o["y"] % 16) / 8)) * 8) % 16
            use[t][(ox, oy, p.get("hflip") == "true", p.get("vflip") == "true")] += 1
            if t == SLAB:
                looks[t][int(p.get("metatile ID appearance", SLAB_DEFAULT_MT))] += 1
    return use, looks


def gb_colours(img, hazard):
    """GDA colour indices -> ours (0 clear, 3 outline / bright, 1-2 fills). Hazards: GDA's outline
    11 -> 3, its edge shades 9 / 10 and the bright half of the fill -> 2, the dark fill -> 1.
    Decorations (background, palette 2: 3 is the accent): their brightest index -> 3, the rest 1."""
    used = sorted(set(img.getdata()) - {0})
    top = used[-1] if used else 0

    def f(c):
        if not c:
            return 0
        if hazard:
            return 3 if c >= 11 else 2 if c >= 9 or c >= 5 else 1
        return 3 if c == top and len(used) > 1 else 1
    return img.point([f(c) for c in range(256)])


def flip(img, ox, oy, hf, vf):
    """the art (and where the object's top left is in it) mirrored about the object's 16x16 cell"""
    if hf:
        img, ox = img.transpose(Image.FLIP_LEFT_RIGHT), img.width - ox - 16
    if vf:
        img, oy = img.transpose(Image.FLIP_TOP_BOTTOM), img.height - oy - 16
    return img, ox, oy


def variant_cells(art, ox, oy, dx, dy, shape):
    """{(col, row): (16x16 art, shape in that cell or None)} of a variant: the art with the
    object's top left at (dx, dy) of cell (0, 0); shape in object coordinates"""
    x0, y0 = dx - ox, dy - oy
    xs, ys = [x0, x0 + art.width], [y0, y0 + art.height]
    if shape:
        k, v = shape
        if k == "rect":
            xs += [dx + v[2], dx + v[2] + v[0]]
            ys += [dy + v[3], dy + v[3] + v[1]]
        else:
            xs += [dx + v[1] - v[0], dx + v[1] + v[0]]
            ys += [dy + v[2] - v[0], dy + v[2] + v[0]]
    out = {}
    for cy in range(min(ys) // 16, (max(ys) - 1) // 16 + 1):
        for cx in range(min(xs) // 16, (max(xs) - 1) // 16 + 1):
            tile = Image.new("L", (16, 16), 0)
            tile.paste(art, (x0 - cx * 16, y0 - cy * 16))
            s = None
            if shape:
                k, v = shape
                bx, by = cx * 16, cy * 16
                if k in ("rect", "solid"):
                    w, h, xo, yo = v
                    rx0, rx1 = max(dx + xo - bx, 0), min(dx + xo + w - bx, 16)
                    ry0, ry1 = max(dy + yo - by, 0), min(dy + yo + h - by, 16)
                    if rx0 < rx1 and ry0 < ry1:
                        s = (SHAPE_SOLID if k == "solid" else SHAPE_DEADLY, rx0, rx1, ry0, ry1)
                else:
                    r, ccx, ccy = v
                    ccx, ccy = dx + ccx - bx, dy + ccy - by
                    if ccx + r > 0 and ccx - r < 16 and ccy + r > 0 and ccy - r < 16:
                        s = (SHAPE_CIRCLE, ccx, ccy, r, 0)
            if tile.getbbox() or s:
                out[(cx, cy)] = (tile, s)
    # cell coordinates from the block's top left (and where that is, in cells from the object's)
    mx, my = min(c for c, _ in out), min(r for _, r in out)
    return {(c - mx, r - my): v for (c, r), v in out.items()}, (mx, my)


def suffix(dx, dy, hf, vf):
    s = ("_X8" if dx else "") + ("_Y8" if dy else "")
    return s + ("_HF" if hf else "") + ("_VF" if vf else "")


def build_blocks(todo, override=None):
    """[(name, palette, gda type, {(c, r): (art, shape)})] of the GDA types todo, their variants;
    override: {type: (art in our colours, origin x, origin y)} drawn instead of GDA's art"""
    types = gda.sprite_types()
    boxes = hitboxes()
    use, looks = usage(types)
    mts = gda.metatiles()
    blocks = []   # (name, palette, gda type, {(c, r): (art, shape)})
    for t in todo:
        hb = boxes[types.index(t)] if types.index(t) < len(boxes) else (None, None)
        if t == SLAB:
            kinds = [(mid, "" if mid == SLAB_DEFAULT_MT else "_" + mts[mid][0].replace("WAVY_SLAB_", "WAVY_")
                      .replace("_TOP", "")) for mid in looks[t]]
        else:
            kinds = [(None, "")]
        hazard = hb[0] is not None
        for mid, kname in kinds:
            if mid is not None:
                a = metatile_art(mid)
                img = Image.new("L", (16, 8), 0)
                for y in range(8):
                    for x in range(16):
                        img.putpixel((x, y), a[y][x])
                ox = oy = 0
                solid = SLAB_SOLID.get(mts[mid][2], (0, 16, 0, 8))
                shape = ("solid", (solid[1] - solid[0], min(solid[3], 8) - solid[2], solid[0], solid[2]))
                pal = 0
            else:
                if override and t in override:
                    img, ox, oy = override[t]
                else:
                    r = gda.sprite_art(types.index(t))
                    if not r:
                        print("no art:", t)
                        continue
                    img, ox, oy = r
                    img = gb_colours(img, hazard)
                shape = hb if hazard else None
                pal = 0 if hazard else 2
            counts = use[t]
            vs = {(0, 0, False, False)}
            for (dx, dy, hf, vf), n in counts.items():
                if (hazard or mid is not None) and n >= HAZARD_MIN or n >= DECO_MIN:
                    vs.add((dx, dy, hf, vf))
                elif not hazard:
                    if sum(m for (_, _, h2, v2), m in counts.items() if (h2, v2) == (hf, vf)) >= DECO_MIN:
                        vs.add((0, 0, hf, vf))
            for dx, dy, hf, vf in sorted(vs):
                im, fx, fy = flip(img, ox, oy, hf, vf)
                sh = shape
                if sh and (hf or vf):
                    k, v = sh
                    if k in ("rect", "solid"):
                        w, h, xo, yo = v
                        sh = (k, (w, h, 16 - xo - w if hf else xo, 16 - yo - h if vf else yo))
                    else:
                        rr, ccx, ccy = v
                        sh = (k, (rr, 16 - ccx if hf else ccx, 16 - ccy if vf else ccy))
                cells, corner = variant_cells(im, fx, fy, dx, dy, sh)
                blocks.append((our_name(t) + kname + suffix(dx, dy, hf, vf), pal, t, cells, corner))

    return blocks


def refresh_art(override):
    """Redraws the blocks of the GDA types in override (art: see build_blocks) into the catalog
    cells they already have (same ids, collision kept). Returns the cells it could not place."""
    cat = load_catalog()
    sheet = Image.open(ART).convert("L")
    missing = []
    corners = {b[0]: b[4] for b in build_blocks(sorted(override))}   # GDA's art: the cells' names
    for name, pal, t, cells, (mx, my) in build_blocks(sorted(override), override):
        ox, oy = corners[name][0] - mx, corners[name][1] - my
        many = any(n.startswith(name + "_R") for n in cat.by_name)
        old = {n: tid for n, tid in cat.by_name.items() if n == name or re.fullmatch(re.escape(name) + r"_R\d+C\d+", n)}
        done = set()
        for (c, r), (tile, s) in cells.items():
            c, r = c - ox, r - oy
            n = name + ("_R%dC%d" % (r, c) if many else "")
            tid = cat.by_name.get(n)
            if tid is None:
                missing.append(n)
                continue
            done.add(n)
            px = tile.load()
            for yy in range(16):
                for xx in range(16):
                    sheet.putpixel(((tid % COLUMNS) * 16 + xx, (tid // COLUMNS) * 16 + yy), 255 - 85 * px[xx, yy])
        for n, tid in old.items():   # cells the new art leaves empty
            if n not in done:
                for yy in range(16):
                    for xx in range(16):
                        sheet.putpixel(((tid % COLUMNS) * 16 + xx, (tid // COLUMNS) * 16 + yy), 255)
    sheet.save(ART)
    write_preview()
    return missing


def main():
    cat = load_catalog()
    types = gda.sprite_types()
    boxes = hitboxes()
    use, looks = usage(types)
    mts = gda.metatiles()
    ours = set()
    for f in ("gda_sprites.json", "gda_ours.json"):
        import json
        for v in json.loads((ROOT / "levels" / "objects" / f).read_text()).values():
            for k, p in gda_match.parse_label(v.get("gda") or ""):
                if k and k.startswith("SP") and p:
                    ours.add(p.split("/")[0].split("[")[0])
    todo = sorted(t for t in use if t not in ours and t not in SPRITES and t not in SKIP
                  and not any(o.get("gda_name") == t for o in cat.objects.values()))

    blocks = build_blocks(todo)

    # shapes already in the catalog keep their ids
    shapes = {}
    for o in cat.objects.values():
        if "shape" in o:
            shapes[tuple(int(x) for x in o["shape"].split(","))] = o["collision"]

    def shape_id(s):
        if s not in shapes:
            shapes[s] = COL_SHAPE_BASE + len(shapes)
            if shapes[s] > 0xFF:
                raise SystemExit("more than %d collision shapes" % (0x100 - COL_SHAPE_BASE))
        return shapes[s]

    # lay the blocks out in objects.png: shelves of 16 columns from the first free row
    first_row = (max(cat.objects) // COLUMNS) + 1
    placed, x, y, shelf_h = [], 0, first_row, 0
    for name, pal, t, cells, _ in sorted(blocks, key=lambda b: (-max(r for _, r in b[3]), b[0])):
        w = max(c for c, _ in cells) + 1
        h = max(r for _, r in cells) + 1
        if x + w > COLUMNS:
            x, y, shelf_h = 0, y + shelf_h, 0
        placed.append((name, pal, t, cells, x, y))
        x += w
        shelf_h = max(shelf_h, h)
    rows = y + shelf_h
    sheet = Image.open(ART).convert("L")
    if rows * 16 > sheet.height:
        grown = Image.new("L", (sheet.width, rows * 16), 255)
        grown.paste(sheet, (0, 0))
        sheet = grown
    tiles, n = [], 0
    for name, pal, t, cells, bx, by in placed:
        many = len(cells) > 1
        for (c, r), (tile, s) in sorted(cells.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            tid = (by + r) * COLUMNS + bx + c
            px = tile.load()
            for yy in range(16):
                for xx in range(16):
                    sheet.putpixel(((bx + c) * 16 + xx, (by + r) * 16 + yy), 255 - 85 * px[xx, yy])
            col = shape_id(s) if s else 0
            tiles.append(
                ' <tile id="%d">\n  <properties>\n'
                '   <property name="name" value="%s"/>\n'
                '   <property name="collision" type="int" value="%d"/>\n'
                '   <property name="palette" type="int" value="%d"/>\n'
                '   <property name="famidash_id" type="int" value="-1"/>\n'
                '   <property name="dmg_art" type="int" value="-1"/>\n'
                '   <property name="gda_name" value="%s"/>\n'
                '   <property name="gda_collision" value="SPRITE"/>\n%s'
                '  </properties>\n </tile>\n' % (
                    tid, name + ("_R%dC%d" % (r, c) if many else ""), col, pal, t,
                    '   <property name="shape" value="%s"/>\n' % ",".join(map(str, s)) if s else ""))
            n += 1
    sheet.save(ART)
    tsx = TSX.read_text(encoding="utf-8")
    TSX.write_text(tsx.replace("</tileset>", "".join(tiles) + "</tileset>"), encoding="utf-8")
    write_preview()
    write_shapes(shapes)
    print("added %d objects in %d blocks (rows %d..%d), %d collision shapes" % (
        n, len(placed), first_row, rows - 1, len(shapes)))


def write_shapes(shapes):
    rows = sorted(shapes.items(), key=lambda kv: kv[1])
    lines = ["    { %d, %d, %d, %d, %d },   // 0x%02X" % (s + (i,)) for s, i in rows]
    SHAPES_H.write_text(
        "// Collision shapes of the off-grid GDA pieces (collision ids COL_SHAPE_BASE..): written by\n"
        "// tools/import_gda_sprites.py from the catalog's shape properties. Per id: kind, then\n"
        "//   SHAPE_SOLID / SHAPE_DEADLY: x0, x1, y0, y1 (x0 <= x < x1 in the metatile)\n"
        "//   SHAPE_CIRCLE (deadly): centre x, y (from the metatile's top left, may be outside it), r\n"
        "#ifndef COL_SHAPES_H\n#define COL_SHAPES_H\n\n"
        "#define SHAPE_SOLID  0\n#define SHAPE_DEADLY 1\n#define SHAPE_CIRCLE 2\n"
        "#define COL_SHAPE_COUNT %d\n\n"
        "static const int8_t col_shapes[COL_SHAPE_COUNT][5] = {\n%s\n};\n\n#endif\n" % (len(rows), "\n".join(lines)))


if __name__ == "__main__":
    main()
