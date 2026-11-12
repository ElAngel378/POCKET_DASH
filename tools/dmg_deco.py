"""DMG: the decoration sprites as background (the DMG shows no decoration sprites: too many for its
frame time). The art and the layout are the CGB sprites' (src/sprites/famidash_sprites.c
famidash_deco_table, levels/chr_data/game_art/sprite_tiles.png), in their big / 7 px state, painted
over the cells they cover: tools/level_objects.py compile_objects gives those cells a metatile of
their own whose DMG look has them (tools/compose.py); on CGB they look as before.

The portals, orbs, pads and the arrow too (the DMG draws no object as a sprite but the coins, which
spin and go away when taken): their art is levels/chr_data/game_art/dmg_bg_objects.png, what the
DMG's sprites of them looked like (captured from the game), a 64 x 64 cell each in SHEET_IDS order,
the object's position at (8, 8) of its cell; white 255 is colour 0 .. black 0 is colour 3. On the
black theme only colour 3 shows (white), like the blocks' outlines: the portals' rings and badges
and the arrow are drawn in it (their sprites' dark grey became black).

Not painted: what covers a cell the engine treats by id (saws, the ground) or a fading cell.
"""
import os
import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "game_art" / "sprite_tiles.png"
# sprites.tsx tile -> engine id (tools/tmx2sprites.py portals_map), the decorations only
ENGINE_ID = {**{i: i for i in range(42, 64)}, 73: 73, 74: 74, 112: 65, 113: 66}
# sprites.tsx tile -> engine id of the objects in the sheet
SHEET = ROOT / "levels" / "chr_data" / "game_art" / "dmg_bg_objects.png"
SHEET_IDS = [0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 24, 25, 37, 51, 121, 126]
SHEET_TILE = {**{i: i for i in SHEET_IDS if i not in (4, 20, 21)}, 38: 4, 82: 20, 83: 21, 253: 13, 254: 14}
# the levels with Famidash's cloud decoration art (src/gameplay.c LEVEL_DECO_CLOUD)
CLOUD_LEVELS = {"xstep", "clutterfunk", "theoryofeverything", "electromanadventures"}
S_FLIPX, S_FLIPY = 0x20, 0x40


def shade(v, x, y):
    """Sprite colour -> DMG background colour. Black stays black (3). The player's colour is dark
    grey (2) with a black dot every other pixel both ways: on the black theme (BGP 0x3F: only 3 is
    white) the shape still shows, dimmer than the blocks' outlines"""
    return 2 if v == 2 and (x | y) & 1 else 3

_cache = {}


def _table():
    """engine id -> [(x, y, tile, flip x, flip y)] of 8 x 16 pieces"""
    src = (ROOT / "src" / "sprites" / "famidash_sprites.c").read_text()
    hdr = (ROOT / "include" / "famidash_sprites.h").read_text()
    d = {n: int(v) for n, v in re.findall(r'#define (D_\w+)\s+(\d+)', hdr)}
    decos = {}
    for name, n, xs, ys, ts, ps in re.findall(
            r'FamidashDeco (\w+) = \{(\d+), \d+, \{([^}]*)\}, \{([^}]*)\}, \{([^}]*)\}, \{([^}]*)\}\}', src):
        xs, ys, ts, ps = (v.split(",") for v in (xs, ys, ts, ps))
        decos[name] = [(int(xs[k]), int(ys[k]), d[ts[k].strip()], "S_FLIPX" in ps[k], "S_FLIPY" in ps[k])
                       for k in range(int(n))]
    return {int(i): decos[n] for i, n in re.findall(r'\[(\d+)\] = &(\w+)', src)}


def _pair(tile, cloud):
    """the 8 x 16 picture of decoration tile pair D_xx: {(x, y): colour 1..3}"""
    import build_sprite_tiles as ST
    key = (tile, cloud)
    if key not in _cache:
        if "png" not in _cache:
            _cache["png"] = Image.open(PNG).convert("L").load()
            _cache["gdp"] = ST.GDP_CHR.read_bytes()
        px = _cache["png"]
        idx = lambda v: min(range(4), key=lambda i: abs(v - (255 - 85 * i)))
        out = {}
        if tile == _cache["d"]["D_PB"]:
            # the pulsing ball: a 7 px disc centred at (3, 12) (build_sprite_tiles.py pulse_ball_tiles)
            for y in range(16):
                for x in range(8):
                    dx, dy = abs(x - 3), abs(y - 12)
                    if dx < 3.5 and dy < 3.5 and dx + dy < 4.5:
                        out[(x, y)] = 2
        elif tile >= _cache["d"]["D_RING"]:
            x0 = ST.PULSE_GDP[(tile - _cache["d"]["D_RING"]) // 2][0]
            for y in range(16):
                for x in range(8):
                    if ST.gdp_pixel(_cache["gdp"], x0 + x, y):
                        out[(x, y)] = 1
        else:
            p = tile // 2 if cloud else 80 + tile // 2
            row = ST.SECTION_ROW["DECO_CLOUD" if cloud else "BANK1"]
            x0, y0 = (p % 16) * 8, (row + p // 16) * 16
            for y in range(16):
                for x in range(8):
                    v = idx(px[x0 + x, y0 + y])
                    if v:
                        out[(x, y)] = v
        _cache[key] = out
    return _cache[key]


def _sheet():
    """engine id -> {(dx, dy): DMG colour} from the object's position"""
    if "sheet" not in _cache:
        im = Image.open(SHEET).convert("RGBA")
        px = im.load()
        out = {}
        for k, o in enumerate(SHEET_IDS):
            x0, y0 = (k % 8) * 64, (k // 8) * 64
            out[o] = {(x - 8, y - 8): 3 - px[x0 + x, y0 + y][0] // 85
                      for y in range(64) for x in range(64) if px[x0 + x, y0 + y][3]}
        _cache["sheet"] = out
    return _cache["sheet"]


def pixels(root, name):
    """[{(x, y): DMG colour}]: the level's objects, then its decorations (the ones placed the same
    way most often first: they share their cells' looks), in map pixels (y from the top of the TMX)"""
    if "table" not in _cache:
        _cache["table"] = _table()
        hdr = (ROOT / "include" / "famidash_sprites.h").read_text()
        _cache["d"] = {n: int(v) for n, v in re.findall(r'#define (D_\w+)\s+(\d+)', hdr)}
    first = next((int(t.get("firstgid")) for t in root.findall("tileset")
                  if t.get("source", "").endswith("sprites.tsx")), None)
    if first is None or os.environ.get("DMG_DECO") == "0":     # (0: without them, to compare builds)
        return []
    cloud = name in CLOUD_LEVELS
    found = []
    for group in root.findall("objectgroup"):
        if group.get("name") != "SP":
            continue
        for o in group.findall("object"):
            if o.get("gid") is None:
                continue
            tile = (int(o.get("gid")) & 0x1FFFFFFF) - first
            ox = int(float(o.get("x")))
            oy = int(float(o.get("y"))) - int(float(o.get("height", 16)))
            out = {}
            if tile in SHEET_TILE:
                for (dx, dy), v in _sheet()[SHEET_TILE[tile]].items():
                    out[(ox + dx, oy + dy)] = v
                found.append((0, tile, ox, oy, out))
                continue
            obj = ENGINE_ID.get(tile)
            if obj is None or obj not in _cache["table"]:
                continue
            for x, y, t, fx, fy in _cache["table"][obj]:
                for (px, py), v in _pair(t, cloud).items():
                    lx, ly = x + (7 - px if fx else px), y + (15 - py if fy else py)
                    out[(ox + lx, oy + ly)] = shade(v, lx, ly)
            found.append((1, tile, ox, oy, out))
    often = {}
    for kind, tile, ox, oy, _ in found:
        k = (tile, ox % 16, oy % 16)
        often[k] = often.get(k, 0) + 1
    found.sort(key=lambda f: (f[0], -often[(f[1], f[2] % 16, f[3] % 16)], f[2], f[3]))
    return [f[4] for f in found]
