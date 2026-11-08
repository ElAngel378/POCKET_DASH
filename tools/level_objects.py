"""Levels made of objects (docs/PLAN_1.6.md, phase 0).

A level's TMX places objects from levels/chr_data/objects/gd_objects.tsx (tools/gd_objects.py) on
tile layers: one click per object, rotated / flipped with Tiled's own buttons. Objects stack: put
them on several layers: a higher number draws on top (Layer 1 under Layer 2 under Layer 3). A layer
may be shifted by 8 px (Layer Properties, Offset X / Y = 8, named Layer 1 +8x and so on): its objects
sit half a block off the grid, like GD's. The SP layer keeps the sprites.

For now the game still draws Famidash metatiles: the build turns the objects back into the
metatile map (compile_tmx), through the table tools/gd_objects.py FAMIDASH.

    python tools/level_objects.py tileset            levels/chr_data/objects/ (tileset + images)
    python tools/level_objects.py convert [level..]  Famidash TMX -> object TMX, in place
    python tools/level_objects.py check [level..]    convert, compile back, compare (no writes)

An object's picture is taken from the Famidash metatiles it makes up: each 8x8 quarter of a
metatile shows the topmost object covering it. The same object must look the same wherever
Famidash uses it (checked).
"""
import sys
import re
import xml.etree.ElementTree as ET
from itertools import combinations
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_bg_tiles as BG
import gd_objects as GO

TMX_DIR = ROOT / "levels" / "chr_data" / "tmx"
OBJ_DIR = ROOT / "levels" / "chr_data" / "objects"
TSX = OBJ_DIR / "gd_objects.tsx"
TILESET_NAME = "gd_objects"
SPRITES_FIRSTGID = 10001     # the sprites tileset after gd_objects (tile ids up to 9999)
FLAGS = GO.FLIP_H | GO.FLIP_V | GO.FLIP_D | 0x10000000
TF_NAME = {v: k for k, v in GO.TRANSFORMS.items()}
KEY_OF_TID = {v[0]: k for k, v in GO.OBJECTS.items()}


# ---------------------------------------------------------------- Famidash metatiles

_sheet = None


def sheet():
    """GB tile pixels: sections of bg_tiles.png, metatile -> sheet tiles, palettes"""
    global _sheet
    if _sheet is None:
        tiles = BG.read_png()
        sec = {name: tiles[row * 16: row * 16 + count] for name, row, count in BG.SECTIONS}
        src = (ROOT / "include" / "famidash_metatiles_dmg.c").read_text()
        a = src.index("const uint8_t metatiles[FAMIDASH_NUM_METATILES][4]")
        mts = [list(map(int, m)) for m in re.findall(r'\{ (\d+), (\d+), (\d+), (\d+) \}', src[a:src.index("};", a)])]
        fsrc = (ROOT / "include" / "famidash_metatiles.c").read_text()
        a = fsrc.index("famidash_metatile_palettes[FAMIDASH_NUM_METATILES] =")
        pal = list(map(int, re.findall(r'\d+', fsrc[fsrc.index("{", a):fsrc.index("};", a)])))
        names = {int(v): k for k, v in re.findall(r'#define FAMIDASH_MT_(\w+) (\d+)', fsrc)}
        _sheet = (tiles, sec, mts, pal, names)
    return _sheet


def level_flags(name, grid=None):
    """The Famidash tile sets a level uses (tools/build_bg_tiles.py LEVEL_SETS), and "NS" when it
    has no big saws (metatile 120 is then not a saw centre)"""
    sets = BG.LEVEL_SETS.get(name, {})
    f = set()
    if sets.get("blocks") == "B":
        f.add("B")
    if sets.get("spikes") in ("B", "C"):
        f.add("S" + sets["spikes"])
    if grid is not None and not any(m in BG.BIG_SAW_PARTS for row in grid for m in row):
        f.add("NS")
    return f


def quarter_tiles(m, flags):
    """The 4 tiles metatile m shows in a level with these flags, as 64-pixel tuples"""
    tiles, sec, mts, _, _ = sheet()
    out = []
    for q in range(4):
        st = BG.SAW_METATILES.get(m, (None,) * 4)[q]
        if m == BG.SAW_CENTER_MT and "NS" in flags:
            st = None
        t = mts[m][q]
        if st is not None:
            px = tiles[11 * 16 + st]            # SAWS_0, as drawn (CGB)
        elif "SB" in flags and any(o[:2] == (m, q) for o in BG.SPIKES_B_OVERRIDES):
            px = sec["SPIKES_B_CGB"][[o for o in BG.SPIKES_B_OVERRIDES if o[:2] == (m, q)][0][2]]
        elif "SB" not in flags and any(o[:2] == (m, q) for o in BG.SPIKES_A_OVERRIDES):
            px = sec["SPIKES_A_EXTRA"][[o for o in BG.SPIKES_A_OVERRIDES if o[:2] == (m, q)][0][2]]
        elif "B" in flags and t in BG.BLOCKS_B_SLOTS:
            px = sec["BLOCKS_B_CGB"][BG.BLOCKS_B_SLOTS.index(t)]
        elif "B" in flags and t in BG.BLOCKS_B2_SLOTS:
            px = sec["BLOCKS_B2_CGB"][BG.BLOCKS_B2_SLOTS.index(t)]
        elif "SB" in flags and t in BG.SPIKES_B_SLOTS:
            px = sec["SPIKES_B_CGB"][BG.SPIKES_B_SLOTS.index(t)]
        elif "SC" in flags and t in BG.SPIKES_C_SLOTS:
            px = sec["SPIKES_C"][BG.SPIKES_C_SLOTS.index(t)]
        else:
            px = sec["BASE"][t]
        out.append(tuple(v for row in px for v in row))
    return out


BLANK = (0,) * 64


def entry_for(m, flags):
    """FAMIDASH entry (variant, parts) of metatile m in a level with these flags"""
    vs = [v for v in ("B", "SB", "SC", "NS") if v in flags and (m, v) in GO.FAMIDASH]
    if len(vs) > 1:
        raise SystemExit("metatile %d: more than one variant applies (%s)" % (m, vs))
    v = vs[0] if vs else None
    if (m, v) not in GO.FAMIDASH:
        raise SystemExit("metatile %d (%s) has no objects in tools/gd_objects.py" % (m, sheet()[4].get(m)))
    return v, GO.FAMIDASH[(m, v)]


# ---------------------------------------------------------------- objects

def size(key, flags=0):
    w, h = GO.OBJECTS[key][2]
    return (h, w) if flags & GO.FLIP_D else (w, h)


_drawn = None


def drawn_art():
    """key -> {object quarter: 64 pixels (GB colours)} of the objects with art of their own
    (GO.DRAWN, levels/chr_data/objects/art); quarters the art leaves empty are not in it"""
    global _drawn
    if _drawn is None:
        _drawn = {}
        for key in GO.DRAWN:
            im = Image.open(OBJ_DIR / "art" / ("%s.png" % key))
            w, h = GO.OBJECTS[key][2]
            if im.size != (w, h) or im.mode != "P":
                raise SystemExit("art/%s.png: must be a %dx%d palette PNG (colours 0..3)" % (key, w, h))
            px = im.load()
            qs = {}
            for qx in range(w // 8):
                for qy in range(h // 8):
                    p = tuple(px[qx * 8 + i % 8, qy * 8 + i // 8] & 3 for i in range(64))
                    if any(p) or key in GO.OUTLINE:
                        qs[(qx, qy)] = p
            _drawn[key] = qs
    return _drawn


_spin = {}


def spin_frames(key):
    """The art of a spinning object, GO.SPIN_FRAMES frames: [{object quarter: 64 pixels}], every
    quarter in each. The whole wheel is made of its pieces, turned by a part of its rotational
    symmetry (one tooth), and the piece cut out of it."""
    if key in _spin:
        return _spin[key]
    w, h = GO.OBJECTS[key][2]
    src = Image.open(OBJ_DIR / "art" / ("%s.png" % key))
    base = Image.new("L", (w, h))
    base.putdata([v & 3 for v in src.getdata()])
    pieces = GO.SPIN[key]
    if pieces == 4:
        whole = Image.new("L", (2 * w, 2 * h))
        whole.paste(base, (0, 0))
        whole.paste(base.rotate(-90), (w, 0))
        whole.paste(base.rotate(180), (w, h))
        whole.paste(base.rotate(90), (0, h))
    elif pieces == 2:
        whole = Image.new("L", (2 * w, h))
        whole.paste(base, (0, 0))
        whole.paste(base.rotate(180), (w, 0))
    else:
        whole = base
    if whole.width != whole.height:
        raise SystemExit("%s: a spinning object must be square" % key)
    solid = [v > 0 for v in whole.getdata()]
    step = 90
    for n in (16, 12, 10, 9, 8, 6, 5, 4, 3, 2):
        turned = [v > 0 for v in whole.rotate(360 / n).getdata()]
        if sum(a != b for a, b in zip(solid, turned)) <= 0.2 * sum(solid):
            step = 360 / n
            break
    out = []
    for f in range(GO.SPIN_FRAMES):
        img = whole.rotate(-step * f / GO.SPIN_FRAMES).crop((0, 0, w, h)).load()
        out.append({(qx, qy): tuple(img[qx * 8 + i % 8, qy * 8 + i // 8] for i in range(64))
                    for qx in range(w // 8) for qy in range(h // 8)})
    _spin[key] = out
    return out


_masks = {}


def mask(key, flags=0):
    """Occupied 8x8 quarters of a placed object (object space, after the transform)"""
    if (key, flags) in _masks:
        return _masks[(key, flags)]
    w, h = GO.OBJECTS[key][2]
    m = GO.OBJECTS[key][3]
    if key in GO.SPIN:          # all that its frames cover
        m = {q for frame in spin_frames(key) for q, px in frame.items() if any(px)}
    elif m is None and key in GO.DRAWN and key not in GO.OUTLINE:
        m = set(drawn_art()[key])
    qs = m if m is not None else {(x, y) for x in range(w // 8) for y in range(h // 8)}
    out = turned(qs, w // 8, h // 8, flags)
    _masks[(key, flags)] = out
    return out


def turned(quarters, cols, rows, flags):
    """8x8 quarters of an object of cols x rows quarters, after the transform"""
    out = set()
    for x, y in quarters:
        W, H = cols, rows
        if flags & GO.FLIP_D:
            x, y, W, H = y, x, H, W
        if flags & GO.FLIP_H:
            x = W - 1 - x
        if flags & GO.FLIP_V:
            y = H - 1 - y
        out.add((x, y))
    return out


def cells_of(key, x, y, flags):
    """{(cell x, cell y): part} for an object placed with its top left at (x, y) px"""
    out = {}
    for qx, qy in mask(key, flags):
        cx, cy = (x + 8 * qx) // 16, (y + 8 * qy) // 16
        out[(cx, cy)] = (key, x - cx * 16, y - cy * 16, TF_NAME[flags])
    return out


def parts_quarters(parts):
    """cell quarter -> (index of the topmost part covering it, object quarter)"""
    top = {}
    for i, (key, dx, dy, tf) in enumerate(parts):
        for qx, qy in mask(key, GO.TRANSFORMS[tf]):
            cq = ((dx + 8 * qx) // 8, (dy + 8 * qy) // 8)
            if 0 <= cq[0] < 2 and 0 <= cq[1] < 2:
                top[cq] = (i, (qx, qy))
    return top


def object_art(levels):
    """(key, transform) -> {object quarter: tile pixels}, key -> palette, from the levels' metatile
    maps. Conflicts (an object that looks different in two places) are listed."""
    _, _, _, pal, names = sheet()
    art, opal, problems = {}, {}, []
    for name, grid in levels.items():
        flags = level_flags(name, grid)
        for m in sorted({m for row in grid for m in row}):
            v, parts = entry_for(m, flags)
            qt = quarter_tiles(m, flags)
            top = parts_quarters(parts)
            for q in range(4):
                cq = (q % 2, q // 2)
                if cq not in top:
                    if qt[q] != BLANK:
                        problems.append("%s: metatile %d %s quarter %s shows art no object covers"
                                        % (name, m, names.get(m), cq))
                    continue
                i, oq = top[cq]
                key, _, _, tf = parts[i]
                if key in GO.INVISIBLE:
                    continue
                a = art.setdefault((key, tf), {})
                if a.get(oq, qt[q]) != qt[q]:
                    problems.append("%s: %s %s quarter %s differs in metatile %d %s"
                                    % (name, key, tf or "-", oq, m, names.get(m)))
                a[oq] = qt[q]
                # Famidash colours a whole metatile with one palette (the big saw's centre has
                # its own): the object keeps the first one, for the Tiled picture
                opal.setdefault(key, pal[m])
    for (m, v), parts in GO.FAMIDASH_ALT.items():
        qt = quarter_tiles(m, set())
        for cq, (i, oq) in parts_quarters(parts).items():
            art.setdefault((parts[i][0], parts[i][3]), {})[oq] = qt[cq[0] + 2 * cq[1]]
            opal.setdefault(parts[i][0], pal[m])
    for key, qs in drawn_art().items():
        art[(key, "")] = dict(qs)
        for q in mask(key):
            art[(key, "")].setdefault(q, (0,) * 64)
        opal[key] = GO.drawn_pal(key)
    return art, opal, problems


# ---------------------------------------------------------------- TMX

def read_old_grid(root):
    """The Famidash metatile layer of an old-format TMX: rows of metatile ids"""
    w, h = int(root.get("width")), int(root.get("height"))
    layer = None
    for l in root.findall("layer"):
        if l.get("name") in ("Tile Layer 1", "background", "BG", "Level"):
            layer = l
            break
    if layer is None:
        layer = root.find("layer")
    vals = [int(x) for x in layer.find("data").text.replace("\n", "").split(",") if x.strip()]
    return layer, [[max(0, v - 1) for v in vals[y * w:(y + 1) * w]] for y in range(h)]


def is_object_tmx(root):
    return any(ts.get("source", "").endswith(TILESET_NAME + ".tsx") for ts in root.findall("tileset"))


def convert_grid(name, grid):
    """Famidash metatile rows -> [(key, x, y, flags)] (TMX pixel space), in stacking order"""
    flags = level_flags(name, grid)
    objs = {}
    for cy, row in enumerate(grid):
        for cx, m in enumerate(row):
            for i, (key, dx, dy, tf) in enumerate(entry_for(m, flags)[1]):
                o = (key, cx * 16 + dx, cy * 16 + dy, GO.TRANSFORMS[tf])
                objs[o] = min(objs.get(o, i), i)
    return sorted(objs, key=lambda o: (objs[o], o[1], o[2]))


def compile_objects(name, objs, w, h, novel=None):
    """[(key, x, y, flags)] in drawing order (later = on top) -> (metatile rows (h x w), errors).
    novel: a dict to make the cells no Famidash metatile shows in (tools/compose.py: metatile id ->
    (CGB quarters, DMG quarters, collision)); without it they are errors."""
    cells = {}
    for z, (key, x, y, flags) in enumerate(objs):
        for c, part in cells_of(key, x, y, flags).items():
            cell = cells.setdefault(c, {})
            cell[part] = max(cell.get(part, z), z)
    has_big_saw = any(o[0] == "gd88" for o in objs)
    lflags = level_flags(name) | (set() if has_big_saw else {"NS"})
    inv = {}
    for (m, v), parts in list(GO.FAMIDASH.items()) + list(GO.FAMIDASH_ALT.items()):
        k = (frozenset((key, dx, dy, tf) for key, dx, dy, tf in parts), v)
        if k in inv:
            raise SystemExit("tools/gd_objects.py: metatiles %d and %d are the same objects" % (inv[k], m))
        inv[k] = m

    def lookup(fp):
        found = [inv[(fp, v)] for v in lflags if (fp, v) in inv]
        if not found and (fp, None) in inv:
            m = inv[(fp, None)]
            # a metatile that looks different in this level's tile set cannot stand for these objects
            if not any((m, v) in GO.FAMIDASH for v in lflags):
                found = [m]
        return found

    grid = [[0] * w for _ in range(h)]
    errors = []
    new = []
    for (cx, cy), zs in sorted(cells.items()):
        if not (0 <= cx < w and 0 <= cy < h):
            continue        # the part of an object hanging off the map
        parts = set(zs)
        found = lookup(frozenset(parts))
        if not found:
            # An object poking into this cell from a neighbour (a saw, a spike half a block off)
            # is hidden by the objects that are wholly in it, and of two such objects the one on
            # top shows: keep the most parts that match, the topmost ones first
            loose = [p for p in parts if cells_of(p[0], p[1], p[2], GO.TRANSFORMS[p[3]]).keys() != {(0, 0)}]
            whole = parts - set(loose)
            for n in range(len(loose) - 1, -1, -1):
                hits = []
                for keep in combinations(loose, n):
                    if not whole and not keep:
                        continue    # nothing left to hide the rest: not hidden, a cell of its own
                    for m in lookup(frozenset(whole | set(keep))):
                        hits.append((max([zs[p] for p in keep], default=-1), m))
                if hits:
                    best = max(hits)[0]
                    found = sorted({m for z, m in hits if z == best})
                    break
        if len(found) != 1:
            if novel is not None and not found:
                new.append((cx, cy, [p for p in sorted(parts, key=lambda p: zs[p])]))
                grid[cy][cx] = -1
                continue
            errors.append("cell %d, %d (x %d px): no Famidash metatile for %s"
                          % (cx, cy, cx * 16, sorted(parts)))
            continue
        grid[cy][cx] = found[0]
    if new:
        made, warn = composer().make(name, grid, new, lookup)
        novel.update(made)
        for w_ in warn:
            print("  warning: " + w_)
    return grid, errors


_composer = None


def composer():
    global _composer
    if _composer is None:
        import compose
        _composer = compose.Composer()
    return _composer


def tmx_objects(root):
    """[(key, x, y, flags)] of an object TMX's tile layers (all but SP)"""
    first = None
    for ts in root.findall("tileset"):
        if ts.get("source", "").endswith(TILESET_NAME + ".tsx"):
            first = int(ts.get("firstgid"))
    w = int(root.get("width"))
    objs = []
    for layer in root.findall("layer"):
        if layer.get("name") == "SP":
            continue
        ox = int(float(layer.get("offsetx", 0)))
        oy = int(float(layer.get("offsety", 0)))
        if ox % 8 or oy % 8:
            raise SystemExit("layer %s: offset %d, %d is not a multiple of 8 px" % (layer.get("name"), ox, oy))
        vals = [int(v) for v in layer.find("data").text.replace("\n", "").split(",") if v.strip()]
        for i, g in enumerate(vals):
            if not g:
                continue
            tid = (g & ~FLAGS) - first
            if tid not in KEY_OF_TID:
                raise SystemExit("layer %s, cell %d, %d: tile %d is not an object of %s"
                                 % (layer.get("name"), i % w, i // w, g & ~FLAGS, TILESET_NAME))
            key = KEY_OF_TID[tid]
            fl = g & (GO.FLIP_H | GO.FLIP_V | GO.FLIP_D)
            ow, oh = size(key, fl)
            objs.append((key, (i % w) * 16 + ox, (i // w + 1) * 16 + oy - oh, fl))
    return objs


def compile_tmx(path, novel=None):
    """The metatile rows of an object TMX (novel: see compile_objects)"""
    root = ET.parse(path).getroot()
    w, h = int(root.get("width")), int(root.get("height"))
    grid, errors = compile_objects(path.stem.lower(), tmx_objects(root), w, h, novel)
    if errors:
        raise SystemExit("%s:\n  %s" % (path.name, "\n  ".join(errors[:20])))
    return grid


def layer_name(ox, oy, n, big=False):
    """Layer 1: objects bigger than a block. Layer 2: the others, Layer 3 where they share a cell.
    n: how many layers with that offset there are already"""
    num, extra = (1, n) if big else (2, 0) if not n else (3, n - 1)
    nm = "Layer %d" % num + "".join(" %+d%s" % (v, a) for v, a in ((ox, "x"), (oy, "y")) if v)
    return nm + ("" if not extra else " (%d)" % (extra + 1))


def object_layers(objs, w, h):
    """Spread objects over tile layers: one per 8 px offset, more where objects share a cell.
    -> [[name, offset x, offset y, {cell index: object}]], bottom layer first"""
    layers = []
    for o in objs:
        add_to_layer(layers, o, w, h)
    return layers


def add_to_layer(layers, o, w, h, above=False):
    """Put object o in the first layer with its offset and a free cell (above: a new layer on top)"""
    key, x, y, fl = o
    ow, oh = size(key, fl)
    ox, oy = x % 16, (y + oh) % 16
    col, row = (x - ox) // 16, (y + oh - oy) // 16 - 1
    # an object hanging off the top or bottom edge: on a layer shifted to keep it on the map
    while row < 0:
        oy, row = oy - 16, row + 1
    while row >= h:
        oy, row = oy + 16, row - 1
    while col >= w:
        ox, col = ox + 16, col - 1
    if not (0 <= col < w and 0 <= row < h):
        raise SystemExit("object %s at %d, %d is outside the map" % (key, x, y))
    idx = row * w + col
    # objects bigger than a block (saws) get layers of their own, under the others: where a block
    # and a saw meet, the block shows (compile_objects)
    big = max(ow, oh) > 16
    for L in ([] if above else layers):
        if L[1:3] == [ox, oy] and L[4] == big and idx not in L[3]:
            break
    else:
        n = sum(1 for L in layers if L[1:3] == [ox, oy] and L[4] == big)
        L = [layer_name(ox, oy, n, big), ox, oy, {}, big]
        layers.append(L)
    L[3][idx] = o


def stacked(layers):
    """The layers bottom first: Layer 1, 2, 3"""
    return sorted(layers, key=lambda L: L[0][:7])


def drawn_order(layers):
    """The objects in the order Tiled draws them: layer by layer, each row by row (later = on top)"""
    return [L[3][i] for L in stacked(layers) for i in sorted(L[3])]


def convert_tmx(path, write=True):
    """Famidash TMX -> object TMX. Returns (grid, compiled grid, errors)."""
    tree = ET.parse(path)
    root = tree.getroot()
    if is_object_tmx(root):
        return None
    w, h = int(root.get("width")), int(root.get("height"))
    old_layer, grid = read_old_grid(root)
    name = path.stem.lower()
    layers = object_layers(convert_grid(name, grid), w, h)
    flags = level_flags(name, grid)
    for _ in range(40):
        objs = drawn_order(layers)
        back, errors = compile_objects(name, objs, w, h)
        wrong = [(x, y) for y in range(h) for x in range(w) if back[y][x] != grid[y][x]]
        if not wrong:
            break
        # overlapping objects where Famidash shows the other one on top: move that one up
        moved = set()
        for x, y in wrong:
            want = set(entry_for(grid[y][x], flags)[1])
            for o in objs:
                part = cells_of(o[0], o[1], o[2], o[3]).get((x, y))
                if part and (part[0], part[1], part[2], part[3]) in want and o not in moved:
                    for L in layers:
                        L[3] = {i: p for i, p in L[3].items() if p != o}
                    add_to_layer(layers, o, w, h, above=True)
                    moved.add(o)
        layers = [L for L in layers if L[3]]
        if not moved:
            break
    for _ in range(4):
        if not wrong:
            break
        # what no stacking order gives: the metatile's other objects (FAMIDASH_ALT)
        for x, y in wrong:
            alt = GO.FAMIDASH_ALT.get((grid[y][x], entry_for(grid[y][x], flags)[0]))
            for key, dx, dy, tf in alt or []:
                add_to_layer(layers, (key, x * 16 + dx, y * 16 + dy, GO.TRANSFORMS[tf]), w, h)
        back, errors = compile_objects(name, drawn_order(layers), w, h)
        wrong = [(x, y) for y in range(h) for x in range(w) if back[y][x] != grid[y][x]]
    for x, y in wrong:
        errors.append("cell %d, %d: metatile %d comes back as %d" % (x, y, grid[y][x], back[y][x]))
    if not write or errors:
        return grid, back, errors

    # tilesets: gd_objects first (ids up to 9999), the sprites after it
    for ts in root.findall("tileset"):
        if ts.get("name") == "famidash":
            root.remove(ts)
    sp_first = None
    for ts in root.findall("tileset"):
        if ts.get("name") == "sprites":
            sp_first = int(ts.get("firstgid"))
            ts.set("firstgid", str(SPRITES_FIRSTGID))
    new_ts = ET.Element("tileset", {"firstgid": "1", "source": "../objects/%s.tsx" % TILESET_NAME})
    root.insert(list(root).index(root.find("tileset")), new_ts)
    for layer in root.findall("layer"):
        if layer.get("name") == "SP" and sp_first is not None:
            d = layer.find("data")
            vals = [int(v) for v in d.text.replace("\n", "").split(",") if v.strip()]
            vals = [(v - sp_first + SPRITES_FIRSTGID) if v else 0 for v in vals]
            d.text = csv(vals, w)

    # object layers in place of the metatile layer
    pos = list(root).index(old_layer)
    root.remove(old_layer)
    next_id = int(root.get("nextlayerid", "1"))
    for L in reversed(stacked(layers) or [[layer_name(0, 0, 0), 0, 0, {}, False]]):
        el = ET.Element("layer", {"id": str(next_id), "name": L[0], "width": str(w), "height": str(h)})
        next_id += 1
        if L[1]:
            el.set("offsetx", str(L[1]))
        if L[2]:
            el.set("offsety", str(L[2]))
        d = ET.SubElement(el, "data", {"encoding": "csv"})
        d.text = csv([(1 + GO.OBJECTS[L[3][i][0]][0] | L[3][i][3]) if i in L[3] else 0 for i in range(w * h)], w)
        root.insert(pos, el)
    root.set("nextlayerid", str(next_id))
    ET.indent(tree, " ")
    tree.write(path, encoding="UTF-8", xml_declaration=True)
    return grid, back, errors


def csv(vals, w):
    return "\n" + ",\n".join(",".join(str(v) for v in vals[i:i + w]) for i in range(0, len(vals), w)) + "\n"


# ---------------------------------------------------------------- tileset

GREY = [(255, 255, 255, 0), (170, 170, 170, 255), (85, 85, 85, 255), (0, 0, 0, 255)]
# palette tints for the Tiled pictures (CGB palette 1 = ground, 2 = background deco, 5 = fake,
# 6 = background shades, 7 = player colour 2)
TINT = {0: (1, 1, 1), 1: (0.85, 0.7, 0.5), 2: (0.6, 0.75, 1.0), 5: (1.0, 0.6, 0.6),
        6: (0.55, 0.8, 1.0), 7: (0.6, 1.0, 0.8)}


def picture(key, art, opal):
    """The object at no rotation, as Tiled shows it"""
    w, h = GO.OBJECTS[key][2]
    tfs = [tf for (k, tf) in art if k == key]
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    if key in GO.INVISIBLE:
        for x in range(w):
            for y in range(h):
                if x in (0, w - 1) or y in (0, h - 1) or (x + y) % 4 == 0:
                    im.putpixel((x, y), (255, 0, 0, 160))
        return im
    if not tfs:
        return im
    tf = "" if "" in tfs else sorted(tfs)[0]
    fl = GO.TRANSFORMS[tf]
    pw, ph = size(key, fl)
    placed = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
    t = TINT.get(opal.get(key, 0), (1, 1, 1))
    for (qx, qy), px in art[(key, tf)].items():
        for i, v in enumerate(px):
            r, g, b, a = GREY[v]
            placed.putpixel((qx * 8 + i % 8, qy * 8 + i // 8), (int(r * t[0]), int(g * t[1]), int(b * t[2]), a))
    for q in mask(key, fl) - set(art[(key, tf)]):      # never seen (always covered): grey hatching
        for i in range(64):
            if (i % 8 + i // 8) % 3 == 0:
                placed.putpixel((q[0] * 8 + i % 8, q[1] * 8 + i // 8), (128, 128, 128, 120))
    # undo the transform: V, H, then the transpose
    if fl & GO.FLIP_V:
        placed = placed.transpose(Image.FLIP_TOP_BOTTOM)
    if fl & GO.FLIP_H:
        placed = placed.transpose(Image.FLIP_LEFT_RIGHT)
    if fl & GO.FLIP_D:
        placed = placed.transpose(Image.TRANSPOSE)
    return placed


def write_tileset(art, opal):
    (OBJ_DIR / "tiles").mkdir(parents=True, exist_ok=True)
    keys = sorted(GO.OBJECTS, key=lambda k: GO.OBJECTS[k][0])
    for k in keys:
        picture(k, art, opal).save(OBJ_DIR / "tiles" / ("%s.png" % k))
    mw = max(GO.OBJECTS[k][2][0] for k in keys)
    mh = max(GO.OBJECTS[k][2][1] for k in keys)
    L = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<tileset version="1.10" tiledversion="1.11.2" name="%s" tilewidth="%d" tileheight="%d" tilecount="%d" columns="0">'
         % (TILESET_NAME, mw, mh, len(keys)),
         ' <grid orientation="orthogonal" width="1" height="1"/>']
    for k in keys:
        tid, name, (w, h), _ = GO.OBJECTS[k]
        L += [' <tile id="%d" type="%s">' % (tid, k),
              '  <properties>', '   <property name="name" value="%s"/>' % name, '  </properties>',
              '  <image width="%d" height="%d" source="tiles/%s.png"/>' % (w, h, k), ' </tile>']
    L.append('</tileset>')
    TSX.write_text("\n".join(L) + "\n", encoding="utf-8")


# ---------------------------------------------------------------- main

def level_paths(names):
    """The levels' TMX files (all of them, or the named ones; the template only by name)"""
    ps = sorted(TMX_DIR.glob("*.tmx"))
    return [p for p in ps if p.stem.lower() in names or (not names and p.stem.lower() != "template")]


def level_grids(paths, old_only=False):
    """name -> metatile rows of each level (object TMX files compiled)"""
    out = {}
    for p in paths:
        root = ET.parse(p).getroot()
        if not is_object_tmx(root):
            out[p.stem.lower()] = read_old_grid(root)[1]
        elif not old_only:
            # the level's own metatiles (phase 2) are made of objects seen elsewhere: left out
            novel = {}
            grid = compile_tmx(p, novel)
            out[p.stem.lower()] = [[0 if m in novel else m for m in row] for row in grid]
    return out


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "check"
    names = [n.lower() for n in sys.argv[2:]]
    if cmd == "tileset":
        art, opal, problems = object_art(level_grids(level_paths([])))
        for p in problems:
            print("  " + p)
        write_tileset(art, opal)
        print("%s: %d objects" % (TSX.relative_to(ROOT), len(GO.OBJECTS)))
        return
    if cmd not in ("check", "convert"):
        raise SystemExit(__doc__)
    paths = level_paths(names)
    art, opal, problems = object_art(level_grids(paths, old_only=True))
    for p in problems:
        print("  art: " + p)
    bad = 0
    for p in paths:
        r = convert_tmx(p, write=(cmd == "convert" and not problems))
        if r is None:
            print("%-24s already objects" % p.stem)
            continue
        grid, back, errors = r
        n = sum(1 for row in grid for m in row if m)
        print("%-24s %6d cells %s" % (p.stem, n, "OK" if not errors else "%d errors" % len(errors)))
        for e in errors[:12]:
            print("    " + e)
        bad += bool(errors)
    if bad or problems:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
