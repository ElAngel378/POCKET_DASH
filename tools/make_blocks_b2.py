"""Draws the rest of Famidash block set B into levels/chr_data/bg_tiles.png rows 31..34
(BLOCKS_B2_CGB / BLOCKS_B2_DMG in tools/build_bg_tiles.py; then run that).

    python tools/make_blocks_b2.py [--famidash PATH]

BLOCKS_B_CGB / _DMG (rows 17, 19) only hold the block set B tiles Clutterfunk uses. Theory of
Everything also uses the plain blocks (NES $40..$49, $50..$59), which are bricks in set B and a
grid in our sheet (set A). Colours as in rows 17 / 19: CGB = the NES colour, DMG = colours 2 and 3
swapped (black fill, white outline).

    row 31 (CGB) / 33 (DMG) tiles 0..19 <- sheet slots BLOCKS_B2_SLOTS
"""
from pathlib import Path
import argparse
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "bg_tiles.png"
# sheet slot -> NES tile of the block CHR bank ($40..$7F)
SLOTS = [(72 + i, 0x40 + i) for i in range(10)] + [(88 + i, 0x50 + i) for i in range(10)]
ROW_CGB, ROW_DMG = 31, 33
DMG_MAP = {0: 0, 1: 1, 2: 3, 3: 2}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--famidash", default=r"C:\Users\soter\Source\Repos\famidash-main")
    a = ap.parse_args()
    chr_b = (Path(a.famidash) / "GRAPHICS" / "Level Tiles" / "BlocksB.chr").read_bytes()
    img = Image.open(PNG).convert("L")
    rows = ROW_DMG + 2
    if img.size[1] < rows * 8:
        big = Image.new("L", (128, rows * 8), 255)
        big.paste(img, (0, 0))
        img = big
    px = img.load()
    grey = lambda c: 255 - 85 * c
    for k, (slot, nes) in enumerate(SLOTS):
        t = chr_b[(nes - 0x40) * 16: (nes - 0x40) * 16 + 16]
        for y in range(8):
            for x in range(8):
                c = ((t[y] >> (7 - x)) & 1) | (((t[y + 8] >> (7 - x)) & 1) << 1)
                for row, cm in ((ROW_CGB, c), (ROW_DMG, DMG_MAP[c])):
                    px[(k % 16) * 8 + x, (row + k // 16) * 8 + y] = grey(cm)
    img.save(PNG)


if __name__ == "__main__":
    main()
