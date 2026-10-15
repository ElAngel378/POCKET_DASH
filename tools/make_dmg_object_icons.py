"""DMG object icons: one sprite per orb and pad instead of the CGB art, and a symbol badge on the portals.

    python tools/make_dmg_object_icons.py

Pads are 2 sprites each on CGB; DMG draws them as a single 8x16 sprite with a symbol for its
type (cheaper, and readable without colours). Orbs are 16x16 like the GD orbs (2 sprites: the
right half is the left tile pair mirrored). Portals keep their art and get
a badge with the mode symbol on their side, since on DMG they only differ by colour. Pixels: '.' = 0
(transparent), '1' / '2' / '3' = light grey / dark grey / black. Pads only use the top 8 rows
(the bottom tile is empty); orbs and badges are 8x16.

Writes src/sprites/dmg_object_icons.h (tile data, included by famidash_sprite_tiles.c) and a
preview, levels/chr_data/dmg_object_icons.png.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent

E8 = ["........"] * 8

def orb(fill, symbol=None):
    """Orb (16x16, like the GD orbs: a ring, a gap, and a disc with a black edge). Left half
    only: the right half is the same tile pair mirrored (S_FLIPX). Ring in shade 2 (the GD
    orbs' glow ring), disc in shade `fill`, optional 8x8 symbol in black on the disc."""
    import math
    g = [["."] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            d = math.hypot(x - 7.5, y - 7.5)
            if 6.4 <= d < 7.7:
                g[y][x] = "2"
            elif d < 5.0:
                g[y][x] = "3" if d >= 4.0 else str(fill)
    for y, row in enumerate(symbol or []):
        for x, ch in enumerate(row):
            if ch == "#":
                g[4 + y][4 + x] = "3"
    rows = ["".join(r) for r in g]
    assert all(r == r[::-1] for r in rows), "orbs must be symmetric (right half = left mirrored)"
    return [r[:8] for r in rows]


ORB_YELLOW = orb(1)                    # jump orb: light disc
ORB_PINK = orb(2)                      # small jump orb: dark disc
ORB_BLUE = orb(1, ["        ",        # gravity orb: light disc, up/down arrow
                   "   ##   ",
                   "  ####  ",
                   "   ##   ",
                   "   ##   ",
                   "  ####  ",
                   "   ##   ",
                   "        "])
PAD_YELLOW = [   # jump pad: solid light triangle (narrow top, wide bottom) on a base
    "...33...",
    "..3113..",
    "..3113..",
    ".311113.",
    ".311113.",
    "31111113",
    "33333333",
    "........",
]
PAD_BLUE = [     # gravity pad: hourglass (wide top and bottom): a different silhouette
    "33333333",  # from the yellow triangle even with LCD ghosting
    ".311113.",
    "..3113..",
    "...33...",
    "..3113..",
    ".311113.",
    "33333333",
    "........",
]
PAD_PINK = [     # small jump pad: low dome on a base
    "........",
    "........",
    "........",
    "..3333..",
    ".311113.",
    "31111113",
    "33333333",
    "........",
]


def badge(symbol):
    """Portal badge: the half disc on the side of the original portal art (like the mode icons
    on Geometry Dash portals), placed just right of the portal's ring. Solid shade 3 with the
    6x8 symbol in shade 1, so it stands out from the portal body (shades 1 / 2) in every DMG
    palette: a black disc with a light symbol on the normal background, a white disc with a
    dark symbol on the inverted ones."""
    assert len(symbol) == 8 and all(len(s) == 6 for s in symbol), symbol
    rows = ["3333....", "333333..", "3333333.", "33333333"]
    rows += ["3" + s.replace("#", "1").replace(" ", "3") + "3" for s in symbol]
    rows += ["33333333", "3333333.", "333333..", "3333...."]
    assert len(rows) == 16 and all(len(r) == 8 for r in rows), rows
    return rows


BADGE_CUBE = badge(["      ", "######", "#    #", "# ## #", "# ## #", "#    #", "######", "      "])
BADGE_SHIP = badge(["      ", "#     ", "###   ", "######", "######", "###   ", "#     ", "      "])
BADGE_BALL = badge(["      ", " #### ", "##  ##", "# ## #", "# ## #", "##  ##", " #### ", "      "])
BADGE_GRAVITY = badge(["      ", "  ##  ", "  ##  ", "  ##  ", "######", " #### ", "  ##  ", "      "])  # down; up = flipped
BADGE_MINI = badge(["      ", "      ", "      ", "######", "######", "      ", "      ", "      "])   # minus
BADGE_GROW = badge(["      ", "  ##  ", "  ##  ", "######", "######", "  ##  ", "  ##  ", "      "])   # plus
BADGE_UFO = badge(["      ", "  ##  ", " #### ", "######", "######", " #  # ", "      ", "      "])    # saucer

# Tile pair order: must match DMG_ICON_* / DMG_BADGE_* in src/sp_draw.c. The orb and pad
# icons go to sprite tiles 180..191 (the CGB orb/pad art), the badges to 202..207, 240..245
# and 238..239 (UFO; all unused on DMG); the portals keep their own art.
PAIRS = [
    ORB_YELLOW, ORB_BLUE, ORB_PINK,
    PAD_YELLOW + E8, PAD_BLUE + E8, PAD_PINK + E8,
    BADGE_CUBE, BADGE_SHIP, BADGE_BALL, BADGE_GRAVITY, BADGE_MINI, BADGE_GROW, BADGE_UFO,
]


def tile_bytes(rows):
    out = []
    for r in rows:
        assert len(r) == 8, r
        lo = hi = 0
        for x, ch in enumerate(r):
            c = 0 if ch == "." else int(ch)
            lo |= (c & 1) << (7 - x)
            hi |= (c >> 1) << (7 - x)
        out += [lo, hi]
    return out


def main():
    data = []
    for p in PAIRS:
        data += tile_bytes(p[:8]) + tile_bytes(p[8:])
    lines = ["// Generated by tools/make_dmg_object_icons.py - do not edit",
             "#define DMG_OBJECT_ICON_TILES %d" % (len(PAIRS) * 2),
             "static const uint8_t dmg_object_icon_tiles[] = {"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    (ROOT / "src" / "sprites" / "dmg_object_icons.h").write_text("\n".join(lines) + "\n")

    shade = {".": (160, 200, 160), "1": (170, 170, 170), "2": (85, 85, 85), "3": (0, 0, 0)}
    im = Image.new("RGB", (len(PAIRS) * 10, 16), (255, 255, 255))
    for i, p in enumerate(PAIRS):
        for y, r in enumerate(p):
            for x, ch in enumerate(r):
                im.putpixel((i * 10 + x, y), shade[ch])
    im.save(ROOT / "levels" / "chr_data" / "dmg_object_icons.png")


if __name__ == "__main__":
    main()
