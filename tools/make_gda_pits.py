"""GDA-style spike pits: GDA's background spike sprites behind our black pit spikes, as strips of
catalog objects, and every level's white background-spike pits (Famidash's BOTTOM / TOP
_BACKGROUND_SPIKES and their _SB look) replaced by them.

    python tools/make_gda_pits.py

GDA draws a pit as its black GROUND_SPIKE_BOTTOM metatile in front of a BIG / MEDIUM / SMALL_BG_SPIKES
sprite (in the player's colour); its big sprite's peaks stand 5 px above the pit cell. Here:
    - GDA_PIT_C0..C3:        BIG_BG_SPIKES (64 px: 4 cells, shifted down 5 px to fit the cell)
                             behind PITSPIKES; replaces BOTTOM_BACKGROUND_SPIKES
    - GDA_PIT_TOP_C0..C3:    the same upside down, behind PITSPIKES_TOP; replaces
                             TOP_BACKGROUND_SPIKES
    - GDA_PIT_SB_C0..C1:     SMALL_BG_SPIKES (32 px: 2 cells) behind BOTTOM_WAVYPIT; replaces
                             BOTTOM_BACKGROUND_SPIKES_SB (Xstep)
    - GDA_PIT_SB_TOP_C0..C1: upside down, behind TOP_WAVYPIT; replaces TOP_BACKGROUND_SPIKES_SB
A replaced cell takes the strip piece of its map column (column % width), so a run of them is one
continuous row of spikes. Colours: palette 2 (the spikes in the decoration accent, the cube's
colour, like GDA's player colour; the pit black). Collision: the bottom pits COL_PIT_SPIKES (they
kill at any x low in the cell, as the engine did for BOTTOM_BACKGROUND_SPIKES by its id), the top
ones COL_DEATH_TOP like TOP_BACKGROUND_SPIKES. DMG: the pieces keep the old pits' art (dmg_art,
objects_dmg.png): the strips' ~16 extra tiles a level (32 with the upside down ones, no flips on
DMG) left Clutterfunk no room for its saws. Run once: the objects are added to the catalog and the
levels rewritten (a second run finds no old pits and adds nothing).
"""
import re
import sys
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
COL_PIT_SPIKES = 0x17
COL_DEATH_TOP = 0x03
PAL_DECO = 2


def by_name(cat, name):
    return next(t for t, o in cat.objects.items() if o["name"] == name)


def cell(sheet, tid):
    """objects.png cell tid as colour indices 0..3 (16x16 'L' image)"""
    x, y = (tid % COLUMNS) * 16, (tid // COLUMNS) * 16
    return sheet.crop((x, y, x + 16, y + 16)).point(lambda v: (255 - v + 42) // 85)


def spikes(tid, top_row):
    """GDA sprite tid's art from row top_row, 16 rows, in our colours (its brightest = 3, the
    accent; the darker edge = 1)"""
    img, ox, oy = gda.sprite_art(tid)
    used = sorted(set(img.getdata()) - {0})
    img = img.point([0 if not c else 3 if c == used[-1] else 1 for c in range(256)])
    return img.crop((0, top_row, img.width, top_row + 16))


def strip(spike_img, pit, vflip):
    """the cells of a pit strip: spike art (upside down when vflip) with the pit in front"""
    if vflip:
        spike_img = spike_img.transpose(Image.FLIP_TOP_BOTTOM)
    out = []
    for c in range(spike_img.width // 16):
        t = spike_img.crop((c * 16, 0, c * 16 + 16, 16))
        tp, pp = t.load(), pit.load()
        for y in range(16):
            for x in range(16):
                if pp[x, y]:
                    tp[x, y] = pp[x, y]
        out.append(t)
    return out


def dmg_fallback():
    """DMG art of the GDA_PIT_* pieces: the art of the pit they replaced, in a new row of
    objects_dmg.png"""
    cat = load_catalog()
    olds = {"GDA_PIT_C": "BOTTOM_BACKGROUND_SPIKES", "GDA_PIT_TOP_C": "TOP_BACKGROUND_SPIKES",
            "GDA_PIT_SB_C": "BOTTOM_BACKGROUND_SPIKES_SB", "GDA_PIT_SB_TOP_C": "TOP_BACKGROUND_SPIKES_SB"}
    sheet, dmg = Image.open(ART).convert("L"), Image.open(DMG_ART).convert("L")
    first = (dmg.height // 16) * COLUMNS
    grown = Image.new("L", (dmg.width, dmg.height + 16), 255)
    grown.paste(dmg, (0, 0))
    art_of = {}
    for k, (prefix, old) in enumerate(olds.items()):
        t = by_name(cat, old)
        grown.paste(sheet.crop(((t % COLUMNS) * 16, (t // COLUMNS) * 16, (t % COLUMNS) * 16 + 16,
                                (t // COLUMNS) * 16 + 16)), (k * 16, dmg.height))
        art_of[prefix] = first + k
    grown.save(DMG_ART)
    tsx = TSX.read_text(encoding="utf-8")
    for tid, o in cat.objects.items():
        prefix = o["name"].rstrip("0123456789")
        if prefix in art_of:
            tsx, n = re.subn(r'(<tile id="%d">\s*<properties>(?:(?!</properties>).)*?<property name="dmg_art" type="int" value=")-1"' % tid,
                             r'\g<1>%d"' % art_of[prefix], tsx, count=1, flags=re.S)
            assert n == 1, o["name"]
    TSX.write_text(tsx, encoding="utf-8")


def main():
    cat = load_catalog()
    if any(o["name"].startswith("GDA_PIT_") for o in cat.objects.values()):
        raise SystemExit("the GDA pits are already in the catalog")
    sheet = Image.open(ART).convert("L")
    big = spikes(gda.sprite_types().index("BIG_BG_SPIKES"), 21)      # peaks at row 0 (GDA: -5)
    small = spikes(gda.sprite_types().index("SMALL_BG_SPIKES"), 8)   # rows 0..15 of the cell
    groups = [  # name, replaces, cells, collision
        ("GDA_PIT", "BOTTOM_BACKGROUND_SPIKES", strip(big, cell(sheet, by_name(cat, "PITSPIKES")), False), COL_PIT_SPIKES),
        ("GDA_PIT_TOP", "TOP_BACKGROUND_SPIKES", strip(big, cell(sheet, by_name(cat, "PITSPIKES_TOP")), True), COL_DEATH_TOP),
        ("GDA_PIT_SB", "BOTTOM_BACKGROUND_SPIKES_SB", strip(small, cell(sheet, by_name(cat, "BOTTOM_WAVYPIT")), False), COL_PIT_SPIKES),
        ("GDA_PIT_SB_TOP", "TOP_BACKGROUND_SPIKES_SB", strip(small, cell(sheet, by_name(cat, "TOP_WAVYPIT")), True), COL_DEATH_TOP),
    ]

    # a new row of objects.png
    first = (max(cat.objects) // COLUMNS + 1) * COLUMNS
    grown = Image.new("L", (sheet.width, (first // COLUMNS + 1) * 16), 255)
    grown.paste(sheet, (0, 0))
    sheet = grown
    tid, tiles, replace = first, [], {}
    for name, old, cells, col in groups:
        new_ids = []
        for c, img in enumerate(cells):
            px = img.load()
            for y in range(16):
                for x in range(16):
                    sheet.putpixel(((tid % COLUMNS) * 16 + x, (tid // COLUMNS) * 16 + y), 255 - 85 * px[x, y])
            tiles.append(
                ' <tile id="%d">\n  <properties>\n'
                '   <property name="name" value="%s_C%d"/>\n'
                '   <property name="collision" type="int" value="%d"/>\n'
                '   <property name="palette" type="int" value="%d"/>\n'
                '   <property name="famidash_id" type="int" value="-1"/>\n'
                '   <property name="dmg_art" type="int" value="-1"/>\n'
                '  </properties>\n </tile>\n' % (tid, name, c, col, PAL_DECO))
            new_ids.append(tid)
            tid += 1
        replace[by_name(cat, old)] = new_ids
    sheet.save(ART)
    tsx = TSX.read_text(encoding="utf-8")
    tsx = re.sub(r'tilecount="\d+"', 'tilecount="%d"' % ((first // COLUMNS + 1) * COLUMNS), tsx, count=1)
    TSX.write_text(tsx.replace("</tileset>", "".join(tiles) + "</tileset>"), encoding="utf-8")

    # the levels: the map layer (catalog firstgid 1), flip bits kept
    for p in sorted(TMX.glob("*.tmx")):
        s = p.read_text(encoding="utf-8")
        w = int(re.search(r'<map [^>]*\swidth="(\d+)"', s).group(1))
        m = re.search(r'(<layer id="1"[^>]*>\s*<data encoding="csv">)(.*?)(</data>)', s, re.S)
        if not m:
            continue
        rows = m.group(2).strip("\r\n").split("\n")
        n = 0
        out_rows = []
        for row in rows:
            vals = row.rstrip("\r").split(",")
            for i, v in enumerate(vals):
                if not v.strip():
                    continue
                g = int(v)
                t = (g & 0x0FFFFFFF) - 1
                if t in replace:
                    piece = replace[t]
                    vals[i] = str((g & ~0x0FFFFFFF) | (piece[i % len(piece)] + 1))
                    n += 1
            out_rows.append(",".join(vals) + ("\r" if row.endswith("\r") else ""))
        if n:
            body = "\n".join(out_rows)
            lead = m.group(2)[:len(m.group(2)) - len(m.group(2).lstrip("\r\n"))]
            tail = m.group(2)[len(m.group(2).rstrip("\r\n")):]
            s = s[:m.start(2)] + lead + body + tail + s[m.end(2):]
            p.write_text(s, encoding="utf-8", newline="")
            print("%s: %d pit cells" % (p.stem, n))
    dmg_fallback()
    write_preview()
    print("added %d objects (tiles %d..%d)" % (tid - first, first, tid - 1))


if __name__ == "__main__":
    main()
