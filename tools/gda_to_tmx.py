"""A Geometry Dash Advance level, or a stretch of one, as our objects in one of our TMX levels. No
sprites yet: only GDA's tile layers and the metatiles on its object layer.

    python tools/gda_to_tmx.py electromanadventures test                    the whole level
    python tools/gda_to_tmx.py electromanadventures test 282-322:12         columns 282..321 to column 12

Replaces everything in every layer of the TMX (--add keeps what is there). The whole level also sets the TMX's width to GDA's.
Rows count from the bottom of the level.

A GDA piece becomes the object whose art is exactly the same, in any rotation or flip. Pieces of
Famidash's families can't match exactly, their art is another colour: BLOCK_NAMES and NAMED give the
object and direction from GDA's name for the piece. A piece with neither is left out and reported.
Metatiles on the object layer go on the free layers, on the nearest 8 px position. A level has 256
cell types: if the objects need more, the rarest ones go on whole cells instead.
"""
import re
import sys
from collections import Counter
from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gda
import gd_objects as GO
import import_gda_art as IA
import level_objects as LO

ROOT = Path(__file__).resolve().parent.parent
TMX = ROOT / "levels" / "chr_data" / "tmx"
ART = ROOT / "levels" / "chr_data" / "objects" / "art"
PREVIEWS = ROOT / "levels" / "chr_data" / "objects" / "tiles"
FLIP_H, FLIP_V, FLIP_D = 1 << 31, 1 << 30, 1 << 29

TURN = {"": 0, "R90": FLIP_D | FLIP_H, "R180": FLIP_H | FLIP_V, "R270": FLIP_D | FLIP_V, "V": FLIP_V, "H": FLIP_H}
SIDES = {"UP": "", "TOP": "", "RIGHT": "R90", "RGT": "R90", "DOWN": "R180", "BOTTOM": "R180", "BOT": "R180",
         "LEFT": "R270", "LFT": "R270"}
CORNERS = {"TL": "", "TR": "R90", "BR": "R180", "BL": "R270"}
# piece name after the family prefix -> (shape, transform of the shape as drawn: see FAMILY in gd_objects)
# (attached to that side: the object's art stands on its bottom)
ATTACHED = {"BOTTOM": "", "TOP": "R180", "LEFT": "R90", "RIGHT": "R270"}
BLOCK_NAMES = ((r"^FULL$", "full", None), (r"^DB_SIDE_V$", "opp", ""), (r"^DB_SIDE_H$", "opp", "R90"),
               (r"^SIDE_H$", "opp", ""), (r"^SIDE_V$", "opp", "R90"),   # bricks: named the other way round
               (r"^SIDE_(\w+)$", "line", SIDES), (r"^CORNER_(\w+)$", "corner", CORNERS),
               (r"^(?:INS_CRN|CRN_INS)_(\w+)$", "dot", CORNERS), (r"^DB_CRN_(\w+)$", "three", SIDES),
               (r"^FILLER$", "none", None))
BLOCK_FAMILIES = {
    "X_": {s: "fd_x_" + s for s in ("full", "line", "corner", "dot", "opp", "none", "three")},
    "BLK_": {s: "fd_black_" + s for s in ("full", "line", "corner", "dot", "opp", "none", "three")},
    "BRICKS_": GO.BRICK,
    "CHK_": GO.GRID,
}
# (name, object, how its direction is named), most specific first
NAMED = ((r"^1\.6_SMALL_SPIKE_(\w+)$", "gd179", ATTACHED),
         (r"^1\.6_SPIKE_(\w+)$", "gd177", {"UP": "", "DOWN": "R180", "LEFT": "R90", "RIGHT": "R270"}),
         (r"^FAKE_SPIKE_(\w+)$", "gd191", SIDES), (r"^GROUND_WAVY_SPIKE_(\w+)$", "gd61", ATTACHED),
         (r"^GROUND_SPIKE_(\w+)$", "gda_ground_spikes", ATTACHED), (r"^MEDIUM_SPIKE_(\w+)$", "gd103", ATTACHED), (r"^SMALL_SPIKE_(\w+)$", "gd39", ATTACHED), (r"^BASIC_BLOCK$", "gd1", None), (r"^SPIKE_(\w+)$", "gd8", SIDES), (r"^1\.6_WAVE_DECO_MIDDLE(_UD)?$", "gd157", {"_UD": "V"}),
         (r"^1\.6_WAVE_DECO_LFT(_UD)?$", "gd158", {"_UD": "V"}), (r"^1\.6_WAVE_DECO_RGT(_UD)?$", "gd159", {"_UD": "V"}),
         (r"^BREAKABLE_BRICK$", "gd143", None), (r"^WAVY_SLAB_SINGLE_TOP$", "fd_wavy_top_s", None))
# GDA sprites that are objects here: sprite -> (object, cell of GDA's position from the canvas's left)
DECO_SPRITES = {"BIG_BG_SPIKES": ("gd18", 32), "MEDIUM_BG_SPIKES": ("gd19", 32), "SMALL_BG_SPIKES": ("gd20", 16),
                "TINY_BG_SPIKES": ("gd21", 0)}
DECO_LAYERS = ["Layer 1", "Layer 2", "Layer 3"]
# position (8 px steps) -> the layers objects can go on, first choice first
OBJECT_LAYERS = {(0, 0): ["Layer 3", "Layer 2", "Layer 1"], (1, 0): ["Layer 1 +8x", "Layer 2 +8x"],
                 (0, 1): ["Layer 1 +8y", "Layer 2 +8y"], (1, 1): ["Layer 1 +8x +8y", "Layer 2 +8x +8y"]}


def tiled_matrix(flags):
    """the 2x2 matrix (y down) of Tiled's flip flags"""
    a, b, c, d = (0, 1, 1, 0) if flags & FLIP_D else (1, 0, 0, 1)
    if flags & FLIP_H:
        a, b = -a, -b
    if flags & FLIP_V:
        c, d = -c, -d
    return (a, b, c, d)


ORIENTATIONS = [h | v | d for d in (0, FLIP_D) for h in (0, FLIP_H) for v in (0, FLIP_V)]
TO_FLAGS = {tiled_matrix(f): f for f in ORIENTATIONS}


def combine(outer, inner):
    """the flags of a piece turned by `inner`, then by `outer`"""
    a, b, c, d = tiled_matrix(outer)
    e, f, g, h = tiled_matrix(inner)
    return TO_FLAGS[(a * e + b * g, a * f + b * h, c * e + d * g, c * f + d * h)]


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


def indices(tiles, chr_data):
    """GDA's colour indices of a metatile"""
    rows = [[0] * 16 for _ in range(16)]
    for q, (tile, h, v, _pal) in enumerate(tiles):
        raw = chr_data[tile * 32:tile * 32 + 32]
        for y in range(8):
            for x in range(8):
                rows[(q // 2) * 8 + (7 - y if v else y)][(q % 2) * 8 + (7 - x if h else x)] = \
                    (raw[y * 4 + x // 2] >> (4 * (x & 1))) & 15
    return rows


def turn_of(match, names):
    """the flags a named direction asks for, or None if the name is unknown"""
    if names is None or isinstance(names, str):
        return TURN[names or ""]
    return TURN[names[match.group(1)]] if match.group(1) in names else (0 if match.group(1) is None else None)


def family(piece):
    """-> (object key, flags) of a piece of Famidash's families, or None"""
    for prefix, keys in BLOCK_FAMILIES.items():
        if piece.startswith(prefix):
            for pattern, shape, names in BLOCK_NAMES:
                m = re.match(pattern, piece[len(prefix):])
                if m:
                    flags = turn_of(m, names)
                    return None if flags is None else (keys[shape], flags)
            return None
    for pattern, key, names in NAMED:
        m = re.match(pattern, piece)
        if m:
            flags = turn_of(m, names)
            return None if flags is None else (key, flags)
    return None


_drawn = None


def choose(piece, raw):
    """-> (object key or None, Tiled flags, "exact" / "family")"""
    global _drawn
    if _drawn is None:
        _drawn = [(k, our_art(k)) for k in GO.DRAWN if our_art(k)]
    # names first: a family piece can have the same pattern as another colour's object (black / dark fill)
    found = family(piece)
    if found is not None and found[0] in GO.OBJECTS:
        return found[0], found[1], "family"
    art = [[IA.FILLED_COLOURS.get(v, 2) for v in r] for r in raw]
    for key, rows in _drawn:
        for flags in ORIENTATIONS:
            if oriented(rows, flags) == art:
                return key, flags, "exact"
    return None, 0, ""


def main(level, target, spans, add=False):
    w, h, layers, sprites = gda.read_level(level)
    table = gda.metatiles()
    names = [n for n, _, _ in table]
    tiles_of = {n: t for n, t, _ in reversed(table)}
    chr_data = (gda.GDA / "graphics" / "tileset.chr").read_bytes()
    path = TMX / (target + ".tmx")
    text = path.open(newline="").read()
    old_w, root_h = (int(v) for v in re.search(r'<map [^>]*?\bwidth="(\d+)" height="(\d+)"', text).groups())
    whole = not spans
    root_w = w if whole else old_w
    if whole:
        spans = [(0, w, 0)]
        text = re.sub(r'(<map [^>]*?\bwidth=")\d+', r"\g<1>%d" % w, text, count=1)
        text = re.sub(r'(<layer [^>]*?\bwidth=")\d+', r"\g<1>%d" % w, text)
    drop = root_h - GO.ROWS_BELOW - h
    out = {n: [0] * (root_w * root_h) for n in re.findall(r'<layer [^>]*name="([^"]+)"', text)}
    chosen, count, missing, snapped, full = {}, Counter(), Counter(), Counter(), Counter()
    if add:
        for m in re.finditer(r'<layer [^>]*name="([^"]+)"[^>]*>\s*<data encoding="csv">(.*?)</data>', text, flags=re.S):
            out[m.group(1)] = [int(v) for v in m.group(2).replace(chr(10), "").replace(chr(13), "").split(",") if v.strip()]

    def piece_of(g):
        piece = names[g - 1]
        if piece not in chosen:
            chosen[piece] = choose(piece, indices(tiles_of[piece], chr_data))
        return piece, chosen[piece]

    for src, dst in (("l2", "Layer 1"), ("l1", "Layer 2")):
        for a, b, to in spans:
            for col in range(a, b):
                for row in range(h):
                    g = layers[src][row][col]
                    if g <= 1:
                        continue
                    piece, (key, flags, _how) = piece_of(g)
                    if key is None:
                        missing[piece] += 1
                        continue
                    count[piece] += 1
                    out[dst][(row + drop) * root_w + to + col - a] = (GO.OBJECTS[key][0] + 1) | flags

    objects = []
    sprite_names = gda.sprite_types()
    for o in sprites:
        name = sprite_names[o["gid"] - gda.SPRITE_GID] if o["gid"] >= gda.SPRITE_GID else None
        if name in DECO_SPRITES:
            span = next(((a, b, to) for a, b, to in spans if a <= o["x"] // 16 < b), None)
            if span is None:
                continue
            key, cell = DECO_SPRITES[name]
            chosen[name] = (key, 0, "deco")
            h2 = GO.OBJECTS[key][2][1]
            flags = (FLIP_H if o["props"].get("hflip") == "true" else 0) | (FLIP_V if o["props"].get("vflip") == "true" else 0)
            y = o["y"] + drop * 16 + (h2 - 16 if flags & FLIP_V else 0)
            objects.append((name, key, flags, o["x"] - cell + (span[2] - span[0]) * 16, y, True))
            continue
        if o["gid"] >= gda.SPRITE_GID or o["gid"] <= 1:
            continue
        x, y = o["x"], o["y"] + drop * 16
        span = next(((a, b, to) for a, b, to in spans if a <= x // 16 < b), None)
        if span is None:
            continue
        piece, (key, flags, _how) = piece_of(o["gid"])
        if key is None:
            missing[piece] += 1
            continue
        flags = combine(o["raw_gid"] & (FLIP_H | FLIP_V | FLIP_D), flags)
        objects.append((piece, key, flags, x + (span[2] - span[0]) * 16, y, False))

    def build(on_grid):
        """the TMX text with the tile layers and these objects; those in on_grid go on whole cells"""
        layer_data = {n: list(d) for n, d in out.items()}
        placed, moved, no_layer = Counter(), Counter(), Counter()
        for i, (piece, key, flags, x, y, deco) in enumerate(objects):
            step = 16 if i in on_grid else 8
            x2, y2 = round(x / step) * step // 8, round(y / step) * step // 8
            moved[int(max(abs(x2 * 8 - x), abs(y2 * 8 - y)))] += 1
            col, row = x2 >> 1, y2 >> 1
            if not (0 <= col < root_w and 0 <= row < root_h):
                no_layer[piece] += 1
                continue
            for layer in (DECO_LAYERS if deco and not (x2 & 1 or y2 & 1) else OBJECT_LAYERS[(x2 & 1, y2 & 1)]):
                if not layer_data[layer][row * root_w + col]:
                    layer_data[layer][row * root_w + col] = (GO.OBJECTS[key][0] + 1) | flags
                    placed[piece] += 1
                    break
            else:
                no_layer[piece] += 1

        def fill(m):
            name = re.search(r'name="([^"]+)"', m.group(1)).group(1)
            d = layer_data[name]
            return m.group(1) + m.group(2) + "\n" + ",\n".join(
                ",".join(str(v) for v in d[y * root_w:(y + 1) * root_w]) for y in range(root_h)) + "\n" + m.group(4)

        return re.sub(r'(<layer [^>]*>\s*)(<data encoding="csv">)(.*?)(</data>)', fill, text, flags=re.S), placed, moved, no_layer

    def fits(tmx_text):
        root = ET.fromstring(tmx_text)
        try:
            LO.compile_objects(target, LO.tmx_objects(root), int(root.get("width")), int(root.get("height")) - GO.ROWS_BELOW, {})
        except SystemExit as e:
            if "no metatile id left" in str(e):
                return False
            raise
        return True

    # A level has 256 cell types and off-grid objects make the most: the rarest ones go on whole cells
    # until the level fits
    how_common = Counter((key, flags, round(x / 8) & 1, round(y / 8) & 1) for _p, key, flags, x, y, _d in objects)
    rarest = sorted((i for i, (_p, _k, _f, x, y, _d) in enumerate(objects) if x % 16 or y % 16),
                    key=lambda i: how_common[(objects[i][1], objects[i][2], round(objects[i][3] / 8) & 1, round(objects[i][4] / 8) & 1)])
    low, high = 0, len(rarest)
    if not fits(build(set())[0]):
        while high - low > 1:
            mid = (low + high) // 2
            if fits(build(set(rarest[:mid]))[0]):
                high = mid
            else:
                low = mid
    else:
        high = 0
    final, placed, snapped, full = build(set(rarest[:high]))
    count.update(placed)
    if not add:
        final = re.sub(r'(<objectgroup [^>]*name="SP"[^>]*>).*?(</objectgroup>)', r'\1\2', final, flags=re.S)
    with path.open("w", newline="") as f:
        f.write(final)
    flag_names = {FLIP_H: "H", FLIP_V: "V", FLIP_D: "D"}
    for piece, n in count.most_common():
        key, flags, how = chosen[piece]
        print("%5d %-32s %-20s %-4s %s" % (n, piece, key, "".join(c for f, c in flag_names.items() if flags & f), how))
    print("placed %d cells" % sum(count.values()))
    print("object layer, moved (px: objects): %s; rarest %d of %d off-grid put on whole cells to fit the level's cell types"
          % (dict(sorted(snapped.items())), high, len(rarest)))
    print("no free layer: %s" % dict(full))
    print("not placed: %s" % dict(missing))


if __name__ == "__main__":
    spans = []
    add = "--add" in sys.argv
    for arg in [a for a in sys.argv[3:] if a != "--add"]:
        a, b, to = re.fullmatch(r"(\d+)-(\d+):(\d+)", arg).groups()
        spans.append((int(a), int(b), int(to)))
    main(sys.argv[1], sys.argv[2], spans, add)
