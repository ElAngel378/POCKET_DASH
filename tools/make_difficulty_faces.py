"""Level-select difficulty faces from levels/chr_data/game_art/difficulty_faces.chr.

    python tools/make_difficulty_faces.py

difficulty_faces.chr is Game Boy 2bpp, 16 tiles per 128 px row: ten 16x16 faces in the order
Auto, Easy, Normal, Hard, Harder, Insane, Demon, Insane Demon, NA, Easy Demon. Face k = tiles
2k, 2k+1 (top) and 16+2k, 17+2k (bottom) for k 0..7, then 32+2(k-8).. and 48+2(k-8).. for k 8, 9.
The art only has 4 indices, so they mean different things per face: 1 is black, 3 the light skin
(and the white of eyes/teeth on some faces), 2 the dark skin, and 0 both the area outside the
circle and eye glints/white. 0 pixels connected to the border are "outside" (transparent),
the enclosed ones are white (Hard: its light skin).

They are drawn as sprites (colour 0 transparent), the top and bottom tile rows with their own
sprite palette (3 and 4), which gives the skin gradient and keeps eyes/teeth white:
    CGB  per half one of  A [-, black, white, light]   B [-, black, dark, light]
                          C [-, black, skin, white]  (skin = light on top, dark below)
    DMG  1 white, 2 skin (grey), 3 black; raw 3 is white, raw 2 grey, raw 1 black (OBP0 0xE0)

Writes src/graphics/difficulty_faces.h.
"""
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHR = ROOT / "levels" / "chr_data" / "game_art" / "difficulty_faces.chr"
OUT = ROOT / "src" / "graphics" / "difficulty_faces.h"

NAMES = ["Auto", "Easy", "Normal", "Hard", "Harder", "Insane", "Demon", "Insane Demon", "NA", "Easy Demon"]
HARD = 3
# (light, dark) skin colours
SKIN = [
    ((255, 215, 110), (245, 140, 30)),
    ((60, 205, 255), (20, 100, 235)),
    ((110, 235, 60), (40, 160, 40)),
    ((255, 235, 90), (250, 160, 30)),
    ((245, 140, 40), (235, 50, 30)),
    ((235, 130, 225), (215, 40, 235)),
    ((235, 85, 75), (190, 40, 40)),
    ((200, 50, 50), (120, 20, 25)),
    ((225, 225, 225), (140, 140, 140)),
    ((190, 80, 210), (125, 45, 165)),
]
BLACK, WHITE = (0, 0, 0), (255, 255, 255)
TRANSP = (0, 0, 0)


def main():
    d = CHR.read_bytes()

    def tile_px(t, y, x):
        lo, hi = d[t * 16 + 2 * y], d[t * 16 + 2 * y + 1]
        return ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)

    def face(k):
        top = 2 * k if k < 8 else 32 + 2 * (k - 8)
        return [[tile_px(top + (16 if Y >= 8 else 0) + (1 if X >= 8 else 0), Y % 8, X % 8)
                 for X in range(16)] for Y in range(16)]

    def tile_bytes(px):  # px: 8x8 values -> 16 bytes
        out = []
        for row in px:
            lo = hi = 0
            for v in row:
                lo = (lo << 1) | (v & 1)
                hi = (hi << 1) | (v >> 1)
            out += [lo, hi]
        return out

    cgb_all, dmg_all, pals = [], [], []
    for k in range(10):
        g = face(k)
        outside = set()
        q = deque()
        for i in range(16):
            for x, y in ((i, 0), (i, 15), (0, i), (15, i)):
                if g[y][x] == 0 and (x, y) not in outside:
                    outside.add((x, y))
                    q.append((x, y))
        while q:
            x, y = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                a, b = x + dx, y + dy
                if 0 <= a < 16 and 0 <= b < 16 and g[b][a] == 0 and (a, b) not in outside:
                    outside.add((a, b))
                    q.append((a, b))

        def role(x, y):
            v = g[y][x]
            if v == 0:
                return 'o' if (x, y) in outside else 'w'
            return str(v)

        # palette type per half
        types = []
        for y0 in (0, 8):
            roles = {role(x, y) for y in range(y0, y0 + 8) for x in range(16)}
            if k == HARD:
                t = 'B'
            elif '2' in roles and 'w' in roles:
                t = 'C'
            elif 'w' in roles:
                t = 'A'
            else:
                t = 'B'
            if t == 'A':
                assert '2' not in roles, (NAMES[k], roles)
            types.append(t)

        light, dark = SKIN[k]
        pal = []
        for half, t in enumerate(types):
            if t == 'A':
                pal += [TRANSP, BLACK, WHITE, light]
            elif t == 'B':
                pal += [TRANSP, BLACK, dark, light]
            else:
                pal += [TRANSP, BLACK, light if half == 0 else dark, WHITE]
        pals.append(pal)

        cgb_px = [[0] * 16 for _ in range(16)]
        dmg_px = [[0] * 16 for _ in range(16)]
        for y in range(16):
            t = types[y // 8]
            for x in range(16):
                r = role(x, y)
                if r == 'o':
                    c, m = 0, 0
                elif r == '1':
                    c, m = 1, 3
                elif r == '2':
                    c, m = 2, 2
                elif r == '3':
                    c, m = (3, 1)
                else:  # enclosed 0
                    c, m = (2 if t == 'A' else 3), 1
                if t == 'C' and r == '3':
                    c = 3   # glow/highlight -> white
                cgb_px[y][x], dmg_px[y][x] = c, m

        def quad(px):
            out = []
            for y0, x0 in ((0, 0), (0, 8), (8, 0), (8, 8)):
                out += tile_bytes([row[x0:x0 + 8] for row in px[y0:y0 + 8]])
            return out

        cgb_all.append(quad(cgb_px))
        dmg_all.append(quad(dmg_px))
        print(f"{NAMES[k]:14} top {types[0]} bottom {types[1]}")

    def arr(name, rows, fmt):
        s = f"static const {fmt} {name}[10][{len(rows[0])}] = {{\n"
        for k, row in enumerate(rows):
            s += f"    // {k}: {NAMES[k]}\n    {{\n"
            for i in range(0, len(row), 16):
                s += "        " + ", ".join(f"0x{b:02x}" for b in row[i:i + 16]) + ",\n"
            s += "    },\n"
        return s + "};\n\n"

    text = "// Generated by tools/make_difficulty_faces.py from levels/chr_data/game_art/difficulty_faces.chr\n"
    text += "// Do not edit.\n\n#define NUM_DIFFICULTY_FACES 10\n\n"
    text += "// Sprite tiles TL, TR, BL, BR (2bpp, colour 0 transparent)\n"
    text += arr("difficulty_face_cgb_tiles", cgb_all, "uint8_t")
    text += arr("difficulty_face_dmg_tiles", dmg_all, "uint8_t")
    text += "// CGB sprite palettes 3 (top row) and 4 (bottom row)\n"
    text += "static const palette_color_t difficulty_face_pals[10][8] = {\n"
    for k, pal in enumerate(pals):
        text += f"    // {k}: {NAMES[k]}\n    {{ " + ", ".join(f"RGB8({r}, {g}, {b})" for r, g, b in pal) + " },\n"
    text += "};\n"
    OUT.write_text(text)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
