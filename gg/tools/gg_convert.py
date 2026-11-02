#!/usr/bin/env python3
"""Convert Pocket Dash (Game Boy) assets to Game Gear data for the Dry Out proof of concept.

Outputs gg/src/gg_data.c and gg/src/gg_data.h.

Game Gear tiles are 4bpp, so the 2bpp Game Boy tiles are "baked" with their sub-palette:
pixel value = sub_palette * 4 + gb_pixel.  One 16-colour background palette therefore
holds the four Game Boy sub-palettes used by Dry Out (0, 1, 2 and the ground palette 4),
and one 16-colour sprite palette holds the player.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT_C = os.path.join(ROOT, 'gg', 'src', 'gg_data.c')
OUT_H = os.path.join(ROOT, 'gg', 'src', 'gg_data.h')
OUT_LEVEL_C = os.path.join(ROOT, 'gg', 'src', 'gg_level.c')
OUT_TILES_C = os.path.join(ROOT, 'gg', 'src', 'gg_tiles.c')

LEVEL_MAP = 'levels/level_data/dryout_16high.bin'
LEVEL_NAME = 'dryout'
ENABLE_DECOS = os.environ.get('GG_DECOS', '1') != '0'    # set GG_DECOS=0 to build without decorations
SKY_COLOR_IDX = 22     # level_initial_bg_color[Dry Out]
GROUND_COLOR_IDX = 22   # level_initial_g_color[Dry Out]

GB_TO_SUB = {0: 0, 1: 1, 2: 2, 4: 3}  # GB bg palette -> sub-palette in the GG bg palette


def read(path):
    with open(os.path.join(ROOT, path), 'rb') as f:
        return f.read()


def read_text(path):
    with open(os.path.join(ROOT, path), 'r', encoding='utf-8') as f:
        return f.read()


def decode_2bpp_tile(data, idx):
    """Returns an 8x8 list of rows of pixel values 0-3."""
    rows = []
    for y in range(8):
        lo = data[idx * 16 + y * 2]
        hi = data[idx * 16 + y * 2 + 1]
        rows.append([(((hi >> (7 - x)) & 1) << 1) | ((lo >> (7 - x)) & 1) for x in range(8)])
    return rows


def encode_4bpp_tile(rows, sub, transparent0=False):
    out = bytearray()
    for y in range(8):
        planes = [0, 0, 0, 0]
        for x in range(8):
            v = rows[y][x]
            if v or not transparent0:
                v += sub * 4
            for b in range(4):
                planes[b] |= ((v >> b) & 1) << (7 - x)
        out += bytes(planes)
    return bytes(out)


def parse_block(text, name):
    m = re.search(re.escape(name) + r'[^=]*=\s*\{(.*?)\n\};', text, re.S)
    if not m:
        sys.exit('block not found: ' + name)
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', m.group(1), flags=re.S)
    return [int(n) for n in re.findall(r'\d+', body)]


def gb555_to_gg(v):
    r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
    return (r >> 1) | ((g >> 1) << 4) | ((b >> 1) << 8)


def rgb8_to_gg(r, g, b):
    return (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8)


def _lab(r, g, b):
    """sRGB 0-255 -> CIELAB (D65)."""
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = lin(r), lin(g), lin(b)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    def f(t):
        return t ** (1 / 3.0) if t > 0.008856 else 7.787 * t + 16 / 116.0
    fx, fy, fz = f(x), f(y), f(z)
    return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))


_SMS_LAB = [(_lab(r * 85, g * 85, b * 85), r | (g << 2) | (b << 4))
            for r in range(4) for g in range(4) for b in range(4)]


def gg_to_sms(c):
    """12-bit Game Gear colour -> nearest of the 64 Master System colours (CIELAB distance).
    Rounding each channel separately shifts hues badly with only 4 levels per channel."""
    L = _lab((c & 15) * 17, ((c >> 4) & 15) * 17, ((c >> 8) & 15) * 17)
    return min(_SMS_LAB, key=lambda e: sum((a - b) ** 2 for a, b in zip(e[0], L)))[1]


def main():
    mt_src = read_text('include/famidash_metatiles_dmg.c')
    metatiles = parse_block(mt_src, 'metatiles[FAMIDASH_NUM_METATILES][4]')
    mt_pal = parse_block(mt_src, 'famidash_metatile_palettes')
    mt_col = parse_block(mt_src, 'famidash_metatile_collision')
    assert len(metatiles) == 1024 and len(mt_pal) == 256 and len(mt_col) == 256

    tiles2bpp = read('levels/chr_data/chr_gb_dmg_tiles.bin')
    ground2bpp = read('levels/chr_data/menu_ground_tiles.bin')
    level = read(LEVEL_MAP)
    assert len(level) % 16 == 0
    map_w = len(level) // 16
    used_mts = sorted(set(level))

    # ---- background tiles ----
    bg_tiles = [bytes(32)]           # tile 0: blank (sky colour, palette entry 0)
    bg_index = {}

    def bg_tile(key, rows, sub):
        if key not in bg_index:
            bg_index[key] = len(bg_tiles)
            bg_tiles.append(encode_4bpp_tile(rows, sub))
        return bg_index[key]

    gg_mt = [[0] * 4 for _ in range(256)]
    for mt in used_mts:
        pal = mt_pal[mt]
        sub = GB_TO_SUB.get(pal, 0)
        for i in range(4):
            t = metatiles[mt * 4 + i]
            if t == 12:
                gg_mt[mt][i] = 0     # parallax placeholder -> plain sky
            else:
                gg_mt[mt][i] = bg_tile(('lvl', t, sub), decode_2bpp_tile(tiles2bpp, t), sub)

    ground_top_src = [48, 49, 49, 49, 49, 49, 49, 50]
    ground_bot_src = [51, 52, 52, 52, 52, 52, 52, 53]
    ground_sub = GB_TO_SUB[4]
    ground_top = [bg_tile(('gnd', t), decode_2bpp_tile(ground2bpp, t - 48), ground_sub) for t in ground_top_src]
    ground_bot = [bg_tile(('gnd', t), decode_2bpp_tile(ground2bpp, t - 48), ground_sub) for t in ground_bot_src]
    if len(bg_tiles) > 256:
        sys.exit('too many bg tiles: %d (need >8-bit tile ids)' % len(bg_tiles))

    # ---- palettes ----
    tables = read_text('src/famidash_bg_tables.h')
    sky = re.findall(r'\{\s*(0x[0-9A-Fa-f]+)u,\s*(0x[0-9A-Fa-f]+)u,\s*(0x[0-9A-Fa-f]+)u,\s*(0x[0-9A-Fa-f]+)u,\s*(0x[0-9A-Fa-f]+)u\s*\}', tables)
    gnd = re.findall(r'\{\s*(0x[0-9A-Fa-f]+)u,\s*(0x[0-9A-Fa-f]+)u,\s*(0x[0-9A-Fa-f]+)u\s*\}', tables)
    assert len(sky) == 64 and len(gnd) == 64
    sky_c = int(sky[SKY_COLOR_IDX][0], 16)
    sky_d = int(sky[SKY_COLOR_IDX][1], 16)
    g_color = int(sky[GROUND_COLOR_IDX][0], 16)
    g_dark, g_18, g_9 = (int(v, 16) for v in gnd[GROUND_COLOR_IDX])
    WHITE = rgb8_to_gg(255, 255, 255)
    BLACK = 0
    bg_pal = [
        gb555_to_gg(sky_c), gb555_to_gg(sky_d), BLACK, WHITE,                       # 0 normal
        gb555_to_gg(sky_c), gb555_to_gg(g_dark), gb555_to_gg(g_color), WHITE,      # 1 accent
        gb555_to_gg(sky_c), gb555_to_gg(sky_d), BLACK, rgb8_to_gg(125, 255, 0),    # 2 hazards
        WHITE, gb555_to_gg(g_color), gb555_to_gg(g_18), gb555_to_gg(g_9),          # 3 = GB palette 4 (ground)
    ]

    # ---- sprites ----
    # One 16-colour sprite palette: colour 0 is transparent, then five sub-palettes of
    # three colours each (GB sprite palette n -> sub-palette n). Pixel v (1-3) of sub-palette
    # s becomes 1 + 3*s + (v-1).
    SPR_COLORS = {
        0: [(0, 255, 255), (125, 255, 0), (0, 0, 0)],      # cube / ship
        1: [(0, 0, 0), (125, 255, 0), (0, 0, 0)],          # cube portal
        2: [(0, 0, 0), (0, 255, 255), (0, 255, 255)],      # blue (gravity down, blue pads/orbs)
        3: [(0, 0, 0), (255, 255, 0), (255, 255, 0)],      # yellow (gravity up, yellow pads/orbs)
        4: [(0, 0, 0), (255, 100, 255), (255, 100, 255)],  # pink (ship portal, pink pads/orbs)
    }
    spr_pal = [0] * 16
    for s, cols in SPR_COLORS.items():
        for j, (r, g, b) in enumerate(cols):
            spr_pal[1 + 3 * s + j] = rgb8_to_gg(r, g, b)

    def encode_spr_tile(rows, sub):
        out = bytearray()
        for y in range(8):
            planes = [0, 0, 0, 0]
            for x in range(8):
                v = rows[y][x]
                if v:
                    v = 1 + 3 * sub + (v - 1)
                for b in range(4):
                    planes[b] |= ((v >> b) & 1) << (7 - x)
            out += bytes(planes)
        return bytes(out)

    def load_c_bytes(path, name):
        m = re.search(name + r'\[[^\]]*\]\s*=\s*\{(.*?)\};', read_text(path), re.S)
        return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1)))

    icon_bytes = load_c_bytes('src/graphics/icon1.c', 'icon1_tiles')
    ship_bytes = load_c_bytes('src/graphics/ship1.c', 'ship_tiles')
    fam_bytes = read('levels/chr_data/famidash/famidash_sprites_dmg_tiles.bin')
    assert len(icon_bytes) == 128 and len(ship_bytes) == 64

    spr_tiles = []
    spr_seen = {}

    def spr_pair(src, tile, sub, fx, fy):
        """8x16 sprite made from source tiles (tile, tile+1); returns the even hardware tile index."""
        img = decode_2bpp_tile(src, tile) + decode_2bpp_tile(src, tile + 1)
        if fy:
            img = img[::-1]
        if fx:
            img = [row[::-1] for row in img]
        top, bot = encode_spr_tile(img[:8], sub), encode_spr_tile(img[8:], sub)
        if (top, bot) not in spr_seen:
            spr_seen[(top, bot)] = len(spr_tiles)
            spr_tiles.extend([top, bot])
        return spr_seen[(top, bot)]

    # Player cube: metasprite offsets are relative to the previous item (dx -1, then +8).
    icon_src = read_text('src/graphics/icon1.c')
    cube_frames = []   # index: gravity_flipped * 25 + frame -> [left, right]
    parsed = []
    for fm in re.finditer(r'icon1_metasprite\d+\[\]\s*=\s*\{(.*?)METASPR_TERM', icon_src, re.S):
        row = []
        for it in re.findall(r'METASPR_ITEM\(([^\n]*)\),', fm.group(1)):
            parts = [p.strip() for p in it.split(',', 3)]
            row.append((int(parts[2]), 'S_FLIPX' in parts[3], 'S_FLIPY' in parts[3]))
        assert len(row) == 2
        parsed.append(row)
    assert len(parsed) == 25
    for gflip in (0, 1):
        for row in parsed:
            cube_frames.append([spr_pair(icon_bytes, t, 0, fx, fy != bool(gflip)) for (t, fx, fy) in row])

    # Ship: { -1,-1,8,0 }, { 0,8,10,0 } -> tail tiles 0/1, nose tiles 2/3 of ship_tiles.
    ship_pairs = [[spr_pair(ship_bytes, 0, 0, False, bool(g)), spr_pair(ship_bytes, 2, 0, False, bool(g))] for g in (0, 1)]

    # Level objects (portals, pads, orbs). Item = (dy, dx, tile, pal, flipx, flipy), relative to previous.
    PT = dict(PT_41=0, PT_43=2, PT_45=4, PT_47=6, PT_49=8, PT_4B=10, PT_6D=12, PT_6F=14, PT_73=16,
              PT_75=18, PT_79=20, PT_7B=22, MT_99=24, MT_9B=26, MT_B9=28, MT_BB=30, PT_59=32,
              PT_5B=34, PT_4D=36, PT_4F=38, PT_51=40)

    def cube_portal(p):
        def f(dy, dx, t, y=False):
            return (dy, dx, PT[t], p, False, y)
        return [f(0, 0, 'PT_41'), f(0, 8, 'PT_43'), f(0, 8, 'PT_45'),
                f(16, -16, 'PT_47'), f(0, 8, 'PT_49'), f(0, 8, 'PT_4B'),
                f(16, -16, 'PT_41', True), f(0, 8, 'PT_43', True), f(0, 8, 'PT_45', True)]

    def horiz_portal(p, flipv):
        return [(0, 0, PT['PT_4D'], p, False, flipv), (0, 8, PT['PT_4F'], p, False, flipv),
                (0, 8, PT['PT_51'], p, False, flipv), (0, 8, PT['PT_51'], p, True, flipv),
                (0, 8, PT['PT_4F'], p, True, flipv), (0, 8, PT['PT_4D'], p, True, flipv)]

    def vert_portal(p):
        return [(0, 0, PT['PT_6D'], p, False, False), (0, 8, PT['PT_6F'], p, False, False),
                (16, -8, PT['PT_73'], p, False, False), (0, 8, PT['PT_75'], p, False, False),
                (16, -8, PT['PT_6D'], p, False, True), (0, 8, PT['PT_6F'], p, False, True)]

    def two_tile(l, r, p, flipy=False):
        return [(0, 0, PT[l], p, False, flipy), (0, 8, PT[r], p, True, flipy)]

    OBJ_DEFS = {
        0: cube_portal(1), 1: cube_portal(4),
        5: two_tile('MT_99', 'MT_9B', 2), 6: two_tile('MT_B9', 'MT_BB', 4),
        8: vert_portal(2), 9: vert_portal(3),
        10: two_tile('PT_79', 'PT_7B', 3), 11: two_tile('MT_99', 'MT_9B', 3),
        12: two_tile('PT_79', 'PT_7B', 3, True), 13: two_tile('PT_79', 'PT_7B', 2),
        14: two_tile('PT_79', 'PT_7B', 2, True),
        16: horiz_portal(2, False), 17: horiz_portal(2, True),
        18: horiz_portal(3, False), 19: horiz_portal(3, True),
        37: two_tile('PT_59', 'PT_5B', 4),
    }
    # Decorations (chains, lights, spikes...). Items are absolute (dx, dy) from the object position.
    deco_bytes = read('levels/chr_data/famidash/famidash_deco_cgb_tiles.bin')
    fam_c = read_text('src/sprites/famidash_sprites.c')
    fam_h = read_text('include/famidash_sprites.h')
    deco_tiles = {n: int(v) for n, v in re.findall(r'#define (D_[0-9A-F]+) (\d+)', fam_h)}
    deco_structs = {}
    for name, body in re.findall(r'static const FamidashDeco (\w+) = \{(.*)\};', fam_c):
        groups = re.findall(r'\{([^}]*)\}', body)
        head = body.split('{')[0].split(',')
        count = int(head[0])
        xs = [int(v) for v in groups[0].split(',')]
        ys = [int(v) for v in groups[1].split(',')]
        tiles = [v.strip() for v in groups[2].split(',')]
        props = [v.strip() for v in groups[3].split(',')]
        deco_structs[name] = [(xs[i], ys[i], deco_tiles.get(tiles[i], 0), 'S_FLIPX' in props[i], 'S_FLIPY' in props[i])
                              for i in range(count)]
    deco_table = {int(t): n for t, n in re.findall(r'\[(\d+)\] = &(\w+)', fam_c.split('famidash_deco_table')[1])}
    deco_table.update({int(t): n for t, n in re.findall(r', \[(\d+)\] = &(\w+)', fam_c.split('famidash_deco_table')[1])})
    DECO_DEFS = {t: deco_structs[n] for t, n in deco_table.items() if n in deco_structs}
    if not ENABLE_DECOS:
        DECO_DEFS = {}

    spr_items = []
    obj_defs = [(0, 0)] * 64
    for t, items in sorted(DECO_DEFS.items()):
        obj_defs[t] = (len(spr_items), len(items))
        for dx, dy, tile, fx, fy in items:
            spr_items.append((dx, dy, spr_pair(deco_bytes, tile, 1, fx, fy)))
    for t, items in sorted(OBJ_DEFS.items()):
        obj_defs[t] = (len(spr_items), len(items))
        x = y = 0
        for dy, dx, tile, pal, fx, fy in items:
            y += dy
            x += dx
            spr_items.append((x, y, spr_pair(fam_bytes, tile, pal, fx, fy)))
    if len(spr_tiles) > 256:
        sys.exit('too many sprite tiles: %d' % len(spr_tiles))

    # Level object list (functional objects + colour triggers; decorations are skipped).
    sp_src = read_text('src/sprites/%s_sprites.c' % LEVEL_NAME).split('%s_sp_dmg' % LEVEL_NAME)[0]
    objs = []
    for x, y, t in re.findall(r'\{(\d+),\s*(\d+),\s*(\d+)\}', sp_src):
        x, y, t = int(x), int(y), int(t)
        if t in OBJ_DEFS or t in DECO_DEFS or t == 15 or 128 <= t <= 175 or 192 <= t <= 239:
            objs.append((x, y, t))
    assert objs == sorted(objs, key=lambda o: o[0])
    # Two lists so the per-frame loops only touch what they need:
    #   vis   - objects that have sprites (drawn), incl. decorations
    #   logic - objects the player can interact with (portals, pads, orbs) + colour triggers
    vis = [o for o in objs if o[2] in OBJ_DEFS or o[2] in DECO_DEFS]
    logic = [o for o in objs if o[2] not in DECO_DEFS and o[2] != 15]

    # Colour trigger tables (12-bit Game Gear colours).
    sky_tab = [[gb555_to_gg(int(s[0], 16)), gb555_to_gg(int(s[1], 16))] for s in sky]
    gnd_tab = [[gb555_to_gg(int(sky[i][0], 16)), gb555_to_gg(int(g[0], 16)),
                gb555_to_gg(int(g[1], 16)), gb555_to_gg(int(g[2], 16))] for i, g in enumerate(gnd)]

    # ---- emit ----
    with open(OUT_H, 'w', newline='\n') as h:
        h.write('/* Generated by gg/tools/gg_convert.py - do not edit. */\n')
        h.write('#ifndef GG_DATA_H\n#define GG_DATA_H\n\n#include <stdint.h>\n\n')
        h.write('#define GG_BG_TILE_COUNT %d\n' % len(bg_tiles))
        h.write('#define GG_SPR_TILE_COUNT %d\n' % len(spr_tiles))
        h.write('#define GG_LEVEL_WIDTH %d\n' % map_w)
        h.write('#define GG_OBJ_COUNT %d\n#define GG_LOGIC_COUNT %d\n\n' % (len(vis), len(logic)))
        h.write('typedef struct { int8_t dx, dy; uint8_t tile; } GgSprItem;\n')
        h.write('typedef struct { uint8_t start, count; } GgObjDef;\n')
        h.write('typedef struct { uint16_t x; uint8_t y, type; } GgObj;\n\n')
        h.write('extern const uint8_t gg_bg_tiles[GG_BG_TILE_COUNT * 32];\n')
        h.write('extern const uint8_t gg_spr_tiles[GG_SPR_TILE_COUNT * 32];\n')
        h.write('extern const uint8_t gg_mt[256][4];\n')
        h.write('extern const uint8_t gg_ground_top[8];\nextern const uint8_t gg_ground_bot[8];\n')
        h.write('extern const uint16_t gg_bg_palette[16];\nextern const uint16_t gg_spr_palette[16];\n')
        h.write('/* [gravity_flipped * 25 + anim_frame][left/right] */\n')
        h.write('extern const uint8_t gg_cube_frames[50][2];\n')
        h.write('/* [gravity_flipped][tail/nose] */\n')
        h.write('extern const uint8_t gg_ship_tiles[2][2];\n')
        h.write('extern const GgSprItem gg_spr_items[%d];\n' % len(spr_items))
        h.write('extern const GgObjDef gg_obj_defs[64];\n')
        h.write('extern const GgObj gg_objs[GG_OBJ_COUNT];\n')
        h.write('extern const GgObj gg_logic[GG_LOGIC_COUNT];\n')
        h.write('extern const uint16_t gg_sky_tab[64][2];   /* colour, darker */\n')
        h.write('extern const uint16_t gg_gnd_tab[64][4];   /* colour, darker, grid 18, grid 9 */\n')
        h.write('extern const uint8_t famidash_metatile_collision[256];\n')
        h.write('extern const uint8_t %s_map[GG_LEVEL_WIDTH * 16];\n\n#endif\n' % LEVEL_NAME)

    def arr(name, ctype, values, per_line=16, fmt='%d', nested=False):
        s = 'const %s %s = {\n' % (ctype, name)
        for i in range(0, len(values), per_line):
            row = ', '.join(fmt % v for v in values[i:i + per_line])
            s += '    ' + ('{ %s },' % row if nested else row + ',') + '\n'
        return s + '};\n\n'

    def pal_arr(name, values, per_line, nested=False):
        """Colour table: 12-bit Game Gear colours, or pre-matched SMS colours on __TARGET_sms."""
        return ('#ifdef __TARGET_sms\n' + arr(name, 'uint16_t', [gg_to_sms(v) for v in values], per_line, '0x%02x', nested)
                + '#else\n' + arr(name, 'uint16_t', values, per_line, '0x%03x', nested) + '#endif\n\n')

    def flat(rows):
        return [v for row in rows for v in row]

    with open(OUT_C, 'w', newline='\n') as c:
        c.write('/* Generated by gg/tools/gg_convert.py - do not edit. */\n')
        c.write('#include "gg_data.h"\n\n')
        c.write(arr('gg_mt[256][4]', 'uint8_t', flat(gg_mt), 4, nested=True))
        c.write(arr('gg_ground_top[8]', 'uint8_t', ground_top))
        c.write(arr('gg_ground_bot[8]', 'uint8_t', ground_bot))
        c.write(pal_arr('gg_bg_palette[16]', bg_pal, 8))
        c.write(pal_arr('gg_spr_palette[16]', spr_pal, 8))
        c.write(arr('gg_cube_frames[50][2]', 'uint8_t', flat(cube_frames), 2, nested=True))
        c.write(arr('gg_ship_tiles[2][2]', 'uint8_t', flat(ship_pairs), 2, nested=True))
        c.write('const GgSprItem gg_spr_items[%d] = {\n' % len(spr_items))
        for x, y, t in spr_items:
            c.write('    { %d, %d, %d },\n' % (x, y, t))
        c.write('};\n\n')
        c.write('const GgObjDef gg_obj_defs[64] = {\n')
        for s, n in obj_defs:
            c.write('    { %d, %d },\n' % (s, n))
        c.write('};\n\n')
        c.write('const GgObj gg_objs[GG_OBJ_COUNT] = {\n')
        for x, y, t in vis:
            c.write('    { %d, %d, %d },\n' % (x, y, t))
        c.write('};\n\n')
        c.write('const GgObj gg_logic[GG_LOGIC_COUNT] = {\n')
        for x, y, t in logic:
            c.write('    { %d, %d, %d },\n' % (x, y, t))
        c.write('};\n\n')
        c.write(pal_arr('gg_sky_tab[64][2]', flat(sky_tab), 2, nested=True))
        c.write(pal_arr('gg_gnd_tab[64][4]', flat(gnd_tab), 4, nested=True))
        c.write(arr('famidash_metatile_collision[256]', 'uint8_t', mt_col))

    # Tile graphics are only needed at startup, so they live in bank 3 (see main()).
    with open(OUT_TILES_C, 'w', newline='\n') as c:
        c.write('/* Generated by gg/tools/gg_convert.py - do not edit. */\n')
        c.write('#pragma bank 3\n\n#include "gg_data.h"\n\n')
        c.write(arr('gg_bg_tiles[GG_BG_TILE_COUNT * 32]', 'uint8_t', list(b''.join(bg_tiles)), 16, '0x%02x'))
        c.write(arr('gg_spr_tiles[GG_SPR_TILE_COUNT * 32]', 'uint8_t', list(b''.join(spr_tiles)), 16, '0x%02x'))

    # The level map lives in its own ROM bank, mapped into the 0x4000 window by main().
    with open(OUT_LEVEL_C, 'w', newline='\n') as c:
        c.write('/* Generated by gg/tools/gg_convert.py - do not edit. */\n')
        c.write('#pragma bank 2\n\n#include "gg_data.h"\n\n')
        c.write(arr('%s_map[GG_LEVEL_WIDTH * 16]' % LEVEL_NAME, 'uint8_t', list(level), 32))

    print('bg tiles: %d, sprite tiles: %d, objects: %d, level width: %d' % (len(bg_tiles), len(spr_tiles), len(vis), map_w))


if __name__ == '__main__':
    main()
