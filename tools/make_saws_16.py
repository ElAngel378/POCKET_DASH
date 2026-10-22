"""One-time setup of the 1.6 sawblades (run 2026-10-22): a second saw set, drawn instead of the
Famidash saws in levels that place the 1.6 saw objects (CGB; DMG keeps the Famidash saws).

The art is levels/objects/source/1p6blocks.chr (Game Boy 2bpp, 16x16 pieces as 2x2 tiles): the
big saw (3x3 pieces at row 2, column 1), the medium saw (2x2 at row 2, column 4) and the small saw
(row 3, column 0), one frame each. Frames 1 and 2 are that frame turned by a third of the gap
between two teeth (the saws spin like the Famidash ones: 3 frames). They are cut into the
Famidash saws' 52 tile layout (tools/build_bg_tiles.py SAW_METATILES) and written to
levels/chr_data/bg_tiles.png sections SAWS16_0..2 (redraw frames 1 and 2 there by hand to
improve them). The 1.6 saw objects are added to the catalog: one per Famidash saw part, with its
famidash_id (the engine knows saw parts by id), so a level has either kind of saw.

    python tools/make_saws_16.py
"""
import math
import sys
from collections import Counter
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_bg_tiles as bgt  # noqa: E402
from import_gda_16 import append_objects, pieces  # noqa: E402
from objects_catalog import load_catalog  # noqa: E402

SOURCE = ROOT / "levels" / "objects" / "source" / "1p6blocks.chr"
# saw: (pieces (row, column) of its top left, size in pieces, teeth); part ids per piece, row by row
SAWS = {
    "big": ((2, 1), 3, 12, [116, 117, 118, 119, 120, 121, 122, 123, 124]),
    "med": ((2, 4), 2, 8, [8, 9, 10, 11]),
    "small": ((3, 0), 1, 4, [125]),
}
# small saws offset by half a piece: SMALL_SAW_TOP_HALF (127) shows the small saw's top half in its
# bottom half, SMALL_SAW_BOTTOM_HALF (4) its bottom half in its top half
HALF_SAWS = (127, 4)
SAWS16_ROW = 35                        # bg_tiles.png row of section SAWS16_0 (4 rows per frame)


def rotate(a, deg, k=8):
    """a (rows of colour indices) turned by deg around its centre: each pixel takes the colour that
    covers the most of it (k x k samples), a colour wins over the background from 30%"""
    h, w = len(a), len(a[0])
    cx, cy = w / 2, h / 2
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            votes = Counter()
            for sy in range(k):
                for sx in range(k):
                    px, py = x + (sx + .5) / k - cx, y + (sy + .5) / k - cy
                    ux, uy = int(math.floor(c * px + s * py + cx)), int(math.floor(-s * px + c * py + cy))
                    votes[a[uy][ux] if 0 <= ux < w and 0 <= uy < h else 0] += 1
            best = max((v for v in votes if v), key=lambda v: votes[v], default=0)
            out[y][x] = best if best and votes[best] >= 0.3 * k * k else 0
    return out


def main():
    src = pieces(SOURCE)
    frames = {}
    for name, ((r0, c0), n, teeth, _) in SAWS.items():
        a = [[0] * (16 * n) for _ in range(16 * n)]
        for r in range(n):
            for c in range(n):
                for y in range(16):
                    a[r * 16 + y][c * 16:c * 16 + 16] = src["%d.%d" % (r0 + r, c0 + c)][y]
        frames[name] = [a] + [rotate(a, 360 / teeth / 3 * f) for f in (1, 2)]

    def quarter(img, part_index, n, q):
        """8x8 tile q of piece part_index (row by row) of an n x n piece saw"""
        ox, oy = (part_index % n) * 16 + (q & 1) * 8, (part_index // n) * 16 + (q >> 1) * 8
        return [row[ox:ox + 8] for row in img[oy:oy + 8]]

    # the 52 saw tiles of each frame, at the Famidash layout's places
    tiles = [[None] * bgt.SAW_TILES for _ in range(3)]
    for name, (_, n, _, parts) in SAWS.items():
        for i, m in enumerate(parts):
            for q, t in enumerate(bgt.SAW_METATILES[m]):
                if t is not None:
                    for f in range(3):
                        tiles[f][t] = quarter(frames[name][f], i, n, q)
    assert all(t is not None for f in tiles for t in f), "saw tiles left empty"
    png = Image.open(bgt.PNG).convert("L")
    rows = SAWS16_ROW + 12
    out = Image.new("L", (128, max(png.height, rows * 8)), 255)
    out.paste(png, (0, 0))
    px = out.load()
    for f in range(3):
        for t, rows8 in enumerate(tiles[f]):
            ox, oy = (t % 16) * 8, (SAWS16_ROW + 4 * f + t // 16) * 8
            for y in range(8):
                for x in range(8):
                    px[ox + x, oy + y] = 255 - 85 * rows8[y][x]
    out.save(bgt.PNG)

    # the objects: frame 0 art, the Famidash part's collision, DMG art (DMG draws Famidash saws)
    cat = load_catalog()
    if any(o["name"].startswith("1.6_SAW") for o in cat.objects.values()):
        raise SystemExit("1.6 saw objects already there")
    famidash = {o["famidash_id"]: o for o in cat.objects.values() if "SAW" in o["name"] and "DIAMOND" not in o["name"]}
    new = []
    for name, (_, n, _, parts) in SAWS.items():
        for i, m in enumerate(parts):
            a = [row[(i % n) * 16:(i % n) * 16 + 16] for row in frames[name][0][(i // n) * 16:(i // n) * 16 + 16]]
            fd = famidash[m]
            new.append(dict(name="1.6_SAW_" + fd["name"].replace("_SAW", ""), collision=fd["collision"],
                            famidash_id=m, dmg_art=fd["dmg_art"], art=a, src="1.6 %s saw" % name))
    small = frames["small"][0]
    for m in HALF_SAWS:
        fd = famidash[m]
        a = ([[0] * 16] * 8 + small[0:8]) if m == 127 else (small[8:16] + [[0] * 16] * 8)
        new.append(dict(name="1.6_SAW_" + fd["name"].replace("_SAW", ""), collision=fd["collision"],
                        famidash_id=m, dmg_art=fd["dmg_art"], art=a, src="1.6 small saw half"))
    append_objects(cat, new)


if __name__ == "__main__":
    main()
