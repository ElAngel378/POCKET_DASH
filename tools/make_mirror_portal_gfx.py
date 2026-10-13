"""Draws the mirror portals into levels/chr_data/sprite_tiles.png (then run build_sprite_tiles.py).

    python tools/make_mirror_portal_gfx.py

Three rings getting smaller to the right, like GD's, in the look of the other portals: flat
colour, black shadow blocks (three rows on, one off) right of each ring and inside the left edge
of the big one. 32 x 48 px = 4 columns x 3 rows of 8x16 sprites (sp_draw.c draw_oam_mirror_portal).
The art is mirrored top to bottom, so only the top and the middle row are stored: the bottom row
is the top row drawn with S_FLIPY.

    entrance  sprite_tiles.png pairs 104..111 (VRAM tiles 208..223): ring colour 2 (palette 6: orange)
              top row = pairs 104..107, middle row = pairs 108..111
    exit      pair row 18, pairs 0..7 (CGB VRAM tiles 224..239): ring colour 3 (palette 6: blue)
              The exit art is the entrance mirrored left to right; the DMG exit is the
              entrance drawn flipped.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "sprite_tiles.png"
W, H = 32, 48
INK = 1
# (centre x, radius x, radius y, stroke) back to front; the centre y is H / 2
RINGS = [(20.5, 4.0, 11.0, 2), (14.5, 5.5, 16.0, 2), (7.0, 7.0, 23.0, 3)]


def ring(g, cx, rx, ry, t, col):
    cy = H / 2
    outer, inner = set(), set()
    for y in range(H):
        for x in range(W):
            dx, dy = (x + .5 - cx) / rx, (y + .5 - cy) / ry
            if dx * dx + dy * dy <= 1:
                outer.add((x, y))
            ix, iy = (x + .5 - cx) / (rx - t), (y + .5 - cy) / (ry - t)
            if ix * ix + iy * iy <= 1:
                inner.add((x, y))
    body = outer - inner
    for y in range(H):
        if min(y, H - 1 - y) % 4 == 3:      # the gap rows, mirrored top to bottom
            continue
        xs = [x for (x, yy) in body if yy == y]
        if not xs:
            continue
        for k in range(1, 4):               # shadow right of the ring
            if max(xs) + k < W and (max(xs) + k, y) not in outer:
                g[(max(xs) + k, y)] = INK
        ins = [x for (x, yy) in inner if yy == y]
        if ins:                             # and just inside its left edge
            for k in range(2):
                if (min(ins) + k, y) in inner:
                    g[(min(ins) + k, y)] = INK
    for p in body:
        g[p] = col


def art(col):
    g = {}
    for cx, rx, ry, t in RINGS:
        ring(g, cx, rx, ry, t, col)
    return g


def put(img, x0, y0, g):
    """Top row (art y 0..15) at (x0, y0), middle row (16..31) at (x0 + 32, y0), in pair columns."""
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
    img = Image.open(PNG).convert("L")
    put(img, 64, 96, art(2))
    # the exit is the entrance mirrored left to right (drawn with the flip toggled, sp_draw.c)
    put(img, 0, 288, {(W - 1 - x, y): v for (x, y), v in art(3).items()})
    img.save(PNG)
    print("mirror portals drawn into", PNG.name)


if __name__ == "__main__":
    main()
