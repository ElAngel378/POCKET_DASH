"""Draws the UFO player (1.5) into levels/chr_data/sprite_tiles.png (then run build_sprite_tiles.py).

    python tools/make_ufo_gfx.py

Like Famidash's UFO, one 8x16 tile pair drawn twice (the right half mirrored, S_FLIPX): a small
cube rider (like our ship's) on a flat saucer (Famidash's saucer half, bankgamemodesB.chr $3E/$3F,
did not read as a saucer at this size). The mini UFO is Famidash's 8x8 tile $19 in the top
half of a pair, like the other mini player images.

Famidash colours 1 / 2 / 3 (outline / body / highlight) are our 3 / 2 / 1 (sprite_tiles.png
grey level = Game Boy colour, player palette 0: 3 black outline, 2 primary, 1 secondary).

    UFO       sprite_tiles.png BANK0 pair 7 (tiles 14, 15) -> VRAM sprite tiles 16, 17 (UFO_TILE)
    mini UFO  BANK0 pair 9 (tiles 18, 19)                  -> VRAM sprite tiles 18, 19 (MINI_UFO_TILE)
(the old ball art's second halves were there, never loaded)
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "sprite_tiles.png"
FD = {"1": "3", "2": "2", "3": "1", ".": "."}

RIDER = [                     # left half of a 8x7 cube centred in the 16 px (cols 4..7)
    "........",
    "....3333",
    "....3222",
    "....3233",
    "....3231",
    "....3233",
    "....3222",
    "....3333",
]
SAUCER = [                    # left half of a flat saucer (our colours): rim, body, a band of
    "..333333",              # lights in the secondary colour, the bottom dome
    ".3222222",
    "32222222",
    "33333333",
    "31131131",
    "33333333",
    "..322222",
    "...33333",
]
MINI_FD = [                   # Famidash bankgamemodesB $19, Famidash colours
    "...33...",
    "..3113..",
    ".312213.",
    ".312213.",
    ".111111.",
    "13333331",
    ".122221.",
    "..1111..",
]
UFO = RIDER + SAUCER
MINI = ["".join(FD[c] for c in r) for r in MINI_FD] + ["........"] * 8


def put(img, pair, rows):
    px = img.load()
    x0, y0 = (pair % 16) * 8, (pair // 16) * 16
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            px[x0 + x, y0 + y] = 255 - 85 * (0 if c == "." else int(c))


def main():
    img = Image.open(PNG).convert("L")
    put(img, 7, UFO)
    put(img, 9, MINI)
    img.save(PNG)
    print("UFO drawn into", PNG.name)


if __name__ == "__main__":
    main()
