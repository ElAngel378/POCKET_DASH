"""Draws GDA's 3 pulsing decorations (PULSING_CIRCUNFERENCE, PULSING_HEARTH, PULSING_SQUARE: sprite
objects 39, 40, 41) into levels/chr_data/sprite_tiles.png, CGB VRAM bank 1 tiles 82..99 (pairs 41..49
of the BANK1 section), from GDA's art (tools/gda.py, ../geometry_dash_advance), as a placeholder.

Each is 16x16 and mirror symmetric: one 8x16 tile pair, the left half (the right one is drawn
flipped). 3 sizes, like the pulsing rods (src/sp_draw.c deco_tile_sets): 16, 12 and 8 px wide.
Pair 41 + 3 * size + object: size 0 full, 1 at 12 px, 2 at 8 px. Colour 2 (the cube's colour 1 in
the player's palette, as Famidash draws decorations).

    python tools/make_gda_pulse_tiles.py
"""
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda  # noqa: E402

PNG = ROOT / "levels" / "chr_data" / "sprite_tiles.png"
OBJECTS = ("PULSING_CIRCUNFERENCE", "PULSING_HEARTH", "PULSING_SQUARE")
SIZES = (16, 12, 8)
BANK1_ROW = 8      # tools/build_sprite_tiles.py SECTION_ROW["BANK1"]
FIRST_PAIR = 41


def main():
    types = gda.sprite_types()
    img = Image.open(PNG).convert("L")
    for o, name in enumerate(OBJECTS):
        art, ox, oy = gda.sprite_art(types.index(name))
        mask = art.point(lambda c: 255 if c else 0).crop((ox, oy, ox + 16, oy + 16))
        for s, size in enumerate(SIZES):
            m = mask.resize((size, size), Image.NEAREST) if size != 16 else mask
            full = Image.new("L", (16, 16), 0)
            full.paste(m, ((16 - size) // 2, (16 - size) // 2))
            pair = FIRST_PAIR + 3 * s + o
            x0, y0 = (pair % 16) * 8, (BANK1_ROW + pair // 16) * 16
            for y in range(16):
                for x in range(8):
                    img.putpixel((x0 + x, y0 + y), 255 - 85 * 2 if full.getpixel((x, y)) else 255)
    img.save(PNG)
    print("pairs %d..%d" % (FIRST_PAIR, FIRST_PAIR + 3 * len(SIZES) - 1))


if __name__ == "__main__":
    main()
