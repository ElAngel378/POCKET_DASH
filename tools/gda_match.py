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


# Famidash's sprite object ids (famidash-main LEVELS/include/lvlset_A/objdefines.h); our SP layer uses them
FAMIDASH_SPRITES = {
    0x00: "CUBE_MODE", 0x01: "SHIP_MODE", 0x02: "BALL_MODE", 0x03: "UFO_MODE", 0x04: "ROBOT_MODE",
    0x05: "BLUE_ORB", 0x06: "PINK_ORB", 0x07: "COIN1", 0x08: "GRAVITY_DOWN_PORTAL", 0x09: "GRAVITY_UP_PORTAL",
    0x0A: "YELLOW_PAD_DOWN", 0x0B: "YELLOW_ORB", 0x0C: "YELLOW_PAD_UP", 0x0D: "GRAVITY_PAD_DOWN",
    0x0E: "GRAVITY_PAD_UP", 0x0F: "LEVEL_END_TRIGGER", 0x10: "GRAVITY_DOWN_UPWARDS_PORTAL",
    0x11: "GRAVITY_DOWN_DOWNWARDS_PORTAL", 0x12: "GRAVITY_UP_UPWARDS_PORTAL", 0x13: "GRAVITY_UP_DOWNWARDS_PORTAL",
    0x14: "SPEED_05_PORTAL", 0x15: "SPEED_10_PORTAL", 0x16: "SPEED_20_PORTAL", 0x17: "SPIDER_MODE",
    0x18: "MINI_PORTAL", 0x19: "GROWTH_PORTAL", 0x1A: "COIN2", 0x1B: "COIN3", 0x1F: "YELLOW_ORB_BIGGER",
    0x20: "SPEED_30_PORTAL", 0x21: "SPEED_40_PORTAL", 0x22: "DUAL_PORTAL", 0x23: "SINGLE_PORTAL",
    0x24: "WAVE_MODE", 0x25: "PINK_PAD_DOWN", 0x26: "PINK_PAD_UP", 0x27: "GREEN_ORB", 0x28: "RED_ORB",
    0x29: "YELLOW_ORB_SMALLER", 0x2A: "LONG_LIGHT", 0x2B: "MEDIUM_LIGHT", 0x2C: "SHORT_LIGHT", 0x2D: "CHAIN",
    0x2E: "BG_SPIKE_GROUP1", 0x2F: "BG_SPIKE_GROUP2", 0x30: "BG_SPIKE_GROUP3", 0x31: "BG_SPIKE_GROUP4",
    0x32: "BIG_DIAMOND", 0x33: "RIGHT_HALF_DIAMOND", 0x34: "QUESTION_MARK", 0x35: "EXCLAMATION_MARK",
    0x36: "RIGHT_ARROW", 0x37: "BIG_X", 0x38: "SHORT_LIGHT_RIGHT", 0x39: "SHORT_LIGHT_LEFT",
    0x3A: "LONG_LIGHT_U", 0x3B: "MEDIUM_LIGHT_U", 0x3C: "SHORT_LIGHT_U", 0x3D: "CHAIN_U",
    0x3E: "MEDIUM_LIGHT_RIGHT", 0x3F: "MEDIUM_LIGHT_LEFT", 0x40: "LONG_LIGHT_RIGHT", 0x41: "LONG_LIGHT_LEFT",
    0x42: "SHORT_LIGHT_U_8_PIXELS", 0x43: "CHAIN_U_8_PIXELS", 0x44: "BLACK_ORB", 0x45: "DASH_ORB",
    0x46: "DASH_GRAVITY_ORB", 0x49: "PULSE_ORB", 0x4A: "MUSIC_NOTE", 0x52: "RED_PAD_DOWN", 0x53: "RED_PAD_UP",
    0x79: "SKULL_ORB", 0x7E: "SUPER_RANDOM_PORTAL", 0xF2: "PLAYER_TRAILS_ON", 0xF3: "PLAYER_TRAILS_OFF",
    0xFD: "GRAVITY_PAD_DOWN_INVISIBLE", 0xFE: "GRAVITY_PAD_UP_INVISIBLE",
}


def famidash_sprite_name(i):
    if i in (121, 126):
        return {121: "MIRROR_EXIT (ours)", 126: "MIRROR_PORTAL (ours)"}[i]
    if 128 <= i <= 175:
        return "BG_COLOR_TRIGGER_%d" % (i - 128)
    if 176 <= i <= 191:
        return "OUTLINE_COLOR_TRIGGER_%d" % (i - 176)
    if 192 <= i <= 239:
        return "GROUND_COLOR_TRIGGER_%d" % (i - 192)
    return FAMIDASH_SPRITES.get(i, "UNKNOWN_%d" % i)


def our_sprites(name):
    """our SP layer -> (w, h, [(sprite id, cell x, cell y)])"""
    root = ET.parse(OUR_TMX / (name + ".tmx")).getroot()
    w, h = int(root.get("width")), int(root.get("height"))
    for layer in root.findall("layer"):
        if layer.get("name") == "SP":
            v = [int(x) & 0x0FFFFFFF for x in layer.find("data").text.replace("\n", "").split(",") if x.strip()]
            return w, h, [(g - 4097, i % w, i // w) for i, g in enumerate(v) if g >= 4097]
    return w, h, []


def sprite_votes():
    """{our sprite id: Counter(GDA label)}: the GDA sprite objects nearest each of our sprites (same
    level line-up as the BG). Colour triggers vote with their GDA channel."""
    mt_names = [n for n, _, _ in gda.metatiles()]
    sp_names = gda.sprite_types()
    v = collections.defaultdict(collections.Counter)
    for gname, oname in SHARED.items():
        gw, gh, gc = gda_cells(gname, mt_names, sp_names)
        ow, oh, om = our_level(oname)
        dy = oh - gh

        def agree(dx):
            return sum(1 for y in range(max(0, -dy), gh) if 0 <= y + dy < oh
                       for x in range(0, gw, 3) if 0 <= x + dx < ow and gc[y][x] and om[y + dy][x + dx] > 1)
        dx = max(range(-8, 9), key=agree)
        _, _, layers, objs = gda.read_level(gname)
        _, _, ours = our_sprites(oname)
        for sid, x, y in ours:
            gx, gy = (x - dx) * 16 + 8, (y - dy) * 16 + 8
            best = None
            trigger = 128 <= sid < 240
            for o in objs:
                g = o["gid"]
                if g < gda.SPRITE_GID or trigger != (g == gda.SPRITE_GID + 3):
                    continue
                cx, cy = o["x"] + o["w"] / 2, o["y"] + o["h"] / 2
                # GDA keeps its colour triggers in rows above the level: those by x only
                d = abs(cx - gx) + (0 if trigger else abs(cy - gy))
                if d <= 40 and (best is None or d < best[0]):
                    best = (d, o)
            if best is None:
                v[sid]["(none near)"] += 1
                continue
            o = best[1]
            name = sp_names[o["gid"] - gda.SPRITE_GID]
            if name == "COL_TRIGGER":
                name += "[%s]" % o["props"].get("Channel", "?")
            flips = "".join(f for f, k in (("H", "hflip"), ("V", "vflip")) if o["props"].get(k) == "true")
            v[sid][name + ("/" + flips if flips else "")] += 1
    return v


def sprite_report():
    v = sprite_votes()
    lines = ["# Our sprite objects in GDA", "",
             "Made by `python tools/gda_match.py --sprites`: every Famidash sprite id our levels use, and the",
             "GDA sprite object nearest it in the shared levels (/H /V: flipped; COL_TRIGGER[channel]).", "",
             "| id | ours (Famidash) | placed | GDA nearest (share) |", "|---|---|---|---|"]
    for sid in sorted(v):
        c = v[sid]
        total = sum(c.values())
        g = ", ".join("%s (%d%%)" % (k, 100 * n // total) for k, n in c.most_common(3))
        lines.append("| %d | %s | %d | %s |" % (sid, famidash_sprite_name(sid), total, g))
    (ROOT / "docs" / "GDA_SPRITE_MATCH.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[6:]))


def sprite_review():
    """docs/gda_sprites.png: each Famidash sprite id our levels use (its icon from
    levels/chr_data/graphics/sprites.png) next to its GDA sprite (levels/objects/gda_sprites.json);
    the colour triggers once per kind"""
    import json
    ours = json.loads((ROOT / "levels" / "objects" / "gda_sprites.json").read_text())
    fam = Image.open(ROOT / "levels" / "chr_data" / "graphics" / "sprites.png").convert("RGBA")
    sp_index = {n: i for i, n in enumerate(gda.sprite_types())}
    colours = {"same": (90, 200, 90), "close": (240, 130, 60), "none": (240, 70, 70)}
    rows = [(int(k), m) for k, m in ours.items() if not (128 < int(k) < 176 or 176 < int(k) < 192 or 192 < int(k) < 240)]

    def big(t):
        b = Image.new("RGBA", (16, 16), (190, 190, 200, 255))
        b.alpha_composite(t)
        return b.resize((64, 64), Image.NEAREST)

    per_col = 16
    img = Image.new("RGBA", (((len(rows) + per_col - 1) // per_col) * 420, per_col * 74), (34, 34, 42, 255))
    d = ImageDraw.Draw(img)
    for k, (i, m) in enumerate(rows):
        x, y = (k // per_col) * 420, (k % per_col) * 74
        img.paste(big(fam.crop(((i % 16) * 16, (i // 16) * 16, (i % 16) * 16 + 16, (i // 16) * 16 + 16))), (x + 4, y + 4))
        if m["gda"]:
            img.paste(big(gda.sprite_image(sp_index[m["gda"][3:]])), (x + 72, y + 4))
        d.rectangle((x + 142, y + 12, x + 148, y + 18), fill=colours[m["match"]])
        label = m["ours"] if not 128 <= i < 240 else m["ours"].rsplit("_", 1)[0] + " (all)"
        d.text((x + 154, y + 10), "%d %s" % (i, label[:32]), fill=(240, 240, 240))
        d.text((x + 142, y + 26), (m["gda"] or "-")[:40], fill=(160, 200, 255))
        note = m.get("note", "")
        d.text((x + 142, y + 42), note[:40], fill=(170, 170, 170))
        d.text((x + 142, y + 56), note[40:80], fill=(170, 170, 170))
    img.save(ROOT / "docs" / "gda_sprites.png")
    print(img.size)


if __name__ == "__main__":
    {"--review": review, "--sprites": sprite_report, "--sprite-review": sprite_review}.get((sys.argv[1:] or [""])[0], main)()
