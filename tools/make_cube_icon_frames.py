"""In-game rotation frames of the cube icons (icon select icons 0..25).

    python tools/make_cube_icon_frames.py            # frames PNG -> C (creates the PNG if missing)
    python tools/make_cube_icon_frames.py --regen    # (re)make the frames PNG

levels/chr_data/cube_icon_frames.png: icon 0, the original cube, 24 frames of 16x16: 0, 7.5, ...
172.5 degrees clockwise (the game draws 180..352.5 as these flipped both ways). Grey level =
sprite colour: white transparent, light grey 1 (secondary colour), dark grey 2 (primary
colour), black 3. Its hand-drawn 15 degree frames from sprite_tiles.png; in between, the frame
before turned 7.5 degrees (RotSprite) with its outline cleaned up (between). Can be touched up by
hand: --regen overwrites it.

levels/chr_data/fd_cube_frames.png: icons 1..26, the Famidash cubes (famidash-main next to this
repo: GRAPHICS/Icons/bankicon01..0E.chr - not 00, its default cube -, the 10 contest winners,
starfox.chr, cat.chr), one 16 px row each, 24 frames of 16x16 like icon 0: 0, 7.5, ... 172.5
degrees clockwise (cat.chr: an 8 KB sheet with 22.5 degree frames, see fd_sheet_frames). The 15 degree ones: 0..75 are Famidash's hand-drawn quarter turn, 90..165 the same
turned 90 degrees (exact, pixel for pixel), or the quarter again for an icon that looks the same
turned or spins in 3D (see fd_frames). The ones in between like icon 0's, or the frame before
again for the icons drawn spinning in 3D (FD_3D). 14x14 at
(1, 1) like the original cube. Made when missing or with --regen (needs famidash-main then).

levels/chr_data/ship_frames.png: the ship, 31 frames of 16x16 from 45 degrees nose down to 45 up
in 3 degree steps (same colours). The old hand-drawn ship frames at 45, 24, 0, -24, -45 degrees,
RotSprite of the level one in between; made when missing or with --regen.

levels/chr_data/ball_frames.png: the ball, 24 frames like a cube icon. Its old hand-drawn frames at
0 and 30 degrees, RotSprite in between; made when missing or with --regen.

src/graphics/cube_icon_frames.c (bank 61: icon 0, ship, ball) and cube_fd_frames_0.c /
cube_fd_frames_1.c .. cube_fd_frames_4.c (banks 62 / 63 / 66 / 67 / 68: 10 icons each, the last 5), nothing
else in their banks, not even
code (GDMA needs 16 byte aligned data): per frame 4 sprite tiles, left 8x16 pair then right pair
(64 bytes). The gameplay VBlank handler copies the frame shown into sprite tiles 0..3 / 4..7.
"""
import math
import sys
from collections import deque
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FRAMES = ROOT / "levels" / "chr_data" / "cube_icon_frames.png"
FD_PNG = ROOT / "levels" / "chr_data" / "fd_cube_frames.png"
FD_ROOT = ROOT.parent / "famidash-main"
FD_CHR = [f"GRAPHICS/Icons/bankicon{i:02X}.chr" for i in range(1, 15)] + \
         [f"fan icon collection/CONTEST WINNERS/contest{i:X}.chr" for i in range(1, 11)] + \
         ["fan icon collection/starfox.chr"]
CUSTOM_ICONS = [
    (ROOT / "levels" / "chr_data" / "improved_cat_icon.chr", [0, 1, 16, 17]),
    (ROOT / "levels" / "chr_data" / "pbaxx.chr", [0, 1, 16, 17]),
    (ROOT / "levels" / "chr_data" / "tiny_coins_cube_3.chr", [32, 33, 48, 49]),
]
# tiny_coins_more_cubes.chr: 18 cubes in 2x2 tile blocks, 5 / 5 / 4 / 4 per row at tile rows 2, 4, 6, 8;
# the first one is tiny_coins_cube_3.chr above
MORE_CHR = ROOT / "levels" / "chr_data" / "tiny_coins_more_cubes.chr"
for _r, _n in ((2, 5), (4, 5), (6, 4), (8, 4)):
    for _c in range(_n):
        if (_r, _c) != (2, 0):
            _t = 16 * _r + 2 * _c
            CUSTOM_ICONS.append((MORE_CHR, [_t, _t + 1, _t + 16, _t + 17]))

FD_COUNT, FD_FRAMES, FD_PER_BANK, FD_BANKS = len(FD_CHR) + len(CUSTOM_ICONS), 24, 10, (62, 63, 66, 67, 68)
# drawn spinning in 3D, not in the picture's plane: no turned frames in between
FD_3D = {"contest1.chr", "contest5.chr", "contest8.chr"}
OUT_C = ROOT / "src" / "graphics" / "cube_icon_frames.c"
OUT_FD_C = ROOT / "src" / "graphics" / "cube_fd_frames_{}.c"
OUT_H = ROOT / "include" / "cube_icon_frames.h"
NUM_FRAMES = 24
SHIP_PNG = ROOT / "levels" / "chr_data" / "ship_frames.png"
SHIP_FRAMES = 31                     # 45 .. -45 degrees in 3 degree steps
SHIP_IDX = lambda deg: (45 - deg) // 3
HAND_SHIP = {0: 45, 1: 24, 3: 0, 5: -24, 6: -45}   # old hand-drawn frame: its angle (2, 4 were generated)
BALL_PNG = ROOT / "levels" / "chr_data" / "ball_frames.png"
HAND_BALL = {6: 0, 8: 4}             # sprite_tiles.png pair of an old ball frame: its frame (x 7.5 degrees)
SCROLL_SPEED_FP = 714                # gameplay.c: x speed, 8.8 px per frame
STEP = 7.5   # degrees per frame
SHADE = [255, 170, 85, 0]


def scale2x(g):
    h, w = len(g), len(g[0])
    o = [[0] * (2 * w) for _ in range(2 * h)]
    for y in range(h):
        for x in range(w):
            e = g[y][x]
            b = g[y - 1][x] if y > 0 else e
            h_ = g[y + 1][x] if y < h - 1 else e
            d = g[y][x - 1] if x > 0 else e
            f = g[y][x + 1] if x < w - 1 else e
            if b != h_ and d != f:
                o[2 * y][2 * x] = d if d == b else e
                o[2 * y][2 * x + 1] = f if b == f else e
                o[2 * y + 1][2 * x] = d if d == h_ else e
                o[2 * y + 1][2 * x + 1] = f if h_ == f else e
            else:
                o[2 * y][2 * x] = o[2 * y][2 * x + 1] = o[2 * y + 1][2 * x] = o[2 * y + 1][2 * x + 1] = e
    return o


def rotate(canvas, angles, cx=8.0, cy=8.0):
    """RotSprite: a 16x16 canvas turned clockwise by each angle (degrees) around (cx, cy)."""
    big = scale2x(scale2x(scale2x(canvas)))   # 128x128
    frames = []
    for deg in angles:
        a = math.radians(deg)
        ca, sa = math.cos(a), math.sin(a)
        f = [[0] * 16 for _ in range(16)]
        for y in range(16):
            for x in range(16):
                # clockwise on screen: sample the source rotated back
                px, py = x + 0.5 - cx, y + 0.5 - cy
                sx, sy = px * ca + py * sa + cx, -px * sa + py * ca + cy
                bx, by = int(math.floor(sx * 8)), int(math.floor(sy * 8))
                if 0 <= bx < 128 and 0 <= by < 128:
                    f[y][x] = big[by][bx]
        frames.append(f)
    return frames


def clean_outline(f):
    """A turned frame tidied up: lone pixels sticking out of the silhouette removed and its edge
    redrawn as a 1 px outline (colour 3)."""
    g = [row[:] for row in f]
    near = ((1, 0), (-1, 0), (0, 1), (0, -1))
    inside = lambda x, y: 0 <= x < 16 and 0 <= y < 16 and g[y][x] != 0
    for _ in range(2):
        for y in range(16):
            for x in range(16):
                if g[y][x] and sum(inside(x + dx, y + dy) for dx, dy in near) <= 1:
                    g[y][x] = 0
    out = [row[:] for row in g]
    for y in range(16):
        for x in range(16):
            if g[y][x] and not all(inside(x + dx, y + dy) for dx, dy in near):
                out[y][x] = 3
    return out


def between(f):
    """The frame 7.5 degrees after a hand-drawn one: it turned (RotSprite), outline cleaned."""
    return clean_outline(rotate(f, [STEP])[0])


def ship_frames():
    """The ship: frame k is tilted 45 - 3k degrees clockwise (0: nose steep down, 15: level, 30:
    steep up). The hand-drawn frames of sprite_tiles.png (pairs 18.. of tiles 36..63) at their
    angles, RotSprite of the level one in between."""
    st = Image.open(ROOT / "levels" / "chr_data" / "sprite_tiles.png").convert("L")
    idx = lambda v: min(range(4), key=lambda i: abs(v - SHADE[i]))

    def hand(f):   # old ship frame f (0 steep down .. 3 level .. 6 steep up), 16x16
        g = [[0] * 16 for _ in range(16)]
        for half in (0, 1):
            pr = 18 + 2 * f + half
            for y in range(16):
                for x in range(8):
                    g[y][half * 8 + x] = idx(st.getpixel(((pr % 16) * 8 + x, (pr // 16) * 16 + y)))
        return g
    gen = rotate(hand(3), [45 - 3 * k for k in range(SHIP_FRAMES)], 8.0, 8.5)
    keep = {SHIP_IDX(a): hand(f) for f, a in HAND_SHIP.items()}
    return [keep.get(k, gen[k]) for k in range(SHIP_FRAMES)]


def default_cube():
    """Icon 0, the original cube: its hand-drawn 15 degree frames from sprite_tiles.png (the
    metasprites of src/graphics/icon1.c composed into 16x16), each followed by itself turned 7.5
    degrees (between)."""
    st = Image.open(ROOT / "levels" / "chr_data" / "sprite_tiles.png").convert("L")
    idx = lambda v: min(range(4), key=lambda i: abs(v - SHADE[i]))

    def pair(n, fx, fy):   # 8x16 image of tile pair n
        return [[idx(st.getpixel((n * 8 + (7 - x if fx else x), 15 - y if fy else y))) for x in range(8)]
                for y in range(16)]
    # frames 0..5 of a quarter turn: (left pair, flip y), (right pair = same image, flips)
    quarter = [(0, 0, 0, 1, 0), (1, 0, 1, 1, 1), (2, 0, 2, 1, 1), (3, 0, 3, 1, 0), (2, 1, 2, 1, 0), (1, 1, 1, 1, 0)]
    hand = []
    for li, lfy, ri, rfx, rfy in quarter:
        L, R = pair(li, 0, lfy), pair(ri, rfx, rfy)
        hand.append([L[y] + R[y] for y in range(16)])
    # the original cube looks the same turned 90 degrees: 90..165 = 0..75
    hand = hand + hand
    out = []
    for f in hand:
        out += [f, between(f)]
    return out


def ball_frames():
    """The ball: 24 frames, 0..172.5 degrees clockwise (the game draws the other half flipped
    both ways). Its old hand-drawn frames (left half; the right half is it turned 180 degrees)
    at 0 and 30 degrees, RotSprite of the first one in between."""
    st = Image.open(ROOT / "levels" / "chr_data" / "sprite_tiles.png").convert("L")
    idx = lambda v: min(range(4), key=lambda i: abs(v - SHADE[i]))

    def hand(pr):
        L = [[idx(st.getpixel(((pr % 16) * 8 + x, (pr // 16) * 16 + y))) for x in range(8)] for y in range(16)]
        return [L[y] + [L[15 - y][7 - x] for x in range(8)] for y in range(16)]
    gen = rotate(hand(6), [STEP * k for k in range(NUM_FRAMES)])
    keep = {k: hand(pr) for pr, k in HAND_BALL.items()}
    return [keep.get(k, gen[k]) for k in range(NUM_FRAMES)]


def fd_frames(path):
    """A Famidash icon bank (NES CHR, 8x16 sprites): cube frames 0..5 (0..75 degrees) are tile
    quads 0..5 (left pair, right pair); 90..165 degrees = them turned 90 degrees clockwise; each
    followed by itself turned 7.5 degrees (between).
    Famidash colours: 1 outline (black), 2 colour 1 (primary), 3 colour 2 (secondary)."""
    d = path.read_bytes()
    col = {0: 0, 1: 3, 2: 2, 3: 1}
    if len(d) == 8192:
        return fd_sheet_frames(d, col)

    def frame(k):
        g = [[0] * 16 for _ in range(16)]
        for h in range(2):
            for v in range(2):
                t = 4 * k + 2 * h + v
                for y in range(8):
                    for x in range(8):
                        c = ((d[t * 16 + y] >> (7 - x)) & 1) | (((d[t * 16 + 8 + y] >> (7 - x)) & 1) << 1)
                        g[v * 8 + y][h * 8 + x] = col[c]
        return g
    q = [frame(k) for k in range(6)]
    turn = lambda f: [[f[15 - x][y] for x in range(16)] for y in range(16)]
    # an icon that looks the same turned 90 degrees, or one not drawn turning in the picture's
    # plane (its 90 degree frame is not its first turned: a 3D spin): the quarter turn repeats
    hand = q + q if (turn(q[0]) == q[0] or frame(6) != turn(q[0])) else q + [turn(f) for f in q]
    out = []
    for f in hand:
        out += [f, f if path.name in FD_3D else between(f)]
    return out


def fd_sheet_frames(d, col):
    """An 8 KB pattern sheet (8x8 tiles in order, 16 a row: cat.chr) with a cube's hand-drawn
    frames at 0, 22.5, 45, 67.5 and 90 degrees: 2x2 tile blocks, tiles 80 + 2f, 81 + 2f over
    96 + 2f, 97 + 2f. The frames in between: the nearest hand-drawn one turned (RotSprite,
    outline cleaned); 90..172.5 degrees = 0..82.5 turned 90 degrees."""
    def frame(f):
        g = [[0] * 16 for _ in range(16)]
        for v, base in ((0, 80), (1, 96)):
            for h in range(2):
                t = base + 2 * f + h
                for y in range(8):
                    for x in range(8):
                        c = ((d[t * 16 + y] >> (7 - x)) & 1) | (((d[t * 16 + 8 + y] >> (7 - x)) & 1) << 1)
                        g[v * 8 + y][h * 8 + x] = col[c]
        return g
    def fill_inside(g):
        # colour 0 inside the outline (the face, between the legs) is the sheet's white: colour 2
        # (secondary, sprite colour 1); only the colour 0 reached from the edges is transparent
        out = set()
        todo = deque((x, y) for y in range(16) for x in range(16)
                     if (x in (0, 15) or y in (0, 15)) and g[y][x] == 0)
        while todo:
            x, y = todo.popleft()
            if (x, y) in out or not (0 <= x < 16 and 0 <= y < 16) or g[y][x] != 0:
                continue
            out.add((x, y))
            todo.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
        return [[1 if g[y][x] == 0 and (x, y) not in out else g[y][x] for x in range(16)] for y in range(16)]
    hand = [fill_inside(frame(f)) for f in range(5)]
    quarter = []
    for k in range(NUM_FRAMES // 2):
        deg = k * STEP
        h = int(deg / 22.5 + 0.5)
        diff = deg - h * 22.5
        quarter.append(hand[h] if diff == 0 else clean_outline(rotate(hand[h], [diff])[0]))
    turn = lambda f: [[f[15 - x][y] for x in range(16)] for y in range(16)]
    return quarter + [turn(f) for f in quarter]


def decode_tile_nes(b, col):
    pixels = []
    for y in range(8):
        low, high = b[y], b[8 + y]
        row = []
        for x in range(8):
            bit = 7 - x
            c = (((high >> bit) & 1) << 1) | ((low >> bit) & 1)
            row.append(col[c])
        pixels.append(row)
    return pixels


def custom_frames(path, tiles):
    d = path.read_bytes()
    col = {0: 0, 1: 3, 2: 1, 3: 2}
    t = [decode_tile_nes(d[i * 16:(i + 1) * 16], col) for i in tiles]
    grid = [[0] * 16 for _ in range(16)]
    for y in range(8):
        for x in range(8):
            grid[y][x] = t[0][y][x]
            grid[y][8 + x] = t[1][y][x]
            grid[8 + y][x] = t[2][y][x]
            grid[8 + y][8 + x] = t[3][y][x]
    quarter = [grid]
    for k in range(1, NUM_FRAMES // 2):
        deg = k * STEP
        quarter.append(clean_outline(rotate(grid, [deg])[0]))
    turn = lambda f: [[f[15 - x][y] for x in range(16)] for y in range(16)]
    return quarter + [turn(f) for f in quarter]


def main():
    if "--regen" in sys.argv or not FRAMES.exists():
        img = Image.new("L", (NUM_FRAMES * 16, 16), 255)
        for k, f in enumerate(default_cube()):
            for y in range(16):
                for x in range(16):
                    img.putpixel((k * 16 + x, y), SHADE[f[y][x]])
        img.save(FRAMES)
        print("wrote", FRAMES)

    # made when missing or with --regen; icons added to FD_CHR / CUSTOM_ICONS since: their rows are appended
    # (the rows already there may have been touched up by hand)
    old = None if "--regen" in sys.argv or not FD_PNG.exists() else Image.open(FD_PNG).convert("L")
    if old is None or old.height < FD_COUNT * 16:
        img = Image.new("L", (FD_FRAMES * 16, FD_COUNT * 16), 255)
        first = 0
        if old is not None:
            img.paste(old, (0, 0))
            first = old.height // 16
        for r in range(first, len(FD_CHR)):
            for k, f in enumerate(fd_frames(FD_ROOT / FD_CHR[r])):
                for y in range(16):
                    for x in range(16):
                        img.putpixel((k * 16 + x, r * 16 + y), SHADE[f[y][x]])
        for i, (path, tiles) in enumerate(CUSTOM_ICONS):
            r = len(FD_CHR) + i
            if r >= first:
                for k, f in enumerate(custom_frames(path, tiles)):
                    for y in range(16):
                        for x in range(16):
                            img.putpixel((k * 16 + x, r * 16 + y), SHADE[f[y][x]])
        img.save(FD_PNG)
        print("wrote", FD_PNG)
    else:
        img = old
        for i, (path, tiles) in enumerate(CUSTOM_ICONS):
            r = len(FD_CHR) + i
            for k, f in enumerate(custom_frames(path, tiles)):
                for y in range(16):
                    for x in range(16):
                        img.putpixel((k * 16 + x, r * 16 + y), SHADE[f[y][x]])
        img.save(FD_PNG)
        print("updated custom icons in", FD_PNG)

    if "--regen" in sys.argv or not SHIP_PNG.exists():
        img = Image.new("L", (SHIP_FRAMES * 16, 16), 255)
        for k, f in enumerate(ship_frames()):
            for y in range(16):
                for x in range(16):
                    img.putpixel((k * 16 + x, y), SHADE[f[y][x]])
        img.save(SHIP_PNG)
        print("wrote", SHIP_PNG)

    if "--regen" in sys.argv or not BALL_PNG.exists():
        img = Image.new("L", (NUM_FRAMES * 16, 16), 255)
        for k, f in enumerate(ball_frames()):
            for y in range(16):
                for x in range(16):
                    img.putpixel((k * 16 + x, y), SHADE[f[y][x]])
        img.save(BALL_PNG)
        print("wrote", BALL_PNG)

    idx = lambda v: min(range(4), key=lambda i: abs(v - SHADE[i]))
    data = bytearray()

    def add_frame(img, x0, y0):   # left 8x16 pair, right pair
        nonlocal data
        for half in (0, 8):
            for ty in (0, 8):
                for y in range(8):
                    lo = hi = 0
                    for x in range(8):
                        v = idx(img.getpixel((x0 + half + x, y0 + ty + y)))
                        lo = (lo << 1) | (v & 1)
                        hi = (hi << 1) | (v >> 1)
                    data += bytes([lo, hi])
    img = Image.open(FRAMES).convert("L")
    for k in range(NUM_FRAMES):
        add_frame(img, k * 16, 0)
    ship_first = len(data) // 64
    img = Image.open(SHIP_PNG).convert("L")
    for k in range(SHIP_FRAMES):
        add_frame(img, k * 16, 0)
    ball_first = len(data) // 64
    img = Image.open(BALL_PNG).convert("L")
    for k in range(NUM_FRAMES):
        add_frame(img, k * 16, 0)
    # |vertical speed| (8.8) from which the ship tilts k steps (3k degrees, rounded): its flight
    # direction, atan(vy / x speed)
    steps = [round(SCROLL_SPEED_FP * math.tan(math.radians(3 * k - 1.5))) for k in range(1, 16)]

    def carr(b):
        return ",\n".join("    " + ", ".join(f"0x{v:02X}" for v in b[i:i + 16]) for i in range(0, len(b), 16))
    main_len = len(data)
    lines = carr(data)

    fd_img = Image.open(FD_PNG).convert("L")
    fd_banks = (FD_COUNT + FD_PER_BANK - 1) // FD_PER_BANK
    for b in range(fd_banks):
        data = bytearray()
        for r in range(b * FD_PER_BANK, min(FD_COUNT, (b + 1) * FD_PER_BANK)):
            for k in range(FD_FRAMES):
                add_frame(fd_img, k * 16, r * 16)
        Path(str(OUT_FD_C).format(b)).write_text(f"""#pragma bank {FD_BANKS[b]}
// Generated by tools/make_cube_icon_frames.py from levels/chr_data/fd_cube_frames.png. Do not edit.
// The only thing in its bank (no code either), so 16 byte aligned: GDMA source.

#include <gb/gb.h>
#include "cube_icon_frames.h"

BANKREF(cube_fd_frames_{b})

const uint8_t cube_fd_frames_{b}[{len(data)}] = {{
{carr(data)}
}};
""")

    OUT_C.write_text(f"""#pragma bank 61
// Generated by tools/make_cube_icon_frames.py from levels/chr_data/cube_icon_frames.png,
// ship_frames.png and ball_frames.png. Do not edit.
// The only thing in its bank (no code either), so 16 byte aligned: GDMA source.

#include <gb/gb.h>
#include "cube_icon_frames.h"

BANKREF(cube_icon_frames)

const uint8_t cube_icon_frames[{main_len}] = {{
{lines}
}};
""")
    OUT_H.write_text(f"""// Generated by tools/make_cube_icon_frames.py. Do not edit.
#ifndef CUBE_ICON_FRAMES_H
#define CUBE_ICON_FRAMES_H

#include <gb/gb.h>
#include <stdint.h>

#define CUBE_ICON_FRAMES {NUM_FRAMES}   // 0..172.5 degrees in 7.5 degree steps; 180..352.5 = flipped both ways

BANKREF_EXTERN(cube_icon_frames)
extern const uint8_t cube_icon_frames[];   // per frame: left 8x16 pair, right pair (64 bytes)

// Icon 0 (the original cube): frame 0..23 in cube_icon_frames
#define CUBE_ICON0_FRAME(frame) (cube_icon_frames + (uint16_t)(frame) * 64u)

// Icons 1..{FD_COUNT} (Famidash): {FD_FRAMES} frames like icon 0, {FD_PER_BANK} icons per bank
#define CUBE_FD_COUNT {FD_COUNT}
#define CUBE_FD_FRAMES {FD_FRAMES}
#define CUBE_FD_PER_BANK {FD_PER_BANK}
""" + "".join(f"BANKREF_EXTERN(cube_fd_frames_{b})\nextern const uint8_t cube_fd_frames_{b}[];\n" for b in range(fd_banks)) + f"""

// Ship frames, after the cube ones: frame k tilted 45 - 3k degrees (0 nose steep down, level, 30 up)
#define SHIP_FRAME_COUNT {SHIP_FRAMES}
#define SHIP_FRAME_LEVEL {SHIP_FRAMES // 2}
#define SHIP_FRAME(k) (cube_icon_frames + ((uint16_t){ship_first} + (k)) * 64u)
// Ball frames, after the ship: 0..172.5 degrees like a cube icon (drawn at x + 0, not x - 1)
#define BALL_FRAME(k) (cube_icon_frames + ((uint16_t){ball_first} + (k)) * 64u)

// |vel_y| (8.8) from which the ship tilts one more step: the frame for its flight direction. An
// initializer, not an array: anything defined here would also land in bank 61 ahead of the frames
#define SHIP_TILT_VY {{ {", ".join(str(v) for v in steps)} }}

#endif
""")
    print(f"icon 0 x {NUM_FRAMES} + {SHIP_FRAMES} ship + {NUM_FRAMES} ball frames: {main_len} bytes, "
          f"{FD_COUNT} Famidash icons x {FD_FRAMES} frames in {fd_banks} banks")


if __name__ == "__main__":
    main()
