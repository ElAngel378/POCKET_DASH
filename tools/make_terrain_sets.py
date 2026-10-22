"""Writes the Tiled terrain sets (wang sets) of the object catalog into levels/objects/objects.tsx.

A block style is a family of objects whose white outline (colour index 3) shows on the sides that
touch air: in Tiled, pick the style in the Terrain Sets panel and paint, and it places the right
edge / corner piece for every cell. Each tile's terrain is read from its art: an outlined side is
air there, a lone outline pixel in a corner (a "dot") is air at that corner only.

Run it again after adding a block style (add it to STYLES) or changing their art:
    python tools/make_terrain_sets.py
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from objects_catalog import TSX, load_catalog  # noqa: E402

# (terrain set name, colour shown in Tiled, object names matching it)
STYLES = [
    ("Checkerboard blocks", "#4a90e2", r"CHECKERBOARD(_\d+)?"),
    ("Checkerboard blocks (set B)", "#50e3c2", r"CHECKERBOARD(_\d+)?_BB"),
    ("X blocks", "#e24a4a", r"X_\w+"),
    ("Black blocks", "#9b9b9b", r"BLACK_\w+"),
]
OUTLINE = 3
# Pieces whose art does not show their terrain: CHECKERBOARD_11 is the bottom right dot piece of
# its set (like CHECKERBOARD_11_BB), but Famidash drew it without the dot
TERRAIN_OVERRIDES = {"CHECKERBOARD_11": (1, 1, 1, 0, 1, 1, 1, 1)}


def terrain(art):
    """wangid (top, top right, right, bottom right, bottom, bottom left, left, top left) of a block
    piece: 1 = block on that side / corner, 0 = air"""
    px = [[art[(y >> 3) * 2 + (x >> 3)][y & 7][x & 7] for x in range(16)] for y in range(16)]
    side = lambda cells: sum(px[y][x] == OUTLINE for y, x in cells) >= 12
    t = not side([(0, x) for x in range(16)])
    b = not side([(15, x) for x in range(16)])
    l = not side([(y, 0) for y in range(16)])
    r = not side([(y, 15) for y in range(16)])
    corner = lambda y, x, a, c: int(a and c and px[y][x] != OUTLINE)
    return (int(t), corner(0, 15, t, r), int(r), corner(15, 15, b, r),
            int(b), corner(15, 0, b, l), int(l), corner(0, 0, t, l))


def main():
    cat = load_catalog()
    out = [" <wangsets>"]
    for name, colour, pattern in STYLES:
        tiles = sorted(i for i, o in cat.objects.items() if re.fullmatch(pattern, o["name"]))
        ids = {i: TERRAIN_OVERRIDES.get(cat.objects[i]["name"]) or terrain(cat.art(i)) for i in tiles}
        full = next((i for i in tiles if ids[i] == (1,) * 8), tiles[0])
        out.append('  <wangset name="%s" type="mixed" tile="%d">' % (name, full))
        out.append('   <wangcolor name="%s" color="%s" tile="%d" probability="1"/>' % (name, colour, full))
        for i in tiles:
            out.append('   <wangtile tileid="%d" wangid="%s"/>' % (i, ",".join(map(str, ids[i]))))
        out.append("  </wangset>")
        print("%-28s %2d pieces, %2d kinds" % (name, len(tiles), len(set(ids.values()))))
    out.append(" </wangsets>")
    tsx = TSX.read_text(encoding="utf-8")
    tsx = re.sub(r" <wangsets>.*</wangsets>\n", "", tsx, flags=re.S)
    tsx = tsx.replace("</tileset>", "\n".join(out) + "\n</tileset>")
    TSX.write_text(tsx, encoding="utf-8")


if __name__ == "__main__":
    main()
