"""Reading Geometry Dash Advance (../geometry_dash_advance): its metatiles, sprite objects, levels
and art. Used to find which GDA object each of ours is, and to bring GDA objects over.

GDA has two kinds of level objects:
- metatiles (src/metatiles.c, the l1 / l2 tile layers): 16x16 pieces, like our catalog objects.
  Many are copies of one piece in another colour channel (COL1..4_, LBG_ prefixes).
- sprite objects (include/sprite_routines.h enum ObjTypes, the SP object layer): portals, orbs,
  pads, saws, spikes placed off the grid, decorations... A sprite object's Tiled gid is
  16385 + its ObjTypes value; SP objects with a metatile gid are metatiles placed as sprites.
"""
import re
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
GDA = ROOT.parent / "geometry_dash_advance"
LEVELS = GDA / "levels"
SPRITE_GID = 16385


def metatiles():
    """[(name, [(tile, h, v, palbank) x 4], collision)] in GDA metatile id order"""
    src = (GDA / "src" / "metatiles.c").read_text()
    consts = {"H": 1, "V": 1, "LBG_PAL": 2, "COL1_PAL": 3, "COL2_PAL": 4, "COL3_PAL": 5, "COL4_PAL": 6,
              "PLAYER_PAL": 7}
    num = lambda s: int(s, 0) if s[0].isdigit() else consts.get(s, 0)
    out = []
    for m in re.finditer(r'Metatile\("([^"]*)",\s*' + r",\s*".join([r"TILE\(([^)]*)\)"] * 4) + r",\s*(\w+)\)", src):
        tiles = []
        for t in m.groups()[1:5]:
            a = [x.strip() for x in t.split(",")]
            tiles.append((num(a[0]), num(a[2]), num(a[3]), num(a[1])))
        out.append((m.group(1), tiles, m.group(6)))
    return out


def sprite_types():
    """[ObjTypes name] in enum order (index = type id)"""
    src = (GDA / "include" / "sprite_routines.h").read_text()
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", re.search(r"enum ObjTypes\s*{(.*?)}", src, re.S).group(1), flags=re.S)
    return [n.strip() for n in body.split(",") if n.strip()]


def level_names():
    return sorted(p.stem for p in LEVELS.glob("*.tmx"))


def read_level(name):
    """GDA level -> (w, h, {layer name: rows of gids}, [sprite objects]). A sprite object:
    dict(gid, x, y (top left, px), w, h, props); gids keep their flip bits stripped."""
    root = ET.parse(LEVELS / (name + ".tmx")).getroot()
    w, h = int(root.get("width")), int(root.get("height"))
    layers = {}
    for layer in root.findall("layer"):
        v = [int(x) & 0x0FFFFFFF for x in layer.find("data").text.replace("\n", "").split(",") if x.strip()]
        layers[layer.get("name")] = [v[y * w:(y + 1) * w] for y in range(h)]
    objs = []
    for og in root.findall("objectgroup"):
        for o in og.findall("object"):
            if o.get("gid") is None:
                continue
            props = {p.get("name"): p.get("value") for p in o.findall("properties/property")}
            ow, oh = float(o.get("width", 16)), float(o.get("height", 16))
            objs.append(dict(gid=int(o.get("gid")) & 0x0FFFFFFF, raw_gid=int(o.get("gid")),
                             x=float(o.get("x")), y=float(o.get("y")) - oh, w=ow, h=oh,
                             rot=float(o.get("rotation", 0)), props=props, group=og.get("name")))
    return w, h, layers, objs


def cell_map(layers, w, h):
    """rows of GDA metatile ids (-1 = empty): l1 drawn over l2"""
    l1, l2 = layers["l1"], layers.get("l2")
    pick = lambda a, b: a if a > 1 else (b if b and b > 1 else 0)
    return [[pick(l1[y][x], l2[y][x] if l2 else 0) - 1 for x in range(w)] for y in range(h)]


_sheets = {}


def _sheet(name):
    if name not in _sheets:
        _sheets[name] = Image.open(LEVELS / name).convert("RGBA")
    return _sheets[name]


def metatile_image(mid):
    """GDA's own rendering of metatile mid (levels/metatiles.png, 16 per row), 16x16 RGBA"""
    x, y = (mid % 16) * 16, (mid // 16) * 16
    return _sheet("metatiles.png").crop((x, y, x + 16, y + 16))


def sprite_image(tid):
    """GDA's Tiled icon of sprite type tid (levels/sprites.png, 16 per row), 16x16 RGBA"""
    x, y = (tid % 16) * 16, (tid // 16) * 16
    return _sheet("sprites.png").crop((x, y, x + 16, y + 16))


# GBA OAM sizes: (shape, size) -> (w, h) in pixels
_OBJ_SIZES = {("SQUARE", 8): (8, 8), ("SQUARE", 16): (16, 16), ("SQUARE", 32): (32, 32), ("SQUARE", 64): (64, 64)}


def _metasprites():
    """{array name: [dict(w, h, x, y, id, hflip, vflip)]} from src/metaspr.c, and obj_sprites[] (by
    ObjTypes value: array name or None)"""
    src = (GDA / "src" / "metaspr.c").read_text(encoding="latin1")
    src = re.sub(r"//[^\n]*", "", src)
    arrays = {}
    for m in re.finditer(r"const u16 (\w+)\[\] = \{(.*?)\};", src, re.S):
        # split on the commas outside parentheses (CENTER(8, 8), PRIO_IDOFF(a, b, c))
        items = [x.strip() for x in re.split(r",(?![^(]*\))", m.group(2)) if x.strip()]
        parts = []
        i = 0
        while i + 7 <= len(items) and items[i] != "0xffff":
            a0, a1, a2, x, y, idoff = items[i:i + 6]
            i += 7
            sz = re.search(r"ATTR1_SIZE_(\d+)x(\d+)|ATTR1_SIZE_(\d+)\b", a1)
            if not sz:   # not an object metasprite (another layout)
                parts = None
                break
            w, h = (int(sz.group(1)), int(sz.group(2))) if sz.group(1) else (int(sz.group(3)),) * 2
            tid = re.search(r"ATTR2_ID\((\d+)\)", a2)
            off = re.search(r"PRIO_IDOFF\w*\(\s*\d+\s*,\s*\d+\s*,\s*(\w+)\s*\)", idoff)
            parts.append(dict(w=w, h=h, x=int(x, 0), y=int(y, 0),
                              id=(int(tid.group(1)) if tid else 0) + (int(off.group(1), 0) if off else 0),
                              hflip="ATTR1_HFLIP" in a1, vflip="ATTR1_VFLIP" in a1))
        if parts:
            arrays[m.group(1)] = parts
    table = re.search(r"obj_sprites\[\] = \{(.*?)\};", src, re.S).group(1)
    names = [x.strip() for x in table.split(",") if x.strip()]
    return arrays, [None if n == "NULL" else n for n in names]


def _chr_offsets():
    """[(rom tile, tiles)] by ObjTypes value (src/metaspr.c obj_chr_offset)"""
    src = (GDA / "src" / "metaspr.c").read_text(encoding="latin1")
    body = re.search(r"obj_chr_offset\[\]\[2\] = \{(.*?)\};", src, re.S).group(1)
    out = []
    for m in re.finditer(r'\w*SLOT\("[^"]*",\s*(\w+)\s*,\s*(\w+)\)', body):
        out.append((int(m.group(1), 0) if m.group(1)[0].isdigit() else None, int(m.group(2), 0) & 0xFFFF))
    return out


def sprite_art(tid):
    """GDA's art of sprite type tid as drawn (its metasprite over its tiles in graphics/sprites.chr):
    (image of GBA colour indices 0..15, 0 transparent, origin x, origin y) — the origin is where the
    object's top left (its Tiled x, y) is in the image. None: no art"""
    if not hasattr(sprite_art, "_c"):
        arrays, table = _metasprites()
        sprite_art._c = (arrays, table, _chr_offsets(), (GDA / "graphics" / "sprites.chr").read_bytes())
    arrays, table, chr_off, chr_data = sprite_art._c
    if tid >= len(table) or not table[tid] or table[tid] not in arrays or chr_off[tid][0] is None:
        return None
    parts = arrays[table[tid]]
    if not parts:
        return None
    base = chr_off[tid][0]
    x0 = min(p["x"] for p in parts); y0 = min(p["y"] for p in parts)
    x1 = max(p["x"] + p["w"] for p in parts); y1 = max(p["y"] + p["h"] for p in parts)
    img = Image.new("L", (x1 - x0, y1 - y0), 0)
    pix = img.load()
    for p in parts:
        tw = p["w"] // 8
        for ty in range(p["h"] // 8):
            for tx in range(tw):
                t = base + p["id"] + ty * tw + tx   # 1D mapping
                data = chr_data[t * 32:(t + 1) * 32]
                if len(data) < 32:
                    continue
                for yy in range(8):
                    for xx in range(8):
                        b = data[yy * 4 + xx // 2]
                        c = (b >> 4) if xx & 1 else (b & 15)
                        if not c:
                            continue
                        px, py = tx * 8 + xx, ty * 8 + yy
                        if p["hflip"]:
                            px = p["w"] - 1 - px
                        if p["vflip"]:
                            py = p["h"] - 1 - py
                        pix[p["x"] - x0 + px, p["y"] - y0 + py] = c
    return img, -x0, -y0
