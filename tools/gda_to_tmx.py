"""A stretch of a Geometry Dash Advance level as our objects, in one of our TMX levels: GDA's tile
layers cell for cell, so everything stays on GDA's grid. No sprites yet.

    python tools/gda_to_tmx.py electromanadventures test 282-322:12 382-392:58

copies GDA columns 282..321 to our column 12 and 382..391 to column 58, replacing everything in
every layer of test.tmx. Rows count from the bottom of the level.

A GDA piece becomes the object whose art is exactly the same, in any rotation or flip. A piece with
no exact match becomes the GD object GD's own level has in those cells, in the orientation GD uses
most there. The report prints how well each piece's art matches GDA's.
"""
import base64
import math
import re
import sys
import zlib
from collections import Counter, defaultdict
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gda
import gd_objects as GO
import import_gda_art as IA

ROOT = Path(__file__).resolve().parent.parent
TMX = ROOT / "levels" / "chr_data" / "tmx"
ART = ROOT / "levels" / "chr_data" / "objects" / "art"
GD_LEVELS = Path("C:/Program Files (x86)/Steam/steamapps/common/Geometry Dash/Resources/levels")
GD_LEVEL = {"clutterfunk": 11, "theoryofeverything": 12, "electromanadventures": 13, "clubstep": 14}
FLIP_H, FLIP_V, FLIP_D = 1 << 31, 1 << 30, 1 << 29
# GD stacks these on the same cells as other objects, so the tally can't tell them apart
FIXED = {"GROUND_WAVY_SPIKE_BOTTOM": "gd61", "BLK_FILLER": "fd_black_none", "1.6_WAVE_DECO_MIDDLE": "gd157",
         "1.6_WAVE_DECO_LFT": "gd158", "1.6_WAVE_DECO_RGT": "gd159"}


def gd_objects(level):
    raw = (GD_LEVELS / ("%d.txt" % level)).read_bytes().strip()
    text = zlib.decompress(base64.urlsafe_b64decode(raw + b"=" * (-len(raw) % 4)), 15 + 32).decode()
    for item in text.split(";")[1:]:
        p = item.split(",")
        if len(p) >= 6:
            k = dict(zip(p[0::2], p[1::2]))
            yield (int(k["1"]), float(k["2"]), float(k["3"]), int(k.get("4", 0)), int(k.get("5", 0)),
                   int(float(k.get("6", 0))))


def gd_matrix(flip_x, flip_y, angle):
    """the 2x2 matrix (y down) of a GD object: flipped, then turned clockwise"""
    c, s = round(math.cos(math.radians(angle))), round(math.sin(math.radians(angle)))
    fx, fy = -1 if flip_x else 1, -1 if flip_y else 1
    return (c * fx, -s * fy, s * fx, c * fy)


def tiled_matrix(flags):
    a, b, c, d = (0, 1, 1, 0) if flags & FLIP_D else (1, 0, 0, 1)
    if flags & FLIP_H:
        a, b = -a, -b
    if flags & FLIP_V:
        c, d = -c, -d
    return (a, b, c, d)


ORIENTATIONS = [h | v | d for d in (0, FLIP_D) for h in (0, FLIP_H) for v in (0, FLIP_V)]
TO_FLAGS = {tiled_matrix(f): f for f in ORIENTATIONS}


def oriented(rows, flags):
    """rows (16x16) as Tiled shows them with these flags"""
    if flags & FLIP_D:
        rows = [list(r) for r in zip(*rows)]
    if flags & FLIP_H:
        rows = [r[::-1] for r in rows]
    if flags & FLIP_V:
        rows = rows[::-1]
    return rows


def our_art(key):
    path = ART / (key + ".png")
    if not path.exists():
        return None
    img = Image.open(path)
    if img.size != (16, 16):
        return None
    data = list(img.getdata())
    return [data[y * 16:y * 16 + 16] for y in range(16)]


def gda_art(tiles, chr_data):
    rows = [[0] * 16 for _ in range(16)]
    for q, (tile, h, v, _pal) in enumerate(tiles):
        raw = chr_data[tile * 32:tile * 32 + 32]
        for y in range(8):
            for x in range(8):
                val = (raw[y * 4 + x // 2] >> (4 * (x & 1))) & 15
                rows[(q // 2) * 8 + (7 - y if v else y)][(q % 2) * 8 + (7 - x if h else x)] = IA.FILLED_COLOURS.get(val, 2)
    return rows


def score(a, b):
    return sum(x == y for ra, rb in zip(a, b) for x, y in zip(ra, rb)) / 256


def pairings(name, gda_h, layers, names):
    """GDA piece name -> Counter of (GD id, flip x, flip y, angle)"""
    w = len(layers["l1"][0])
    tally = defaultdict(Counter)
    for i, x, y, fx, fy, angle in gd_objects(GD_LEVEL[name]):
        col, row = int(x // 30), gda_h - 1 - int(y // 30)
        if 0 <= col < w and 0 <= row < gda_h:
            for layer in ("l1", "l2"):
                g = layers[layer][row][col]
                if g > 1:
                    tally[names[g - 1]][(i, fx, fy, angle)] += 1
    return tally


_drawn = None


def drawn_16():
    """(key, art) of every 16x16 object with art of its own"""
    global _drawn
    if _drawn is None:
        _drawn = [(k, our_art(k)) for k in GO.DRAWN if our_art(k)]
    return _drawn


def choose(piece, art, tally):
    """-> (object key, Tiled flags, how well it matches GDA's art, or None without art)"""
    if piece in FIXED:
        return FIXED[piece], 0, None
    for key, rows in drawn_16():
        for flags in ORIENTATIONS:
            if oriented(rows, flags) == art:
                return key, flags, 1.0
    best = None
    for (i, fx, fy, angle), n in tally[piece].items():
        key = "gd%d" % i
        if key not in GO.OBJECTS or angle % 90:
            continue
        flags = TO_FLAGS[gd_matrix(fx, fy, angle)]
        rows = our_art(key)
        match = score(oriented(rows, flags), art) if rows else None
        cand = (n, match or 0, key, flags, match)
        if best is None or cand[:2] > best[:2]:
            best = cand
    return (best[2], best[3], best[4]) if best else (None, 0, None)


def main(level, target, spans):
    w, h, layers, _sprites = gda.read_level(level)
    table = gda.metatiles()
    names = [n for n, _, _ in table]
    tiles_of = {n: t for n, t, _ in reversed(table)}
    chr_data = (gda.GDA / "graphics" / "tileset.chr").read_bytes()
    tally = pairings(level, h, layers, names)
    path = TMX / (target + ".tmx")
    text = path.open(newline="").read()
    root_w, root_h = (int(v) for v in re.search(r'<map [^>]*?\bwidth="(\d+)" height="(\d+)"', text).groups())
    drop = root_h - h
    out = {n: [0] * (root_w * root_h) for n in re.findall(r'<layer [^>]*name="([^"]+)"', text)}
    chosen, count, missing = {}, Counter(), Counter()
    for src, dst in (("l2", "Layer 1"), ("l1", "Layer 2")):
        for a, b, to in spans:
            for col in range(a, b):
                for row in range(h):
                    g = layers[src][row][col]
                    if g <= 1:
                        continue
                    piece = names[g - 1]
                    if piece not in chosen:
                        chosen[piece] = choose(piece, gda_art(tiles_of[piece], chr_data), tally)
                    key, flags, _m = chosen[piece]
                    if key is None or key not in GO.OBJECTS:
                        missing[piece] += 1
                        continue
                    count[piece] += 1
                    out[dst][(row + drop) * root_w + to + col - a] = (GO.OBJECTS[key][0] + 1) | flags

    def fill(m):
        name = re.search(r'name="([^"]+)"', m.group(1)).group(1)
        d = out[name]
        return m.group(1) + m.group(2) + "\n" + ",\n".join(
            ",".join(str(v) for v in d[y * root_w:(y + 1) * root_w]) for y in range(root_h)) + "\n" + m.group(4)

    text = re.sub(r'(<layer [^>]*>\s*)(<data encoding="csv">)(.*?)(</data>)', fill, text, flags=re.S)
    path.write_text(text, newline="")
    flag_names = {FLIP_H: "H", FLIP_V: "V", FLIP_D: "D"}
    for piece, n in count.most_common():
        key, flags, match = chosen[piece]
        shown = "".join(c for f, c in flag_names.items() if flags & f)
        print("%4d %-32s %-6s %-4s %s" % (n, piece, key, shown, "-" if match is None else "%d%%" % (match * 100)))
    print("placed %d cells; not placed: %s" % (sum(count.values()), dict(missing)))


if __name__ == "__main__":
    spans = []
    for arg in sys.argv[3:]:
        a, b, to = re.fullmatch(r"(\d+)-(\d+):(\d+)", arg).groups()
        spans.append((int(a), int(b), int(to)))
    main(sys.argv[1], sys.argv[2], spans)
