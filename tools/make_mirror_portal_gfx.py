"""Copies the mirror portals from levels/chr_data/mirror_portals.chr into
levels/chr_data/sprite_tiles.png (then run build_sprite_tiles.py).

    python tools/make_mirror_portal_gfx.py

mirror_portals.chr is NES 2bpp CHR, 16 tiles per row: the entrance (ring colour 2, palette 6:
orange) is the 4x4 tiles at tile column 0, the exit (ring colour 3, palette 6: blue) the 4x4 tiles
at tile column 5. Colour 1 is the black glow. 32 x 48 px = 4 columns x 3 rows of 8x16 sprites
(sp_draw.c draw_oam_mirror_portal). Both are drawn facing the same way; the portal is drawn
flipped in the mirrored view.

    entrance  sprite_tiles.png pairs 104..111 (VRAM tiles 208..223)
              top row = pairs 104..107, middle row = pairs 108..111
    exit      pair row 18, pairs 0..7 (CGB VRAM tiles 224..239), same layout; the art is
              stretched from 32 to 48 rows (the straight middle of the rings repeated)
              (the DMG exit is the entrance drawn flipped)
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "sprite_tiles.png"
CHR = ROOT / "levels" / "chr_data" / "mirror_portals.chr"
W, H = 32, 48


def read_art(data, col0):
    """{(x, y): colour} of the 4x4 tiles starting at tile column col0, row 0."""
    g = {}
    for ty in range(4):
        for tx in range(4):
            t = data[(ty * 16 + col0 + tx) * 16:][:16]
            for y in range(8):
                for x in range(8):
                    v = ((t[y] >> (7 - x)) & 1) | (((t[y + 8] >> (7 - x)) & 1) << 1)
                    if v:
                        g[(tx * 8 + x, ty * 8 + y)] = v
    return stretch(g)


def stretch(g):
    """32 -> 48 rows like the other portals: the middle 16 rows are the ring's straight part
    repeated (shadow rows keep their three on, one off rhythm), mirrored top to bottom."""
    def src(y):
        y = min(y, 47 - y)                  # mirrored top to bottom: the bottom third is the top
        if y < 16:
            return y
        return 17 if y % 3 == 2 else 16     # 16..23: on, off, on, on, off, ...
    return {(x, y): g[(x, src(y))] for y in range(H) for x in range(W) if (x, src(y)) in g}


def put(img, x0, y0, g):
    """Top row (art y 0..15) at (x0, y0), middle row (16..31) at (x0 + 32, y0), in pair columns;
    the bottom row (32..47) is the top row flipped (sp_draw.c)."""
    px = img.load()
    for yy in range(16):
        for xx in range(64):
            px[x0 + xx, y0 + yy] = 255
    for (x, y), v in g.items():
        if y < 16:
            px[x0 + x, y0 + y] = 255 - 85 * v
        elif y < 32:
            px[x0 + 32 + x, y0 + y - 16] = 255 - 85 * v


def main():
    data = CHR.read_bytes()
    img = Image.open(PNG).convert("L")
    put(img, 64, 96, read_art(data, 0))
    put(img, 0, 288, read_art(data, 5))
    img.save(PNG)
    print("mirror portals copied into", PNG.name)


if __name__ == "__main__":
    main()
