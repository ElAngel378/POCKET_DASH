"""Draws Famidash spike set C's pit spikes into levels/chr_data/bg_tiles.png row 30 (SPIKES_C in
tools/build_bg_tiles.py; then run that).

    python tools/make_spikes_c.py [--famidash PATH]

Spike set C (Theory of Everything) differs from set A only in the sideways pit spikes
(PITSPIKES_LEFT / _RIGHT, and the diagonal spikes, which use the same NES tiles): the same
spikes with a dotted colour 1 pattern in their empty part. Our sheet's pit spike tiles are a
cleaned up set A, so set C is our tile plus Famidash's colour 1 dots where our tile is empty.

    row 30 tiles 0..3 <- sheet slots 19, 20, 33, 34 (NES $08, $09, $18, $19 of SpikesC.chr)
"""
from pathlib import Path
import argparse
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "bg_tiles.png"
SLOTS = [(19, 0x08), (20, 0x09), (33, 0x18), (34, 0x19)]
ROW = 30


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--famidash", default=r"C:\Users\soter\Source\Repos\famidash-main")
    a = ap.parse_args()
    chr_c = (Path(a.famidash) / "GRAPHICS" / "Level Tiles" / "SpikesC.chr").read_bytes()
    img = Image.open(PNG).convert("L")
    if img.size[1] < (ROW + 1) * 8:
        big = Image.new("L", (128, (ROW + 1) * 8), 255)
        big.paste(img, (0, 0))
        img = big
    px = img.load()
    idx = lambda v: min(range(4), key=lambda i: abs(v - (255 - 85 * i)))
    for k, (slot, nes) in enumerate(SLOTS):
        t = chr_c[nes * 16: nes * 16 + 16]
        for y in range(8):
            for x in range(8):
                ours = idx(px[(slot % 16) * 8 + x, (slot // 16) * 8 + y])
                c = ((t[y] >> (7 - x)) & 1) | (((t[y + 8] >> (7 - x)) & 1) << 1)
                v = 1 if (ours == 0 and c == 1) else ours
                px[k * 8 + x, ROW * 8 + y] = 255 - 85 * v
    img.save(PNG)
    print("spike set C tiles drawn into", PNG.name, "row", ROW)


if __name__ == "__main__":
    main()
