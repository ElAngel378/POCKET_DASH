"""Lays the object catalog (levels/objects/objects.png) out again, by kind: basics, blocks, slopes,
spikes, saws, orbs and pads, decorations, each kind from a new row. Objects that sat next to each
other stay together (a block style's pieces, a GDA sprite object's block of cells keep their
shape). Also deletes the GDA sprite copies that only repeat another one (tools/import_gda_sprites.py
made one per 8 px offset and flip: _X8 / _Y8 / _HF / _VF), unless a level uses them; flips that look
different stay.

Renumbers everything that holds tile ids: objects.png, objects.tsx (tiles, terrain sets),
famidash_ids.json and the levels' maps (levels/chr_data/tmx), then checks every level still has the
same object (by name) in every cell. objects_dmg.png has its own ids and does not change.

    python tools/relayout_objects.py --dry   (only draws the new layout: docs/objects_relayout.png)
    python tools/relayout_objects.py
"""
import glob
import json
import re
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from objects_catalog import (ART, CATALOG_DIR, COLUMNS, PREVIEW, ROOT, SPRITES_FIRSTGID, TSX,  # noqa: E402
                             load_catalog, write_preview)

TMX_GLOB = str(ROOT / "levels" / "chr_data" / "tmx" / "*.tmx")
FLAGS = 0xF0000000
SECTIONS = ["basics", "blocks", "slopes", "spikes", "saws", "orbs and pads", "decorations"]
DEADLY = {1, 2, 3, 4, 8} | set(range(0x10, 0x16))
VARIANT = re.compile(r"((?:_X8|_Y8|_HF|_VF)*)$")
CELL = re.compile(r"(.*)_R(\d+)C(\d+)$")


def kind(o):
    """SECTIONS index of one object"""
    n, col = o["name"], o["collision"]
    if n == "EMPTY" or (n.startswith(("GROUND", "BG5", "BG6", "SOLID", "INVIS")) and not CELL.fullmatch(n)):
        return 0
    if 0x60 <= col < 0x74 or re.search(r"DEG_|SLOPE|BRICKS(_NO_OUT)?_66|CHKRED_22", n):
        return 2
    if re.search(r"SAW|FLOWER|HEXAGON|ROTATION", n):
        return 4
    if re.search(r"ORB|PAD", n):
        return 5
    shape = int(o["shape"].split(",")[0]) if "shape" in o else None
    deadly = col in DEADLY or shape in (1, 2)
    if not deadly and (o["palette"] == 2 or (col == 0 and "FAKE" not in n) or "DECO" in n or "BACKGROUND" in n):
        return 6
    if deadly or "SPIKE" in n:
        return 3
    return 1


def tmx_layers(text):
    """[(start, end) of each map layer's csv data] of a TMX's text"""
    return [(m.start(1), m.end(1)) for m in re.finditer(r'<data encoding="csv">(.*?)</data>', text, re.S)]


def level_grids(cat):
    """{tmx: [names of the objects in each cell of each layer]} (sprites by gid)"""
    out = {}
    for f in sorted(glob.glob(TMX_GLOB)):
        text = Path(f).read_text(encoding="utf-8")
        if "objects.tsx" not in text:
            continue
        cells = []
        for a, b in tmx_layers(text):
            for v in text[a:b].split(","):
                g = int(v) if v.strip() else 0
                t = g & ~FLAGS
                cells.append((g & FLAGS, cat.objects[t - 1]["name"] if 0 < t < SPRITES_FIRSTGID else t))
        out[f] = cells
    return out


def block_image(cat, cells):
    """the drawn pixels of a group of cells {(c, r): tid}, cropped: compares copies"""
    img = Image.new("L", ((max(c for c, _ in cells) + 1) * 16, (max(r for _, r in cells) + 1) * 16), 0)
    for (c, r), t in cells.items():
        q = cat.art(t)
        for y in range(16):
            for x in range(16):
                img.putpixel((c * 16 + x, r * 16 + y), q[(y >> 3) * 2 + (x >> 3)][y & 7][x & 7] + 1
                             if q[(y >> 3) * 2 + (x >> 3)][y & 7][x & 7] else 0)
    box = img.getbbox()
    return img.crop(box).tobytes() + bytes(str(img.crop(box).size), "ascii") if box else b""


def units(cat):
    """[(kind, first old tid, {(c, r): old tid})]: the GDA sprite blocks, and the other objects
    joined to their neighbours of the same kind in the old layout"""
    blocks = {}
    for t, o in cat.objects.items():
        m = CELL.fullmatch(o["name"])
        if m:
            blocks.setdefault(m.group(1), {})[(int(m.group(3)), int(m.group(2)))] = t
    out = []
    for name, cells in blocks.items():
        ks = [kind(cat.objects[t]) for t in cells.values()]
        k = 4 if any(k == 4 for k in ks) else 3 if 3 in ks else 1 if 1 in ks else max(set(ks), key=ks.count)
        if re.search(r"SAW|FLOWER|HEXAGON|ROTATION", name):
            k = 4
        out.append([k, min(cells.values()), cells, name])
    in_block = {t for _, _, c, _ in out for t in c.values()}
    seen = set()
    for t0 in sorted(cat.objects):
        if t0 in seen or t0 in in_block:
            continue
        k, todo, comp = kind(cat.objects[t0]), [t0], set()
        seen.add(t0)
        while todo:
            t = todo.pop()
            comp.add(t)
            c, r = t % COLUMNS, t // COLUMNS
            for nc, nr in ((c - 1, r), (c + 1, r), (c, r - 1), (c, r + 1)):
                n = nr * COLUMNS + nc
                if 0 <= nc < COLUMNS and nr >= 0 and n not in seen and n in cat.objects \
                        and n not in in_block and kind(cat.objects[n]) == k:
                    seen.add(n)
                    todo.append(n)
        c0 = min(t % COLUMNS for t in comp)
        r0 = min(t // COLUMNS for t in comp)
        out.append([k, min(comp), {(t % COLUMNS - c0, t // COLUMNS - r0): t for t in comp}, cat.objects[t0]["name"]])
    return out


def duplicates(cat, us, used):
    """old tids of the variant copies to delete: a unit whose name is another's plus _X8 / _Y8 /
    _HF / _VF and whose drawing is the same as one kept"""
    by_core = {}
    for u in sorted(us, key=lambda u: (len(VARIANT.search(u[3]).group(1)), u[1])):
        name = u[3]
        if CELL.fullmatch(cat.objects[u[1]]["name"]) is None and not VARIANT.search(name).group(1):
            continue
        core = name[:len(name) - len(VARIANT.search(name).group(1))]
        by_core.setdefault(core, []).append(u)
    gone = set()
    for core, group in by_core.items():
        kept = []
        for u in group:
            img = block_image(cat, u[2])
            if VARIANT.search(u[3]).group(1) and img in kept and not any(t in used for t in u[2].values()):
                gone.update(u[2].values())
            else:
                kept.append(img)
    return gone


def layout(us, gone):
    """{old tid: new tid}: each kind from a new row, its units in old order, each at the first place
    (row by row) where its cells are free"""
    new, top = {}, 0
    for k in range(len(SECTIONS)):
        taken, end = set(), top
        for _, _, cells, _ in sorted((u for u in us if u[0] == k), key=lambda u: u[1]):
            cells = {p: t for p, t in cells.items() if t not in gone}
            if not cells:
                continue
            w = max(c for c, _ in cells) + 1
            y = top
            while True:
                x = next((x for x in range(COLUMNS - w + 1)
                          if not any((x + c, y + r) in taken for c, r in cells)), None)
                if x is not None:
                    break
                y += 1
            for (c, r), t in cells.items():
                taken.add((x + c, y + r))
                new[t] = (y + r) * COLUMNS + x + c
                end = max(end, y + r + 1)
        top = end
    return new


def main():
    dry = "--dry" in sys.argv
    cat = load_catalog()
    before = level_grids(cat)
    used = {t for cells in before.values() for _, n in cells if isinstance(n, str) for t in [cat.by_name[n]]}
    us = units(cat)
    gone = duplicates(cat, us, used)
    new = layout(us, gone)
    assert len(set(new.values())) == len(new) == len(cat.objects) - len(gone)
    assert new[0] == 0, "EMPTY must stay tile 0"
    rows = max(new.values()) // COLUMNS + 1
    old_art = Image.open(ART).convert("L")
    art = Image.new("L", (COLUMNS * 16, rows * 16), 255)
    for o, n in new.items():
        art.paste(old_art.crop(((o % COLUMNS) * 16, (o // COLUMNS) * 16, (o % COLUMNS) * 16 + 16, (o // COLUMNS) * 16 + 16)),
                  ((n % COLUMNS) * 16, (n // COLUMNS) * 16))
    print("%d objects, %d copies deleted, %d rows (were %d)" % (len(new), len(gone), rows, old_art.height // 16))
    if dry:
        old_prev = Image.open(PREVIEW).convert("RGBA")
        out = Image.new("RGBA", art.size, (70, 70, 70, 255))
        for o, n in new.items():
            out.alpha_composite(old_prev.crop(((o % COLUMNS) * 16, (o // COLUMNS) * 16, (o % COLUMNS) * 16 + 16, (o // COLUMNS) * 16 + 16)),
                                ((n % COLUMNS) * 16, (n // COLUMNS) * 16))
        out.save(ROOT / "docs" / "objects_relayout.png")
        print("docs/objects_relayout.png")
        return

    remap = lambda m: m.group(1) + str(new[int(m.group(2))]) + m.group(3)  # noqa: E731
    tsx = TSX.read_text(encoding="utf-8")
    blocks = list(re.finditer(r'<tile id="(\d+)">.*?</tile>\n', tsx, re.S))   # each one but the first after " "
    head, tail = tsx[:blocks[0].start()], tsx[blocks[-1].end():]
    kept = sorted((new[int(m.group(1))], re.sub(r'^<tile id="\d+"', '<tile id="%d"' % new[int(m.group(1))], m.group(0)))
                  for m in blocks if int(m.group(1)) in new)
    tail = re.sub(r'(<wangset [^>]* tile=")(\d+)(")', remap, tail)
    tail = re.sub(r'(<wangtile tileid=")(\d+)(")', remap, tail)
    TSX.write_text(head + " ".join(b for _, b in kept) + tail, encoding="utf-8")
    art.save(ART)
    fj = CATALOG_DIR / "famidash_ids.json"
    ids = json.loads(fj.read_text())
    fj.write_text(json.dumps({lv: {k: new[v] for k, v in m.items()} for lv, m in ids.items()}, indent=1) + "\n")
    for f in before:
        text = Path(f).read_text(encoding="utf-8")
        out, last = [], 0
        for a, b in tmx_layers(text):
            vals = []
            for v in text[a:b].split(","):
                g = int(v) if v.strip() else 0
                t = g & ~FLAGS
                if 0 < t < SPRITES_FIRSTGID:
                    v = re.sub(r"\d+", str((g & FLAGS) | (new[t - 1] + 1)), v)
                vals.append(v)
            out += [text[last:a], ",".join(vals)]
            last = b
        Path(f).write_text("".join(out) + text[last:], encoding="utf-8", newline="\n")
    write_preview()
    after = level_grids(load_catalog())
    bad = [f for f in before if before[f] != after[f]]
    if bad:
        raise SystemExit("levels changed: %s" % bad)
    print("all %d levels have the same objects in every cell" % len(before))


if __name__ == "__main__":
    main()
