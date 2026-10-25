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
