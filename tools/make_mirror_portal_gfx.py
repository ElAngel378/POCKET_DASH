"""Draws the mirror portals into levels/chr_data/sprite_tiles.png (then run build_sprite_tiles.py).

    python tools/make_mirror_portal_gfx.py

Three rings getting smaller to the right like GD's, in the look of the other portals: flat 3 px
rings, black teeth (three rows on, one off) right of every ring and left of the big one, a black
shadow just inside the big ring's left edge, and a round 4x4 dot in the ring colour above and
below each ring (like the mode portals). 32 x 48 px = 4 columns x 3 rows of 8x16 sprites
(sp_draw.c draw_oam_mirror_portal). The art is mirrored top to bottom, so only the top and the
middle row are stored: the bottom row is the top row drawn with S_FLIPY.

    entrance  sprite_tiles.png pairs 104..111 (VRAM tiles 208..223): ring colour 2 (palette 6: orange)
              top row = pairs 104..107, middle row = pairs 108..111
    exit      pair row 18, pairs 0..7 (CGB VRAM tiles 224..239): ring colour 3 (palette 6: cyan)
              The same art; the DMG exit is the entrance tiles. Both are drawn flipped in the
              mirrored view.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "sprite_tiles.png"
W, H = 32, 48
INK = 1
# back to front: (cx, rx, ry, stroke)
RINGS=[(25.5,3.5,9.5,2),(19.5,5.0,14.5,3),(11.0,8.0,19.5,3)]
def shape(cx,rx,ry,t):
    cy=H/2; outer=set(); inner=set()
    for y in range(H):
        for x in range(W):
            dx,dy=(x+.5-cx)/rx,(y+.5-cy)/ry
            if dx*dx+dy*dy<=1: outer.add((x,y))
            ix,iy=(x+.5-cx)/(rx-t),(y+.5-cy)/(ry-t)
            if ix*ix+iy*iy<=1: inner.add((x,y))
    return outer,inner
def art(col,right=3,left=3):
    g={}; shadow={}; bodies=[]
    for i,(cx,rx,ry,t) in enumerate(RINGS):
        outer,inner=shape(cx,rx,ry,t); body=outer-inner; bodies.append(body)
        for y in range(H):
            if min(y,H-1-y)%4==3: continue
            xs=[x for (x,yy) in body if yy==y]
            if not xs: continue
            for k in range(1,right+1):
                if (max(xs)+k,y) not in outer and max(xs)+k<W: shadow[(max(xs)+k,y)]=INK
            if i==2:                              # the front ring's teeth also point left
                for k in range(1,left+1):
                    if min(xs)-k>=0 and (min(xs)-k,y) not in outer: shadow[(min(xs)-k,y)]=INK
            ins=[x for (x,yy) in inner if yy==y]
            if ins:
                for k in range(2):
                    if (min(ins)+k,y) in inner: shadow[(min(ins)+k,y)]=INK
    g.update(shadow)
    for i,b in enumerate(bodies):
        outer,_=shape(*RINGS[i])
        for (x,y) in list(g):
            if g[(x,y)]==col and (x,y) not in outer and any((x+dx,y+dy) in outer for dx,dy in ((1,0),(-1,0),(0,1),(0,-1))):
                g[(x,y)]=INK
        for p in b: g[p]=col
    DOT = ((1, 0), (2, 0), (0, 1), (1, 1), (2, 1), (3, 1), (0, 2), (1, 2), (2, 2), (3, 2), (1, 3), (2, 3))
    for i, (cx, rx, ry, t) in enumerate(RINGS):   # a round 4x4 dot above and below each ring, like the mode portals
        y0 = round(H / 2 - ry) - 4 - (0 if i == 2 else 1)
        x0 = round(cx) - 2
        for dx, dy in DOT:
            for yy in (y0 + dy, H - 1 - (y0 + dy)):
                if 0 <= x0 + dx < W and 0 <= yy < H: g[(x0 + dx, yy)] = col
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
    put(img, 0, 288, art(3))
    img.save(PNG)
    print("mirror portals drawn into", PNG.name)


if __name__ == "__main__":
    main()
