"""Famidash's background spikes as the game shows them (spike set A, frame 0) in levels/chr_data/game_art/bg_tiles.png.

The tiles of metatiles 12 / 13 (and the half spikes 19 / 20) were the ones of Famidash's Tiled tileset;
the game loads GRAPHICS/Level Tiles/SpikesA.chr. Their NES colours are ours. Two of the tiles have art
where our metatiles have the transparent tile: section SPIKES_A_EXTRA (tools/build_bg_tiles.py).

Run once: python tools/make_spikes_a.py <path to SpikesA.chr>
"""
import sys
from pathlib import Path

from PIL import Image

SHEET = Path(__file__).resolve().parent.parent / "levels" / "chr_data" / "game_art" / "bg_tiles.png"
TILES = {23: 0x0D, 24: 0x0E, 25: 0x0F, 37: 0x1C, 38: 0x1D, 39: 0x1F}    # BASE slot -> NES tile
EXTRA = [0x0C, 0x1E]
EXTRA_ROW = 35


def main(path):
    chr_data = Path(path).read_bytes()
    img = Image.open(SHEET)
    if img.height < (EXTRA_ROW + 1) * 8:
        bigger = Image.new("L", (img.width, (EXTRA_ROW + 1) * 8), 255)
        bigger.paste(img, (0, 0))
        img = bigger

    def put(tile, x0, y0):
        data = chr_data[tile * 16:tile * 16 + 16]
        for y in range(8):
            for x in range(8):
                v = ((data[y] >> (7 - x)) & 1) | (((data[y + 8] >> (7 - x)) & 1) << 1)
                # colour 1 is Famidash's vertical stripes around the spikes: plain background here
                img.putpixel((x0 + x, y0 + y), 255 - 85 * (0 if v == 1 else v))

    for slot, tile in TILES.items():
        put(tile, (slot % 16) * 8, (slot // 16) * 8)
    for i, tile in enumerate(EXTRA):
        put(tile, i * 8, EXTRA_ROW * 8)
    img.save(SHEET)


if __name__ == "__main__":
    main(sys.argv[1])
