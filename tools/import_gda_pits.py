"""GDA's own pits in our levels: every pit cell (and the cells its spikes reach into) drawn as GDA
draws that spot of the level: its black pit metatiles in front of its BIG / MEDIUM / SMALL / TINY
_BG_SPIKES sprites, cut into 16x16 catalog objects. Also deletes Famidash's ground spike
decoration sprites (46..49) from those levels.

    python tools/import_gda_pits.py [level ...]      (default: the 1.0 levels, LEVELS)

Our maps are GDA's maps cell for cell (both from the same GD levels, bottom-aligned); the offset
is found per level by matching the pits (best of a few cells). Per cell of ours:
    - a pit piece (PITSPIKES, the wavy pits, the GDA_PIT_* strips and tips, their tops) where GDA
      draws something: a GDA_PITART_<n> object with GDA's art and the old piece's collision (the
      engine's id-based pit rule, ids 12 / 24, becomes COL_PIT_SPIKES, which is the same rule)
    - an empty cell where GDA's spikes reach: a GDA_PITART_<n> decoration (no collision)
    - else unchanged (a pit GDA has no art for keeps the generic strip)
Colours: GDA's pit black -> 2 (black), its spikes -> 3 (palette 2: the decoration accent, the
cube's colour, as GDA uses the player's). DMG: the old piece's art (decorations blank), so DMG
levels need exactly the tiles they needed before.
A level whose CGB sheet would overflow (159 tiles) gets back its old pieces for the rarest GDA
objects until it fits. Objects are shared between levels (same art + collision + DMG art).
"""
import re
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda  # noqa: E402
from objects_catalog import load_catalog, write_preview  # noqa: E402

ART = ROOT / "levels" / "objects" / "objects.png"
DMG_ART = ROOT / "levels" / "objects" / "objects_dmg.png"
TSX = ROOT / "levels" / "objects" / "objects.tsx"
TMX = ROOT / "levels" / "chr_data" / "tmx"
COLUMNS = 16
CGB_TILES = 159
SPRITE_FIRSTGID = 4097
DECO_GROUND_SPIKES = (46, 47, 48, 49)   # Famidash's ground spike decorations (sprites)
BG_SPIKES = ("BIG_BG_SPIKES", "MEDIUM_BG_SPIKES", "SMALL_BG_SPIKES", "TINY_BG_SPIKES")
COL_PIT_SPIKES = 0x17
PAL_DECO = 2
LEVELS = ["stereomadness", "backontrack", "polargeist", "dryout", "baseafterbase", "cantletgo",
          "jumper", "timemachine"]
# the pieces that are pits (replaced); combinations with blocks / platforms are left alone
PIT_PIECES = ("PITSPIKES", "PITSPIKES_TOP", "BOTTOM_WAVYPIT", "TOP_WAVYPIT")


def is_pit_piece(name):
    return name in PIT_PIECES or name.startswith("GDA_PIT")


def gda_render(level):
    """GDA's pits of a level as colour indices: {(x, y from the bottom): 16x16 'L' image}, and the
    set of its pit cells"""
    root_tilesets = re.findall(r'<tileset firstgid="(\d+)" source="([^"]+)"',
                               (gda.LEVELS / (level + ".tmx")).read_text())
    sp_first = next(int(f) for f, s in root_tilesets if "sprites" in s)
    types = gda.sprite_types()
    wanted = {types.index(n) for n in BG_SPIKES}
    w, h, layers, objs = gda.read_level(level)
    cm = gda.cell_map(layers, w, h)
    names = [n for n, _, _ in gda.metatiles()]
    img = Image.new("L", (w * 16, h * 16), 0)
    for o in objs:
        t = o["gid"] - sp_first
        if t not in wanted:
            continue
        art, ox, oy = gda.sprite_art(t)
        if o["raw_gid"] & 0x80000000:
            art, ox = art.transpose(Image.FLIP_LEFT_RIGHT), art.width - ox - 16
        if o["raw_gid"] & 0x40000000:
            art, oy = art.transpose(Image.FLIP_TOP_BOTTOM), art.height - oy - 16
        mask = art.point(lambda v: 255 if v else 0)
        img.paste(art.point(lambda v: 3 if v else 0), (int(o["x"]) - ox, int(o["y"]) - oy), mask)
    pits = set()
    px = img.load()
    for y in range(h):
        for x in range(w):
            m = cm[y][x]
            if m < 0 or not ("GROUND" in names[m] and "SPIKE" in names[m]):
                continue
            pits.add((x, y - h))
            mi = gda.metatile_image(m).load()
            for yy in range(16):
                for xx in range(16):
                    if mi[xx, yy][3]:
                        px[x * 16 + xx, y * 16 + yy] = 2
    cells = {}
    for y in range(h):
        for x in range(w):
            c = img.crop((x * 16, y * 16, x * 16 + 16, y * 16 + 16))
            if c.getbbox():
                cells[(x, y - h)] = c
    return cells, pits


def read_map(path):
    s = path.read_text(encoding="utf-8")
    w = int(re.search(r'<map [^>]*\swidth="(\d+)"', s).group(1))
    m = re.search(r'(<layer id="1"[^>]*>\s*<data encoding="csv">)(.*?)(</data>)', s, re.S)
    rows = [r.split(",") for r in m.group(2).split("\n")]
    return s, m, w, rows


def cell_tid(v):
    v = v.strip()
    return (int(v) & 0x0FFFFFFF) - 1 if v else None


def quarters(img):
    """the 4 8x8 quarters' 2bpp bytes of a 16x16 index image (as build_bg_tiles cuts them)"""
    px = img.load()
    out = []
    for q in range(4):
        ox, oy = (q & 1) * 8, (q >> 1) * 8
        b = bytearray()
        for y in range(8):
            lo = hi = 0
            for x in range(8):
                v = px[ox + x, oy + y]
                lo, hi = (lo << 1) | (v & 1), (hi << 1) | (v >> 1)
            b += bytes([lo, hi])
        out.append(bytes(b))
    return out


def main():
    levels = sys.argv[1:] or LEVELS
    cat = load_catalog()
    if any(o["name"].startswith("GDA_PITART_") for o in cat.objects.values()):
        raise SystemExit("GDA pit art is already in the catalog")
    sheet = Image.open(ART).convert("L")
    dmg_sheet = Image.open(DMG_ART).convert("L")
    empty_ids = {None, -1, cat.by_name["EMPTY"]}

    def obj_img(tid):
        x, y = (tid % COLUMNS) * 16, (tid // COLUMNS) * 16
        return sheet.crop((x, y, x + 16, y + 16)).point(lambda v: (255 - v + 42) // 85)

    # DMG art: the old piece's (its dmg_art, or its own art copied into objects_dmg.png)
    dmg_rows = [dmg_sheet]
    dmg_next = [dmg_sheet.height // 16 * COLUMNS]
    dmg_of = {}

    def dmg_index(tid):
        if tid is None:   # a decoration: a blank cell of objects_dmg.png
            tid = "blank"
        if tid in dmg_of:
            return dmg_of[tid]
        d = -1 if tid == "blank" else cat.objects[tid].get("dmg_art", -1)
        if d < 0:
            d = dmg_next[0]
            dmg_next[0] += 1
            src = Image.new("L", (16, 16), 255) if tid == "blank" else \
                sheet.crop(((tid % COLUMNS) * 16, (tid // COLUMNS) * 16, (tid % COLUMNS) * 16 + 16, (tid // COLUMNS) * 16 + 16))
            dmg_rows.append((d, src))
        dmg_of[tid] = d
        return d

    new_objs = {}       # (art bytes, collision, dmg) -> new object index
    new_art = []        # index -> (img, collision, dmg)
    plans = {}
    for level in levels:
        path = TMX / (level + ".tmx")
        s, m, w, rows = read_map(path)
        real = [y for y, r in enumerate(rows) if any(v.strip() for v in r)]   # (the csv starts
        h = real[-1] + 1                                                       # with a newline)
        tids = [[cell_tid(v) for v in r] for r in rows]
        g_cells, g_pits = gda_render(level)
        ours = {(x, y - h): tids[y][x] for y in real for x in range(len(tids[y]))
                if tids[y][x] not in empty_ids and is_pit_piece(cat.objects[tids[y][x]]["name"])
                and "TIP" not in cat.objects[tids[y][x]]["name"]}
        best = max((len({(x + dx, y + dy) for x, y in ours} & g_pits), dx, dy)
                   for dx in range(-8, 9) for dy in range(-3, 4))
        _, dx, dy = best
        changes = {}   # (row, col) -> (new object index, old tid)
        for y in real:
            for x in range(len(tids[y])):
                t = tids[y][x]
                g = g_cells.get((x + dx, y - h + dy))
                if g is None:
                    continue
                if t not in empty_ids and is_pit_piece(cat.objects[t]["name"]):
                    col = cat.objects[t]["collision"]
                    if cat.objects[t].get("famidash_id") in (12, 24) or col == COL_PIT_SPIKES:
                        col = COL_PIT_SPIKES
                    key_old = t
                elif t in empty_ids:
                    col, key_old = 0, None
                else:
                    continue
                dmg = dmg_index(key_old if (key_old is None or "TIP" not in cat.objects[key_old]["name"]) else None)
                key = (g.tobytes(), col, dmg)
                if key not in new_objs:
                    new_objs[key] = len(new_art)
                    new_art.append((g, col, dmg))
                changes[(y, x)] = (new_objs[key], t)
        plans[level] = (path, s, m, rows, tids, changes, dx, dy, len(ours), best[0])

    # catalog: the new objects in new rows of objects.png
    first = (max(cat.objects) // COLUMNS + 1) * COLUMNS
    n_rows = (len(new_art) + COLUMNS - 1) // COLUMNS
    grown = Image.new("L", (sheet.width, (first // COLUMNS + n_rows) * 16), 255)
    grown.paste(sheet, (0, 0))
    sheet = grown
    tiles = []
    for i, (img, col, dmg) in enumerate(new_art):
        tid = first + i
        px = img.load()
        for y in range(16):
            for x in range(16):
                sheet.putpixel(((tid % COLUMNS) * 16 + x, (tid // COLUMNS) * 16 + y), 255 - 85 * px[x, y])
        tiles.append(
            ' <tile id="%d">\n  <properties>\n'
            '   <property name="name" value="GDA_PITART_%d"/>\n'
            '   <property name="collision" type="int" value="%d"/>\n'
            '   <property name="palette" type="int" value="%d"/>\n'
            '   <property name="famidash_id" type="int" value="-1"/>\n'
            '   <property name="dmg_art" type="int" value="%d"/>\n'
            '  </properties>\n </tile>\n' % (tid, i, col, PAL_DECO, dmg))
    sheet.save(ART)
    if len(dmg_rows) > 1:
        last = dmg_next[0] - 1
        d = Image.new("L", (dmg_sheet.width, (last // COLUMNS + 1) * 16), 255)
        d.paste(dmg_sheet, (0, 0))
        for idx, src in dmg_rows[1:]:
            d.paste(src, ((idx % COLUMNS) * 16, (idx // COLUMNS) * 16))
        d.save(DMG_ART)
    tsx = TSX.read_text(encoding="utf-8")
    tsx = re.sub(r'tilecount="\d+"', 'tilecount="%d"' % ((first // COLUMNS + n_rows) * COLUMNS), tsx, count=1)
    TSX.write_text(tsx.replace("</tileset>", "".join(tiles) + "</tileset>"), encoding="utf-8")
    cat = load_catalog()

    # the levels, within the CGB tile budget
    for level, (path, s, m, rows, tids, changes, dx, dy, n_ours, n_match) in plans.items():
        def tile_count(applied):
            used = Counter()
            for y, r in enumerate(tids):
                for x, t in enumerate(r):
                    nt = first + applied[(y, x)][0] if (y, x) in applied else t
                    if nt not in empty_ids:
                        used[nt] += 1
            qs = set()
            for t in used:
                qs.update(q for q in quarters(obj_img(t)) if q != bytes(16))
            return len(qs)
        applied = dict(changes)
        usage = Counter(v[0] for v in applied.values())
        reverted = 0
        while tile_count(applied) > CGB_TILES - 2:   # (a little room: saw / build details)
            rare = min(usage, key=lambda k: (usage[k], -k))
            for key in [k for k, v in applied.items() if v[0] == rare]:
                del applied[key]
                reverted += 1
            del usage[rare]
        for (y, x), (i, _) in applied.items():
            v = rows[y][x]
            flags = int(v) & ~0x0FFFFFFF if v.strip() else 0
            rows[y][x] = str(flags | (first + i + 1))
        body = "\n".join(",".join(r) for r in rows)
        s = s[:m.start(2)] + body + s[m.end(2):]
        # Famidash's ground spike decorations out of the SP layer
        sm = re.search(r'(<layer [^>]*name="SP"[^>]*>\s*<data encoding="csv">)(.*?)(</data>)', s, re.S)
        deco = {str(SPRITE_FIRSTGID + d) for d in DECO_GROUND_SPIKES}
        sp_rows = [r.split(",") for r in sm.group(2).split("\n")]
        n_deco = 0
        for r in sp_rows:
            for k, v in enumerate(r):
                if v.strip() in deco:
                    r[k] = "0" + ("\r" if v.endswith("\r") else "")
                    n_deco += 1
        s = s[:sm.start(2)] + "\n".join(",".join(r) for r in sp_rows) + s[sm.end(2):]
        path.write_text(s, encoding="utf-8", newline="")
        print("%s: offset %+d,%+d (%d of %d pits match GDA), %d cells GDA art, %d kept old (tiles), "
              "%d ground spike decorations removed" % (level, dx, dy, n_match, n_ours, len(applied),
                                                       reverted, n_deco))
    write_preview()
    print("%d GDA pit objects added (tiles %d..%d)" % (len(new_art), first, first + len(new_art) - 1))


if __name__ == "__main__":
    main()
