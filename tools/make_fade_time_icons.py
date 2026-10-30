"""Draws the colour trigger fade time markers into the Tiled sprite sheet
(levels/chr_data/graphics/sprites.png, cells 246..250; tools/tmx2sprites.py reads them):
a clock over the time, like the speed markers next to them.

    python tools/make_fade_time_icons.py
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "graphics" / "sprites.png"

# 3x5 digits; '.' is one pixel wide
GLYPHS = {
    "0": ["###", "#.#", "#.#", "#.#", "###"],
    "1": [".#.", "##.", ".#.", ".#.", "###"],
    "2": ["###", "..#", "###", "#..", "###"],
    "3": ["###", "..#", "###", "..#", "###"],
    "4": ["#.#", "#.#", "###", "..#", "..#"],
    "5": ["###", "#..", "###", "..#", "###"],
    ".": [".", ".", ".", ".", "#"],
    "s": ["...", "###", "##.", "..#", "###"],
}
# cell, label (the time)
MARKERS = [(246, "0s"), (247, ".5s"), (248, "1s"), (249, "2s"), (250, "4s")]

BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
COLOUR = (40, 120, 255, 255)   # blue: the colour triggers' fade (the speed markers are red)
CLEAR = (0, 0, 0, 0)

# a gradient bar (fade) on top: 12 px from white to the colour
def draw(im, cell, label):
    ox, oy = (cell % 16) * 16, (cell // 16) * 16
    for y in range(16):
        for x in range(16):
            im.putpixel((ox + x, oy + y), CLEAR)
    for x in range(14):
        t = x / 13
        c = tuple(int(WHITE[i] * (1 - t) + COLOUR[i] * t) for i in range(3)) + (255,)
        for y in range(2, 7):
            im.putpixel((ox + 1 + x, oy + y), c)
    for x in range(16):   # outline
        for y in (1, 7):
            if 0 < x < 15:
                im.putpixel((ox + x, oy + y), BLACK)
    for y in range(2, 7):
        im.putpixel((ox, oy + y), BLACK)
        im.putpixel((ox + 15, oy + y), BLACK)
    width = sum(len(GLYPHS[ch][0]) + 1 for ch in label) - 1
    x = ox + (16 - width) // 2
    for ch in label:
        g = GLYPHS[ch]
        for gy, row in enumerate(g):
            for gx, p in enumerate(row):
                if p == "#":
                    im.putpixel((x + gx, oy + 10 + gy), COLOUR)
        x += len(g[0]) + 1


def main():
    im = Image.open(PNG).convert("RGBA")
    for cell, label in MARKERS:
        draw(im, cell, label)
    im.save(PNG)
    print("fade time markers drawn: cells %s" % ", ".join(str(c) for c, _ in MARKERS))


if __name__ == "__main__":
    main()
