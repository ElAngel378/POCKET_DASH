"""The mini cube icons: the main menu's, and the player's mini cube in the levels.

    python tools/make_mini_cube_icons.py

levels/chr_data/game_art/mini_cube_icons.chr (NES CHR, 8x8 icons, blank tiles skipped; colours 1
outline, 2 primary, 3 secondary), and the game's own mini cube (sprite_tiles.png, the top half of
pair MINI_PLAYER_TILE_BASE / 2) for the icons with none ->

- src/graphics/mini_cube_frames.c (bank 60, alone in it: GDMA source, 16 byte aligned): per mini
  icon its turned images like the big cube's, 0..165 degrees clockwise in 15 degree steps (the other
  half turn: those flipped both ways), streamed into the player's tile buffer while it is mini. 32
  bytes an image: the image, then an empty tile (the bottom half of its 8x16 sprite).
- src/graphics/mini_cube_icons.c (bank 29): which mini icon each big icon gets (SLOT_MINI), and the
  upright image of each, put in VRAM for the frames before the first one is copied.
- include/mini_cube_frames.h
- levels/chr_data/game_art/mini_cube_frames.png: all the images, to look at (not read by the build).
"""
import re
from pathlib import Path

from PIL import Image

from make_cube_icon_frames import voted, SHADE

ROOT = Path(__file__).resolve().parent.parent
CHR = ROOT / "levels" / "chr_data" / "game_art" / "mini_cube_icons.chr"
SPRITES = ROOT / "levels" / "chr_data" / "game_art" / "sprite_tiles.png"
OUT_C = ROOT / "src" / "graphics" / "mini_cube_icons.c"
OUT_FRAMES = ROOT / "src" / "graphics" / "mini_cube_frames.c"
OUT_H = ROOT / "include" / "mini_cube_frames.h"
OUT_PNG = ROOT / "levels" / "chr_data" / "game_art" / "mini_cube_frames.png"
BANK = 60
FRAMES = 12
ANGLES = [15 * k for k in range(FRAMES)]
DEFAULT_PAIR = 58     # include/famidash_sprites.h MINI_PLAYER_TILE_BASE / 2
# Icon box slot -> mini icon. A cube with none gets the game's default mini cube.
SLOT_MINI = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 9: 9, 10: 10, 11: 11, 12: 12, 13: 13,
             18: 14, 28: 15, 29: 16, 30: 17, 31: 18, 33: 19, 34: 20, 35: 21, 36: 22, 37: 23, 38: 24,
             39: 25, 40: 26, 41: 27, 42: 28, 43: 29, 44: 30, 45: 31}
COLOURS = {0: 0, 1: 3, 2: 2, 3: 1}   # to the game's: 1 secondary, 2 primary, 3 black (like Famidash's icons)


def decode(tile):
    return [[COLOURS[((tile[y] >> (7 - x)) & 1) | (((tile[y + 8] >> (7 - x)) & 1) << 1)] for x in range(8)]
            for y in range(8)]


def encode(img):
    out = []
    for row in img:
        lo = hi = 0
        for v in row:
            lo = (lo << 1) | (v & 1)
            hi = (hi << 1) | (v >> 1)
        out += [lo, hi]
    return out


def outline(img):
    """Its silhouette's edge redrawn in black: at 8x8 a turned outline breaks up"""
    inside = lambda x, y: 0 <= x < 8 and 0 <= y < 8 and img[y][x] != 0
    return [[3 if v and not all(inside(x + a, y + b) for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1))) else v
             for x, v in enumerate(row)] for y, row in enumerate(img)]


def turned(img):
    """The 12 images: 0 and 90 degrees exact, the others RotSprite-like (voted) with the outline redrawn"""
    out = []
    for angle, f in zip(ANGLES, voted(img, ANGLES, black=1.0)):
        if angle == 0:
            out.append([row[:] for row in img])
        elif angle == 90:
            out.append([[img[7 - x][y] for x in range(8)] for y in range(8)])
        else:
            out.append(outline(f))
    return out


def default_mini():
    st = Image.open(SPRITES).convert("L")
    idx = lambda v: min(range(4), key=lambda i: abs(v - SHADE[i]))
    pr = DEFAULT_PAIR
    return [[idx(st.getpixel(((pr % 16) * 8 + x, (pr // 16) * 16 + y))) for x in range(8)] for y in range(8)]


def carr(data):
    return ['    ' + ', '.join(f'0x{v:02X}' for v in data[i:i + 16]) + ',' for i in range(0, len(data), 16)]


def main():
    d = CHR.read_bytes()
    icons = [decode(d[i:i + 16]) for i in range(0, len(d), 16) if any(d[i:i + 16])]
    icons.append(default_mini())
    default = len(icons) - 1
    frames, upright = [], []
    sheet = Image.new("L", (FRAMES * 9 * 2, len(icons) * 9), 128)
    for r, img in enumerate(icons):
        upright += encode(img)
        for k, f in enumerate(turned(img)):
            frames += encode(f) + [0] * 16
            for y in range(8):
                for x in range(8):
                    sheet.putpixel((k * 9 + x, r * 9 + y), SHADE[f[y][x]])
                    # the other half turn: flipped both ways
                    sheet.putpixel(((FRAMES + k) * 9 + x, r * 9 + y), SHADE[f[7 - y][7 - x]])
    frames += [0] * 32     # (the 64 byte copy at a respawn reads one image further)
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(OUT_PNG)

    header = (ROOT / "include" / "icon_catalog.h").read_text()
    order = [int(v) for v in re.search(r'#define ICON_ORDER \{(.*?)\}', header).group(1).split(',')]
    per_icon = [default] * len(order)
    for slot, mini in SLOT_MINI.items():
        per_icon[order[slot]] = mini

    OUT_H.write_text(f"""// Generated by tools/make_mini_cube_icons.py. Do not edit.
#ifndef MINI_CUBE_FRAMES_H
#define MINI_CUBE_FRAMES_H

#include <gb/gb.h>
#include <stdint.h>

#define MINI_CUBE_ICONS {len(icons)}      // the last: the game's own mini cube
#define MINI_CUBE_DEFAULT {default}
#define MINI_CUBE_FRAMES {FRAMES}     // 0..165 degrees clockwise in 15 degree steps; 180..345 = flipped both ways

BANKREF_EXTERN(mini_cube_frames)
extern const uint8_t mini_cube_frames[];   // per image: the image tile, an empty tile (32 bytes)
#define MINI_CUBE_FRAME(m, k) (mini_cube_frames + ((uint16_t)(m) * MINI_CUBE_FRAMES + (k)) * 32u)

uint8_t mini_cube_of_icon(uint8_t icon) BANKED;   // the mini icon (0..MINI_CUBE_DEFAULT) of a big icon

#endif
""")
    OUT_FRAMES.write_text('\n'.join([
        f'#pragma bank {BANK}',
        '// Generated by tools/make_mini_cube_icons.py. Do not edit.',
        '// The only thing in its bank (no code either), so 16 byte aligned: GDMA source.',
        '', '#include <gb/gb.h>', '#include "mini_cube_frames.h"', '',
        'BANKREF(mini_cube_frames)', '',
        f'const uint8_t mini_cube_frames[{len(frames)}] = {{'] + carr(frames) + ['};', '']))
    OUT_C.write_text('\n'.join([
        '#pragma bank 29', '// Generated by tools/make_mini_cube_icons.py. Do not edit.', '',
        '#include <gb/gb.h>', '#include <stdint.h>', '#include "famidash_sprites.h"', '#include "mini_cube_frames.h"', '',
        'BANKREF(mini_cube_icons)',
        f'static const uint8_t mini_of_icon[{len(per_icon)}] = {{ ' + ', '.join(map(str, per_icon)) + ' };',
        '// each mini icon upright (the first of its turned images)',
        f'static const uint8_t mini_cube_upright[{len(upright)}] = {{'] + carr(upright) + ['};', '',
        'uint8_t mini_cube_of_icon(uint8_t icon) BANKED {',
        '    return mini_of_icon[icon];',
        '}', '',
        '// The player mini cube of icon upright in VRAM: shown until its first turned image is copied',
        'void mini_cube_icon_load(uint8_t icon) BANKED {',
        '    set_sprite_data(MINI_PLAYER_TILE_BASE, 1, mini_cube_upright + (uint16_t)mini_of_icon[icon] * 16u);',
        '}', '']))
    print(f'{len(icons)} mini icons x {FRAMES} images: {len(frames)} bytes in bank {BANK}')


if __name__ == '__main__':
    main()
