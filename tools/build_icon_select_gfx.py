"""Icon select (garage) screen, laid out like the Geometry Dash one.

    python tools/build_icon_select_gfx.py

Sources:
    levels/chr_data/cube_icon_frames.png the cube icons as in the game (frame 0, 0 degrees, 14x14
    levels/chr_data/fd_cube_frames.png   at (1, 1) in 16x16): icon 0 the original cube, 1..26
                                         the Famidash ones (tools/make_cube_icon_frames.py).
                                         White transparent, light grey secondary, dark grey
                                         primary, black outline
    levels/chr_data/gamemode_icons.chr   GD icon pack (NES 2bpp): 9x9 mini gamemode icons at
                                         y 23..31, one per 16 px column (cube, ship, ufo, robot,
                                         wave, spider, swing, ball)
    src/graphics/menu_select_bg.c        level select tiles reused here: back arrow, rounded box,
                                         corner stage blocks

Screen (20x18 tiles). The picture is built in DMG shades (0 white, 1 light grey = page,
2 dark grey = boxes, 3 black) so the DMG needs no extra work; CGB colours come from the
attribute map:
    pal 0  greys: page, gamemode badges, icon box, colour box frame
    pal 1  preview cube (cols 8..11, rows 1..4): secondary, page, primary, black
    pal 2  green (back arrow, green corner blocks)   pal 3  cyan corner blocks
    pal 4..7  colour swatches, three colours each (cols 4..15, rows 15..16)
The icon box (cols 2..17, rows 8..13) shows a page of 12 icons: its tiles are per page
(PAGE_TILE_BASE.., loaded at runtime), with a dot per page under the icons.

Writes include/icon_select_bg.h, src/graphics/icon_select_bg.c, include/icon_catalog.h,
src/graphics/icon_catalog.c, src/graphics/icon_pages.c (page tiles, own bank) and
src/graphics/icon_previews.c (preview tiles, own bank).
"""
import os
import re
from collections import deque
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ICON0_PNG = ROOT / "levels" / "chr_data" / "cube_icon_frames.png"
FD_PNG = ROOT / "levels" / "chr_data" / "fd_cube_frames.png"
GM_CHR = ROOT / "levels" / "chr_data" / "gamemode_icons.chr"
MENU_SELECT = ROOT / "src" / "graphics" / "menu_select_bg.c"

NUM_ICONS = 46
ICONS_PER_PAGE = 12
NUM_PAGES = (NUM_ICONS + ICONS_PER_PAGE - 1) // ICONS_PER_PAGE
GRID_C0, GRID_R0, GRID_W, GRID_H = 2, 8, 16, 6   # the icon box: per page tiles
PAGE_TILE_BASE = 16
PAGES_BANK = 64
PREVIEWS_BANK = 69   # the previews (46 x 256 bytes) do not fit with the pages in one bank
GM_ORDER = [0, 1, 7, 2, 4, 3, 5]   # cube, ship, ball, ufo, wave, robot, spider (pack columns)

# Player colours (index 0 / 1 = the default primary (outer fill) / secondary of the GD cube)
COLORS = [
    ("Lime", (125, 255, 0), 1), ("Cyan", (0, 255, 255), 1), ("Yellow", (255, 230, 0), 0),
    ("Orange", (255, 120, 0), 1), ("Red", (255, 30, 30), 2), ("Pink", (255, 0, 200), 1),
    ("Purple", (160, 30, 255), 2), ("Blue", (0, 110, 255), 2), ("Green", (0, 200, 0), 2),
    ("White", (255, 255, 255), 0), ("Grey", (160, 160, 160), 1), ("Dark Grey", (90, 90, 90), 3),
]

PREVIEW_COL, PREVIEW_ROW = 8, 1                 # 4x4 tiles, filled at runtime
PREVIEW_TILE_BASE = 0                           # tiles 0..15
SLOT_X0, SLOT_Y0, SLOT_PITCH = 22, 69, 20       # icon grid: 2 rows x 6 slots
DOTS_Y = 106                                    # page dots under the icons
BADGE_X0, BADGE_Y, BADGE_PITCH = 18, 46, 18     # gamemode badges (16x16)
SWATCH_COL, SWATCH_ROWS = 4, (15, 16)


def menu_select_tiles():
    s = MENU_SELECT.read_text()
    body = re.search(r"menu_select_bg_tiles\[\d*\]\s*=\s*\{(.*?)\};", s, re.S).group(1)
    t = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", body)]

    def tile(n):
        return [[((t[n * 16 + 2 * y] >> (7 - x)) & 1) | (((t[n * 16 + 2 * y + 1] >> (7 - x)) & 1) << 1)
                 for x in range(8)] for y in range(8)]
    return tile


def main():
    ms_tile = menu_select_tiles()
    S = [[1] * 160 for _ in range(144)]   # shades
    A = [[0] * 20 for _ in range(18)]     # CGB palettes

    def put_tile(col, row, px, remap, pal):
        for y in range(8):
            for x in range(8):
                S[row * 8 + y][col * 8 + x] = remap[px[y][x]]
        A[row][col] = pal

    # back arrow and corner blocks (level select art; its colour 0 is the page here)
    deco = {0: 1, 1: 0, 2: 2, 3: 3}
    for (c, r, t) in ((1, 1, 0x0A), (2, 1, 0x0B), (1, 2, 0x0E), (2, 2, 0x0F)):
        put_tile(c, r, ms_tile(t), deco, 2)
    corners = [(0, 15, 0x37, 3), (0, 16, 0x39, 2), (0, 17, 0x3B, 3), (1, 17, 0x3C, 2), (2, 17, 0x3D, 3),
               (19, 15, 0x38, 3), (19, 16, 0x3A, 2), (19, 17, 0x40, 3), (18, 17, 0x3F, 2), (17, 17, 0x3E, 3)]
    for c, r, t, pal in corners:
        put_tile(c, r, ms_tile(t), deco, pal)

    # rounded boxes (level select box: fill -> dark grey, border -> white)
    boxmap = {0: 1, 1: 2, 2: 0, 3: 0}

    def box(c0, r0, c1, r1):
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                top, bot, left, right = r == r0, r == r1, c == c0, c == c1
                t = (0x12 if left else 0x14 if right else 0x13) if top else \
                    (0x21 if left else 0x23 if right else 0x22) if bot else \
                    (0x15 if left else 0x17 if right else 0x16)
                put_tile(c, r, ms_tile(t), boxmap, 0)
    box(2, 8, 17, 13)       # icon grid
    box(3, 14, 16, 17)      # colour swatches

    # line under the preview: solid in the middle, dotted towards the ends
    for x in range(16, 144):
        d = min(x - 16, 143 - x)
        on = d >= 24 or (d >= 12 and x % 2 == 0) or (d < 12 and x % 4 == 0)
        if on:
            S[41][x] = 0
            if d >= 24:
                S[42][x] = 0

    # gamemode badges: dark grey disc with a black ring and the mini icon
    gm = GM_CHR.read_bytes()

    def gm_px(x, y):
        i = (y // 8) * 16 + x // 8
        a, b = gm[i * 16 + y % 8], gm[i * 16 + y % 8 + 8]
        return ((a >> (7 - x % 8)) & 1) | (((b >> (7 - x % 8)) & 1) << 1)

    mini = {0: 2, 1: 0, 2: 1, 3: 3}
    # 16 px pixel-art circle: first/last filled x per row
    span = [(5, 10), (3, 12), (2, 13), (1, 14), (1, 14), (0, 15), (0, 15), (0, 15),
            (0, 15), (0, 15), (0, 15), (1, 14), (1, 14), (2, 13), (3, 12), (5, 10)]

    def inside(x, y):
        return 0 <= y < 16 and span[y][0] <= x <= span[y][1]
    for i, k in enumerate(GM_ORDER):
        bx, by = BADGE_X0 + i * BADGE_PITCH, BADGE_Y
        for y in range(16):
            for x in range(16):
                if inside(x, y):
                    edge = any(not inside(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                    S[by + y][bx + x] = 3 if edge else 2
        for y in range(9):
            for x in range(9):
                v = gm_px(16 * k + x, 23 + y)
                if v:
                    S[by + 4 + y][bx + 4 + x] = mini[v]

    # cube icons: frame 0 of the game's, roles 'o' outside (transparent), 'k' outline,
    # 'p' primary, 's' secondary
    role = {255: 'o', 170: 's', 85: 'p', 0: 'k'}
    icon0, fd = Image.open(ICON0_PNG).convert("L"), Image.open(FD_PNG).convert("L")

    def icon(i):
        img, y0 = (icon0, 0) if i == 0 else (fd, (i - 1) * 16)
        return [[role[img.getpixel((x, y0 + y))] for x in range(16)] for y in range(16)]
    icons = [icon(i) for i in range(NUM_ICONS)]
    grid = {'o': 2, 'k': 3, 'p': 1, 's': 0}

    def draw_page(P, page):
        for slot in range(ICONS_PER_PAGE):
            sx, sy = SLOT_X0 + (slot % 6) * SLOT_PITCH, SLOT_Y0 + (slot // 6) * SLOT_PITCH
            i = page * ICONS_PER_PAGE + slot
            if i < NUM_ICONS:
                for y in range(16):
                    for x in range(16):
                        P[sy + y][sx + x] = grid[icons[i][y][x]]
            else:   # empty slot: faint rounded outline
                for y in range(1, 15):
                    for x in range(1, 15):
                        edge = x in (1, 14) or y in (1, 14)
                        corner = (x in (1, 14)) and (y in (1, 14))
                        if edge and not corner:
                            P[sy + y][sx + x] = 1
        # page dots: 2x2, white for this page, light grey for the others
        dx0 = 80 - (NUM_PAGES * 6 - 4) // 2
        for k in range(NUM_PAGES):
            for y in range(2):
                for x in range(2):
                    P[DOTS_Y + y][dx0 + k * 6 + x] = 0 if k == page else 1

    # preview area palette (tiles filled at runtime)
    for r in range(PREVIEW_ROW, PREVIEW_ROW + 4):
        for c in range(PREVIEW_COL, PREVIEW_COL + 4):
            A[r][c] = 1

    # colour swatches (CGB): 1 px frame in colour 0 (box fill), colour 1..3 inside
    def swatch(k, frame):
        return [[frame if (x in (0, 7) or y in (0, 7) or (x in (1, 6) and y in (1, 6))) else k
                 for x in range(8)] for y in range(8)]
    for r in SWATCH_ROWS:
        for j in range(12):
            put_tile(SWATCH_COL + j, r, swatch(j % 3 + 1, 0), {0: 0, 1: 1, 2: 2, 3: 3}, 4 + j // 3)

    # tiles
    def enc(px):
        out = []
        for row in px:
            lo = hi = 0
            for v in row:
                lo = (lo << 1) | (v & 1)
                hi = (hi << 1) | (v >> 1)
            out += [lo, hi]
        return bytes(out)

    tiles = [bytes(16)] * (PAGE_TILE_BASE + GRID_W * GRID_H)   # preview, icon box page: loaded at runtime
    index = {}
    mp = [[0] * 20 for _ in range(18)]
    page_blobs = []
    for page in range(NUM_PAGES):
        P = [row[:] for row in S]
        draw_page(P, page)
        if os.environ.get("ICON_SELECT_DUMP"):   # debug: the screen with this page
            im = Image.new("L", (160, 144))
            im.putdata([[255, 170, 85, 0][v] for row in P for v in row])
            im.resize((480, 432), Image.NEAREST).save(Path(os.environ["ICON_SELECT_DUMP"]) / f"page{page}.png")
        page_blobs.append(b"".join(enc([P[r * 8 + y][c * 8:c * 8 + 8] for y in range(8)])
                                   for r in range(GRID_R0, GRID_R0 + GRID_H)
                                   for c in range(GRID_C0, GRID_C0 + GRID_W)))
    for r in range(18):
        for c in range(20):
            if PREVIEW_ROW <= r < PREVIEW_ROW + 4 and PREVIEW_COL <= c < PREVIEW_COL + 4:
                mp[r][c] = PREVIEW_TILE_BASE + (r - PREVIEW_ROW) * 4 + (c - PREVIEW_COL)
                continue
            if GRID_R0 <= r < GRID_R0 + GRID_H and GRID_C0 <= c < GRID_C0 + GRID_W:
                mp[r][c] = PAGE_TILE_BASE + (r - GRID_R0) * GRID_W + (c - GRID_C0)
                continue
            b = enc([S[r * 8 + y][c * 8:c * 8 + 8] for y in range(8)])
            if b not in index:
                index[b] = len(tiles)
                tiles.append(b)
            mp[r][c] = index[b]
    dmg_swatch_base = len(tiles)
    for s in range(4):   # DMG swatches: black frame, shade inside, on the dark grey box
        tiles.append(enc([[2 if (x in (0, 7) or y in (0, 7)) else 3 if (x in (1, 6) or y in (1, 6)) else s
                           for x in range(8)] for y in range(8)]))
    tiles.append(enc([[2] * 8 for _ in range(8)]))   # DMG: the box where the other swatches are
    assert len(tiles) <= 256, len(tiles)   # 128+ live at 0x8800 (signed BG addressing), clear of sprite tiles 0..2

    # preview tiles per icon (2x, 32x32): secondary 0, page 1, primary 2, black 3
    prev = {'o': 1, 'k': 3, 'p': 2, 's': 0}
    preview_blobs = []
    for ic in icons:
        P = [[1] * 32 for _ in range(32)]
        for y in range(16):
            for x in range(16):
                v = prev[ic[y][x]]
                for yy in (0, 1):
                    for xx in (0, 1):
                        P[2 * y + yy][2 * x + xx] = v
        blob = b""
        for tr in range(4):
            for tc in range(4):
                blob += enc([P[tr * 8 + y][tc * 8:tc * 8 + 8] for y in range(8)])
        preview_blobs.append(blob)

    # sprites: 0 bracket corner (icon and gamemode cursors, flipped for the other corners), 1 swatch frame
    # colour 1 = cursor colour, 3 = black edge
    bracket =["31111100", "11111300", "11333000", "11300000", "11300000", "13000000", "00000000", "00000000"]
    frame = ["31111113", "13333331", "13000031", "13000031", "13000031", "13000031", "13333331", "31111113"]
    spr = [enc([[int(ch) for ch in r] for r in t]) for t in (bracket, frame)]

    # write
    def carr(data, per=16):
        lines = []
        for i in range(0, len(data), per):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + per]) + ",")
        return "\n".join(lines)

    (ROOT / "include" / "icon_select_bg.h").write_text(f"""// Generated by tools/build_icon_select_gfx.py. Do not edit.
#ifndef ICON_SELECT_BG_H
#define ICON_SELECT_BG_H

#include <stdint.h>
#include <gbdk/platform.h>

#define ICON_SELECT_BG_TILE_COUNT {len(tiles)}
#define ICON_SELECT_SPR_TILE_COUNT {len(spr)}
#define ICON_SELECT_DMG_SWATCH_BASE {dmg_swatch_base}
#define ICON_SELECT_DMG_BOX_TILE {dmg_swatch_base + 4}
#define ICON_SELECT_SWATCH_COL {SWATCH_COL}
#define ICON_SELECT_DMG_SWATCH_COL {SWATCH_COL + 4}   // DMG: the 4 shades, white .. black
#define ICON_SELECT_SWATCH_ROW0 {SWATCH_ROWS[0]}
#define ICON_SELECT_SLOT_X0 {SLOT_X0}
#define ICON_SELECT_SLOT_Y0 {SLOT_Y0}
#define ICON_SELECT_SLOT_PITCH {SLOT_PITCH}
#define ICON_SELECT_BADGE_X0 {BADGE_X0}
#define ICON_SELECT_BADGE_Y {BADGE_Y}
#define ICON_SELECT_BADGE_PITCH {BADGE_PITCH}
#define ICON_SELECT_PAGE_TILE_BASE {PAGE_TILE_BASE}
#define ICON_SELECT_PAGE_TILE_COUNT {GRID_W * GRID_H}

BANKREF_EXTERN(icon_select_bg)
extern const uint8_t icon_select_bg_tiles[];
extern const uint8_t icon_select_bg_map[];
extern const uint8_t icon_select_bg_attrmap[];
extern const uint8_t icon_select_spr_tiles[];

#endif // ICON_SELECT_BG_H
""")
    flat = b"".join(tiles)
    mapb = bytes(v for row in mp for v in row)
    attb = bytes(v for row in A for v in row)
    (ROOT / "src" / "graphics" / "icon_select_bg.c").write_text(f"""#pragma bank 24
// Generated by tools/build_icon_select_gfx.py. Do not edit.

#include "icon_select_bg.h"

BANKREF(icon_select_bg)

const uint8_t icon_select_bg_tiles[{len(flat)}] = {{
{carr(flat)}
}};

const uint8_t icon_select_bg_map[20 * 18] = {{
{carr(mapb, 20)}
}};

const uint8_t icon_select_bg_attrmap[20 * 18] = {{
{carr(attb, 20)}
}};

const uint8_t icon_select_spr_tiles[{len(spr) * 16}] = {{
{carr(b"".join(spr))}
}};
""")

    (ROOT / "include" / "icon_catalog.h").write_text(f"""// Generated by tools/build_icon_select_gfx.py. Do not edit.
#ifndef ICON_CATALOG_H
#define ICON_CATALOG_H

#include <stdint.h>
#include <gbdk/platform.h>
#include <gb/cgb.h>

#define NUM_CUBE_ICONS {NUM_ICONS}
#define ICONS_PER_PAGE {ICONS_PER_PAGE}
#define NUM_ICON_PAGES {NUM_PAGES}
#define NUM_GAMEMODE_TABS {len(GM_ORDER)}
#define NUM_PALETTE_COLORS {len(COLORS)}
#define PREVIEW_TILE_BASE {PREVIEW_TILE_BASE}
#define PREVIEW_TILE_COUNT 16

BANKREF_EXTERN(icon_catalog)
// Bank 24 data: read it from bank 24 code, or use icon_color() from other banks
extern const palette_color_t icon_palette_colors[NUM_PALETTE_COLORS];
extern const uint8_t icon_dmg_shades[NUM_PALETTE_COLORS];

palette_color_t icon_color(uint8_t idx) BANKED;

// src/graphics/icon_previews.c (bank {PREVIEWS_BANK}): load an icon's preview tiles (its colours 0..3
// drawn as c0..c3: secondary, page, primary, black)
void icon_preview_load(uint8_t icon, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3) BANKED;
// src/graphics/icon_pages.c (bank {PAGES_BANK}): a page of the icon box
void icon_page_load(uint8_t page) BANKED;

#endif // ICON_CATALOG_H
""")
    cols = "\n".join(f"    RGB8({r:3d}, {g:3d}, {b:3d}), // {i}: {n}" for i, (n, (r, g, b), _) in enumerate(COLORS))
    shades = ", ".join(str(s) for _, _, s in COLORS)
    prevs = "\n".join(f"    // {i}\n    {{\n{carr(b)}\n    }}," for i, b in enumerate(preview_blobs))
    (ROOT / "src" / "graphics" / "icon_catalog.c").write_text(f"""#pragma bank 24
// Generated by tools/build_icon_select_gfx.py. Do not edit.

#include "icon_catalog.h"

BANKREF(icon_catalog)

const palette_color_t icon_palette_colors[NUM_PALETTE_COLORS] = {{
{cols}
}};

const uint8_t icon_dmg_shades[NUM_PALETTE_COLORS] = {{ {shades} }};

palette_color_t icon_color(uint8_t idx) BANKED {{
    return icon_palette_colors[idx < NUM_PALETTE_COLORS ? idx : 0];
}}
""")
    pages = "\n".join(f"    // {i}\n    {{\n{carr(b)}\n    }}," for i, b in enumerate(page_blobs))
    (ROOT / "src" / "graphics" / "icon_previews.c").write_text(f"""#pragma bank {PREVIEWS_BANK}
// Generated by tools/build_icon_select_gfx.py. Do not edit.

#include <gb/gb.h>
#include "icon_catalog.h"

static const uint8_t icon_preview_tiles[NUM_CUBE_ICONS][PREVIEW_TILE_COUNT * 16] = {{
{prevs}
}};

void icon_preview_load(uint8_t icon, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3) BANKED {{
    static uint8_t buf[PREVIEW_TILE_COUNT * 16];
    const uint8_t *src = icon_preview_tiles[icon];
    for (uint16_t i = 0; i < PREVIEW_TILE_COUNT * 16u; i += 2) {{
        uint8_t lo = src[i], hi = src[i + 1];
        uint8_t m[4];
        m[0] = (uint8_t)~(lo | hi); m[1] = (uint8_t)(lo & ~hi); m[2] = (uint8_t)(hi & ~lo); m[3] = (uint8_t)(lo & hi);
        uint8_t nlo = 0, nhi = 0;
        if (c0 & 1) nlo |= m[0];
        if (c0 & 2) nhi |= m[0];
        if (c1 & 1) nlo |= m[1];
        if (c1 & 2) nhi |= m[1];
        if (c2 & 1) nlo |= m[2];
        if (c2 & 2) nhi |= m[2];
        if (c3 & 1) nlo |= m[3];
        if (c3 & 2) nhi |= m[3];
        buf[i] = nlo; buf[i + 1] = nhi;
    }}
    set_bkg_data(PREVIEW_TILE_BASE, PREVIEW_TILE_COUNT, buf);
}}
""")
    (ROOT / "src" / "graphics" / "icon_pages.c").write_text(f"""#pragma bank {PAGES_BANK}
// Generated by tools/build_icon_select_gfx.py. Do not edit.

#include <gb/gb.h>
#include "icon_catalog.h"
#include "icon_select_bg.h"

static const uint8_t icon_page_tiles[NUM_ICON_PAGES][ICON_SELECT_PAGE_TILE_COUNT * 16] = {{
{pages}
}};

void icon_page_load(uint8_t page) BANKED {{
    set_bkg_data(ICON_SELECT_PAGE_TILE_BASE, ICON_SELECT_PAGE_TILE_COUNT, icon_page_tiles[page]);
}}
""")
    print(f"{len(tiles)} bg tiles ({dmg_swatch_base} + 4 DMG swatches + box), {len(spr)} sprite tiles")


if __name__ == "__main__":
    main()
