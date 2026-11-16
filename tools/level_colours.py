"""Each level's colour table: the exact colours (RGB) its sky and ground take, from the TMX's map
properties (BG colour, Ground colour) and its BG / ground colour triggers. A trigger's engine id
and the level's start colours are positions in this table (up to 48 colours a level); the engine
draws the shades of a colour as fractions of it (SHADES, src/famidash_bg.c), like GDA's fades to
black. Every colour also has its nearest NES colour: its DMG / SGB theme (band)."""
import xml.etree.ElementTree as ET
from functools import lru_cache
from pathlib import Path

from gen_famidash_bg_tables import NES, to555

MAX_COLOURS = 48
DEFAULT_LEVEL = "#ff%06x" % NES[0x11]     # a level without BG / Ground colour
DEFAULT_TRIGGER = "#ff%06x" % NES[0x20]   # a trigger without Colour
BG_TILE, GROUND_TILE = 256, 257           # tmx2sprites BG_TRIGGER, GROUND_TRIGGER
SPRITES_FIRSTGID = 10001
# Shades as fractions of a colour (x / 256), the same in src/famidash_bg.c
# (border, body, shadow: BG SHADES = OLD, the default; GDA's are 177, 215, 136)
SHADES = {"sky_darker": 192, "border": 133, "body": 174, "shadow": 102,
          "ground_darker": 192, "grid_18": 159, "grid_9": 41, "grid_9_dark": 92}
# NES colours a colour's DMG / SGB theme is taken from (not Famidash's repeated blacks / toggles)
_NES_CHOICES = [i for i in range(48) if i not in (13, 14, 29, 30, 31, 45, 46, 47)]


def rgb(value):
    """'#ffRRGGBB' -> 0xRRGGBB"""
    return int(value[-6:], 16)


def nes(colour):
    """The nearest NES colour id a trigger can be (as tools/tmx2sprites.py nearest_colour)"""
    def distance(n):
        return sum((((colour >> s) & 255) - ((NES[n] >> s) & 255)) ** 2 for s in (16, 8, 0))
    return min(_NES_CHOICES, key=distance)


def band(colour):
    """DMG theme (bg_pals index) of a colour: 2 for dark, 3 for black, 1 for mid, 0 for light"""
    c = nes(colour)
    return ((3 if c == 15 else 2) if c < 16 else (1 if c < 32 else 0))


def is_dark(colour):
    return band(colour) >= 2


def _gb(c):
    """0xRRGGBB -> the Game Boy colour it shows as (15 bits)"""
    return to555(((c >> 16) & 255, (c >> 8) & 255, c & 255))


@lru_cache(maxsize=None)
def _table(tmx_path):
    """(the level's colours (0xRRGGBB) in table order, {colour: its table position})"""
    root = ET.parse(str(tmx_path)).getroot()
    props = {p.get("name"): p.get("value") for p in root.findall("properties/property")}
    first = next((int(t.get("firstgid")) for t in root.findall("tileset")
                  if t.get("source", "").endswith("sprites.tsx")), SPRITES_FIRSTGID)
    colours = [rgb(props.get("BG colour", DEFAULT_LEVEL)), rgb(props.get("Ground colour", DEFAULT_LEVEL))]
    for group in root.findall("objectgroup"):
        if group.get("name") != "SP":
            continue
        for o in group.findall("object"):
            if o.get("gid") and (int(o.get("gid")) & 0x1FFFFFFF) - first in (BG_TILE, GROUND_TILE):
                p = {q.get("name"): q.get("value") for q in o.findall("properties/property")}
                colours.append(rgb(p.get("Colour", DEFAULT_TRIGGER)))
    # colours the Game Boy shows the same share a position
    out, slot_of = [], {}
    for c in colours:
        if c in slot_of:
            continue
        g = _gb(c)
        same = next((i for i, o in enumerate(out) if _gb(o) == g), None)
        if same is None:
            same = len(out)
            out.append(c)
        slot_of[c] = same
    # still too many: the closest two share one, until they fit (the most a level has is 48)
    while len(out) > MAX_COLOURS:
        def d(a, b):
            return sum((((a >> s) & 255) - ((b >> s) & 255)) ** 2 for s in (16, 8, 0))
        i, j = min(((i, j) for i in range(len(out)) for j in range(i + 1, len(out))),
                   key=lambda ij: d(out[ij[0]], out[ij[1]]))
        del out[j]
        slot_of = {c: (i if s == j else s - (s > j)) for c, s in slot_of.items()}
    return out, slot_of


def table(tmx_path):
    """The level's colours (0xRRGGBB), in table order"""
    return _table(str(tmx_path))[0]


def slot(tmx_path, value):
    """The table position of a colour '#ffRRGGBB' in the level"""
    return _table(str(tmx_path))[1][rgb(value)]


def c_table(ident, tmx_path):
    """C source of the level's table (level_colour_t, include/level_colours.h)"""
    rows = ["    { 0x%04Xu, 0x%02X }, // #%06x" % (to555(((c >> 16) & 255, (c >> 8) & 255, c & 255)), nes(c), c)
            for c in table(str(tmx_path))]
    return "static const level_colour_t %s_colours[] = {\n%s\n};\n" % (ident, "\n".join(rows))
