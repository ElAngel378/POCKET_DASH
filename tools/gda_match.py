"""Which GDA object is each of our catalog objects?

Lines up the levels both games have (ours: levels/chr_data/tmx, GDA's: ../geometry_dash_advance/
levels) and counts, for every cell holding one of our objects, what GDA has there: a metatile
(MT:name) or a sprite object covering the cell (SP:type). Our object's GDA object is the one most
often in its place.

    python tools/gda_match.py      -> docs/GDA_OBJECT_MATCH.md, docs/gda_object_match.png
"""
import collections
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda  # noqa: E402
from objects_catalog import PREVIEW, COLUMNS, load_catalog  # noqa: E402

OUR_TMX = ROOT / "levels" / "chr_data" / "tmx"
# Levels both games have (GDA name: ours)
SHARED = {n: n for n in ("backontrack", "baseafterbase", "cantletgo", "clutterfunk", "cycles", "dryout",
                         "jumper", "leveleasy", "polargeist", "stereomadness", "theoryofeverything",
                         "timemachine", "xstep", "clubstep", "electromanadventures")}
SHARED["ultimatedestruction"] = "ultiatedestruction"


def our_level(name):
    root = ET.parse(OUR_TMX / (name + ".tmx")).getroot()
    w, h = int(root.get("width")), int(root.get("height"))
    layers = {}
    for layer in root.findall("layer"):
        v = [int(x) & 0x0FFFFFFF for x in layer.find("data").text.replace("\n", "").split(",") if x.strip()]
        layers[layer.get("name") or "map"] = [v[y * w:(y + 1) * w] for y in range(h)]
    bg = next(v for k, v in layers.items() if k != "SP")
    return w, h, bg


def gda_cells(name, mt_names, sp_names):
    """GDA level -> (w, h, rows of [labels at the cell])"""
    w, h, layers, objs = gda.read_level(name)
    l1, l2 = layers["l1"], layers.get("l2")
    cells = [[[] for _ in range(w)] for _ in range(h)]
    for y in range(h):
        for x in range(w):
            # both layers: a cell can hold two GDA pieces (l1 drawn over l2), joined by " & "
            both = [g - 1 for g in (l1[y][x], l2[y][x] if l2 else 0) if g > 1]
            if both:
                cells[y][x].append("MT:" + " & ".join(mt_names[g] for g in both))
    for o in objs:
        g = o["gid"]
        if g >= gda.SPRITE_GID:
            label = "SP:" + sp_names[g - gda.SPRITE_GID]
        elif g > 1:
            label = "SPMT:" + mt_names[g - 1]   # a metatile placed as a sprite
        else:
            continue
        x0, y0 = int(o["x"] // 16), int(o["y"] // 16)
        x1, y1 = int((o["x"] + o["w"] - 1) // 16), int((o["y"] + o["h"] - 1) // 16)
        for y in range(max(0, y0), min(h, y1 + 1)):
            for x in range(max(0, x0), min(w, x1 + 1)):
                cells[y][x].append(label)
    return w, h, cells


def votes():
    mt_names = [n for n, _, _ in gda.metatiles()]
    sp_names = gda.sprite_types()
    v = collections.defaultdict(collections.Counter)
    used = collections.Counter()
    for gname, oname in SHARED.items():
        gw, gh, gc = gda_cells(gname, mt_names, sp_names)
        ow, oh, om = our_level(oname)
        dy = oh - gh   # bottoms lined up (the ground)

        def agree(dx):
            return sum(1 for y in range(max(0, -dy), gh) if 0 <= y + dy < oh
                       for x in range(0, gw, 3) if 0 <= x + dx < ow and gc[y][x] and om[y + dy][x + dx] > 1)
        dx = max(range(-8, 9), key=agree)
        for y in range(oh):
            for x in range(ow):
                t = om[y][x]
                if t <= 0:
                    continue
                used[t - 1] += 1
                gy, gx = y - dy, x - dx
                labels = gc[gy][gx] if 0 <= gy < gh and 0 <= gx < gw else []
                for lab in labels or ["(empty)"]:
                    v[t - 1][lab] += 1
    return v, used


def our_mask(cat, tid):
    a = cat.art(tid)
    return [[a[(y >> 3) * 2 + (x >> 3)][y & 7][x & 7] != 0 for x in range(16)] for y in range(16)]


def gda_mask(img):
    px = img.load()
    return [[px[x, y][3] > 0 for x in range(16)] for y in range(16)]


def shape_candidates(cat, n=3):
    """{our tid: [(GDA label, matching pixels)]}: the plain GDA metatiles (no colour channel copies,
    first of each name) whose filled pixels agree most with ours"""
    seen = set()
    gm = []
    for i, (name, _, _) in enumerate(gda.metatiles()):
        if i == 0 or name in seen or re.match(r"(\d\.\d_)?(COL\d|LBG)_", name):
            continue
        seen.add(name)
        m = gda_mask(gda.metatile_image(i))
        gm.append((name, sum(1 << (y * 16 + x) for y in range(16) for x in range(16) if m[y][x])))
    out = {}
    for tid in cat.objects:
        m = our_mask(cat, tid)
        bits = sum(1 << (y * 16 + x) for y in range(16) for x in range(16) if m[y][x])
        best = sorted(((256 - bin(bits ^ g).count("1"), name) for name, g in gm), reverse=True)[:n]
        out[tid] = [("MT:" + name, sc) for sc, name in best]
    return out


def main():
    cat = load_catalog()
    v, used = votes()
    preview = Image.open(PREVIEW).convert("RGBA")
    mt_index = {n: i for i, (n, _, _) in enumerate(gda.metatiles())}
    sp_index = {n: i for i, n in enumerate(gda.sprite_types())}

    def gda_icon(label):
        kind, name = label.split(":", 1) if ":" in label else ("", "")
        if kind in ("MT", "SPMT"):
            parts = name.split(" & ")
            img = Image.new("RGBA", (16, 16))
            for n in reversed(parts):   # l2 first, l1 over it
                t = gda.metatile_image(mt_index[n])
                img.paste(t, (0, 0), t)
            return img
        if kind == "SP":
            return gda.sprite_image(sp_index[name])
        return None

    shapes = shape_candidates(cat)
    rows = []
    for tid, o in sorted(cat.objects.items()):
        c = v.get(tid, collections.Counter())
        total = sum(c.values())
        top = c.most_common(3)
        rows.append((tid, o["name"], used[tid], total, top, shapes[tid]))

    lines = ["# Our objects in GDA", "",
             "Made by `python tools/gda_match.py`: every catalog object, and what Geometry Dash Advance has",
             "in its place in the levels both games have (MT = metatile, SP = sprite object, SPMT = metatile",
             "placed as a sprite), most often first, with the share of our object's cells. Objects no shared",
             "level uses have no votes. By shape: the GDA metatiles (not the colour channel copies) whose",
             "filled pixels agree most with ours.", "",
             "| id | our object | cells | GDA in its place (share) | GDA by shape (pixels of 256) |", "|---|---|---|---|---|"]
    for tid, name, n, total, top, shape in rows:
        g = ", ".join("%s (%d%%)" % (lab, 100 * k // total) for lab, k in top) if total else "-"
        sh = ", ".join("%s (%d)" % (lab, sc) for lab, sc in shape)
        lines.append("| %d | %s | %d | %s | %s |" % (tid, name, n, g, sh))
    (ROOT / "docs" / "GDA_OBJECT_MATCH.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # picture: our art, then GDA's top 3, 2x
    cw, ch = 16 * 7 + 20, 18
    per_col = 72
    ncols = (len(rows) + per_col - 1) // per_col
    img = Image.new("RGBA", (ncols * (cw + 140), per_col * ch), (40, 40, 48, 255))
    d = ImageDraw.Draw(img)
    for k, (tid, name, n, total, top, shape) in enumerate(rows):
        x0, y0 = (k // per_col) * (cw + 140), (k % per_col) * ch
        ours = preview.crop(((tid % COLUMNS) * 16, (tid // COLUMNS) * 16, (tid % COLUMNS) * 16 + 16, (tid // COLUMNS) * 16 + 16))
        img.paste(ours, (x0, y0), ours)
        for j, (lab, _) in enumerate(top):
            icon = gda_icon(lab)
            if icon is not None:
                img.paste(icon, (x0 + 20 + j * 18, y0), icon)
        for j, (lab, _) in enumerate(shape):
            icon = gda_icon(lab)
            img.paste(icon, (x0 + 80 + j * 18, y0), icon)
        d.text((x0 + 136, y0 + 3), "%d %s" % (tid, name[:22]), fill=(230, 230, 230, 255))
    img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    img.save(ROOT / "docs" / "gda_object_match.png")
    print("%d objects, %d with votes" % (len(rows), sum(1 for r in rows if r[3])))




def parse_label(label):
    """"MT:A & B & SP:C" -> [("MT", "A"), ("MT", "B"), ("SP", "C")]: each part keeps the kind
    before it (MT metatile, SP sprite object, SPMT metatile placed as a sprite), first one drawn on top"""
    out, kind = [], None
    for part in label.split(" & "):
        if ":" in part:
            kind, part = part.split(":", 1)
        out.append((kind, part))
    return out


def review():
    """docs/gda_ours.png: each of our objects next to its GDA object (levels/objects/gda_ours.json)"""
    import json
    cat = load_catalog()
    ours = json.loads((ROOT / "levels" / "objects" / "gda_ours.json").read_text())
    preview = Image.open(PREVIEW).convert("RGBA")
    mt_index = {}
    for i, (n, _, _) in enumerate(gda.metatiles()):
        mt_index.setdefault(n, i)
    sp_index = {n: i for i, n in enumerate(gda.sprite_types())}
    colours = {"same": (90, 200, 90), "combo": (90, 170, 230), "part": (230, 190, 70), "offset": (200, 140, 230),
               "close": (240, 130, 60), "none": (240, 70, 70)}
    per_col, ch, cw = 72, 20, 330
    n = len(ours)
    img = Image.new("RGBA", (((n + per_col - 1) // per_col) * cw, per_col * ch), (34, 34, 42, 255))
    d = ImageDraw.Draw(img)
    for k, (name, m) in enumerate(ours.items()):
        tid = cat.by_name[name]
        x0, y0 = (k // per_col) * cw, (k % per_col) * ch
        o = preview.crop(((tid % COLUMNS) * 16, (tid // COLUMNS) * 16, (tid % COLUMNS) * 16 + 16, (tid // COLUMNS) * 16 + 16))
        img.paste(o, (x0 + 2, y0 + 2), o)
        icon = Image.new("RGBA", (16, 16))
        if m["gda"]:
            for k2, part in reversed(parse_label(m["gda"])):
                t = gda.sprite_image(sp_index[part]) if k2 == "SP" else gda.metatile_image(mt_index[part])
                icon.paste(t, (0, 0), t)
        img.paste(icon, (x0 + 20, y0 + 2), icon)
        d.rectangle((x0 + 38, y0 + 6, x0 + 43, y0 + 11), fill=colours[m["match"]])
        d.text((x0 + 48, y0 + 1), name[:40], fill=(235, 235, 235))
        d.text((x0 + 48, y0 + 10), (m["gda"] or "-")[:44], fill=(150, 150, 160))
    img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    img.save(ROOT / "docs" / "gda_ours.png")
    print(img.size)


if __name__ == "__main__":
    review() if sys.argv[1:] == ["--review"] else main()
