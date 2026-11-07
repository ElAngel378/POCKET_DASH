"""The art of the objects with art of their own (docs/PLAN_1.6.md, phase 3): your .chr drawings
(USER_ART), Clutterfunk's thin lines (LINES), and GDA's art for what nobody has drawn yet.

From GDA only the objects in ART below, nothing else (decision 4 of the plan). Each GDA sprite is
cut to the GD object's shape (GD's big cogwheel is a quarter wheel, its big gear ring half a ring),
turned into GB colours and saved as levels/chr_data/objects/art/<key>.png: a palette PNG, colours
0..3, 0 = transparent. The game and Tiled take the objects' art from there (gd_objects.DRAWN).
Replace a PNG with your own drawing any time; running this tool again would undo that.

    python tools/import_gda_art.py      (needs ../geometry_dash_advance)
"""
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda
import gd_objects as GO

ART_DIR = ROOT / "levels" / "chr_data" / "objects" / "art"

# key -> (GDA sprite type, crop box in the GDA image (x0, y0, x1, y1), None = all)
ART = {
    "gd85": ("BIG_SAW_DECO", (8, 8, 32, 32)),                    # top left quarter of the wheel
    "gd86": ("MEDIUM_SAW_DECO", None),
    "gd87": ("SMALL_SAW_DECO", (4, 4, 28, 28)),
    "gd97": ("TINY_SAW_DECO", None),
    "gd106": ("BIG_WIDE_BG_CHAINS", (0, 8, 16, 48)),
    "gd107": ("SMALL_WIDE_BG_CHAINS", (0, 8, 16, 32)),
    "gd110": ("SMALL_BG_CHAINS", (0, 8, 16, 32)),
    "gd123": ("BUSH_DECO_5", None),
    "gd124": ("BUSH_DECO_4", None),
    "gd125": ("BUSH_DECO_3", None),
    "gd126": ("BUSH_DECO_2", None),
    "gd127": ("BUSH_DECO_1", None),
    "gd128": ("BUSH_DECO_6", (0, 8, 32, 32)),
    "gd129": ("BIG_CLOUD", None),
    "gd130": ("MEDIUM_CLOUD", None),
    "gd131": ("SMALL_CLOUD", (4, 0, 28, 16)),
    "gd137": ("BIG_CIRCUNFERENCE_SAW_DECO", (8, 8, 32, 56)),     # left half of the ring
    "gd138": ("MED_CIRCUNFERENCE_SAW_DECO", None),
    "gd139": ("SMA_CIRCUNFERENCE_SAW_DECO", None),
    "gd151": ("BIG_DECO_TRIANGLE", (4, 8, 12, 32)),
    "gd152": ("MEDIUM_DECO_TRIANGLE", (4, 0, 12, 16)),
    "gd153": ("SMALL_DECO_TRIANGLE", (4, 0, 12, 16)),
    "gd154": ("BIG_SPIKE_SAW_DECO", (8, 8, 32, 32)),             # top left quarter
    "gd155": ("MEDIUM_SPIKE_SAW_DECO", (0, 0, 32, 24)),
    "gd180": ("BIG_CIRCLE_SAW_DECO", (0, 0, 16, 16)),            # top left quarter
    "gd181": ("MEDIUM_CIRCLE_SAW_DECO", (4, 4, 28, 28)),
    "gd182": ("SMALL_CIRCLE_SAW_DECO", None),
    "gd190": ("DIAMOND_BG_CHAINS", (4, 0, 12, 32)),
}

# key -> (your .chr file in levels/objects/incoming_art, NES format?, box in its 128 px sheet)
USER_ART = {
    "gd48": ("Wavy_Gspikes", True, (24, 0, 72, 16)),
    "gd49": ("Wavy_Gspikes", True, (24, 16, 72, 32)),
    "gd113": ("GDP_TOE", False, (0, 24, 64, 40)),
    "gd114": ("GDP_TOE", False, (72, 8, 120, 24)),
    "gd115": ("GDP_TOE", False, (72, 32, 112, 48)),
    "gd143": ("1_6_objects", False, (0, 96, 16, 112)),
    "gd157": ("1p6blocks", False, (0, 32, 16, 48)),
    "gd158": ("1p6blocks", False, (0, 32, 16, 48), "H"),
    "gd159": ("1p6blocks", False, (0, 32, 16, 48)),
    "gd170": ("1p6blocks", False, (0, 0, 16, 16)),
    "gd171": ("1p6blocks", False, (16, 0, 32, 16)),
    "gd173": ("1p6blocks", False, (80, 0, 96, 16)),
    "gd174": ("1p6blocks", False, (96, 0, 112, 16)),
    "gd175": ("1p6blocks", False, (48, 0, 64, 16)),
    "gd176": ("1p6blocks", False, (112, 0, 128, 16)),
    "gd186": ("1p6blocks", False, (16, 32, 40, 56)),     # top left quarter of the saw
    "gd187": ("1p6blocks", False, (64, 32, 96, 64)),
    "gd188": ("1p6blocks", False, (96, 32, 128, 64)),
    "gd192": ("1p6blocks", False, (32, 0, 48, 16)),
    "gd194": ("1p6blocks", False, (96, 0, 112, 16)),
    "gd197": ("1p6blocks", False, (64, 0, 80, 16)),
}
# Clutterfunk's thin lines: a line along the block's edge, nothing to draw (x0, y0, x1, y1)
LINES = {"gd76": (3, 0, 13, 1), "gd77": (3, 0, 13, 1), "gd78": (3, 0, 13, 1), "gd81": (3, 0, 13, 1),
         "gd82": (0, 3, 1, 13)}
# the 1.6 outlines: lines in colour 3 (blocks' outline colour) along a block's edges, to stack on
# fills and blocks
EDGE_T, EDGE_L, EDGE_R, EDGE_B = (0, 0, 16, 1), (0, 0, 1, 16), (15, 0, 16, 16), (0, 15, 16, 16)
OUTLINES = {"gd161": [EDGE_T], "gd162": [EDGE_T, EDGE_L], "gd163": [(0, 0, 1, 1)],
            "gd165": [EDGE_T, EDGE_L, EDGE_R], "gd166": [EDGE_L], "gd167": [(0, 0, 2, 1)],
            "gd169": [EDGE_T, EDGE_L, EDGE_R, EDGE_B]}

# hazards from GDA: spikes keep their shades, black saws are black with a light edge
SPIKES = {"gd177": ("ONE_POINT_SIX_SPIKE_V", None), "gd178": ("ONE_POINT_SIX_SPIKE_V", "half height"),
          "gd179": ("ONE_POINT_SIX_MEDIUM_SPIKE_V", None)}
BLACK_SAWS = {"gd183": ("ALT_BIG_OUTLINE_SAW", (8, 8, 32, 32)),      # top left quarter
              "gd184": ("ALT_MEDIUM_OUTLINE_SAW", None), "gd185": ("ALT_SMALL_OUTLINE_SAW", (12, 4, 20, 28))}

GREYS = [255, 255, 255, 170, 170, 170, 85, 85, 85, 0, 0, 0]


def gb_colours(img, dither=False):
    """GDA decoration -> GB: all of it in colour 3 (the decoration palette's player colour).
    dither (clouds): GDA's other shades become a checkerboard, their soft lower edge"""
    used = sorted(set(img.getdata()) - {0})
    top = used[-1] if used else 0
    out = img.point([0 if not c else 3 for c in range(256)])
    if dither:
        src, px = img.load(), out.load()
        for y in range(img.height):
            for x in range(img.width):
                if src[x, y] and src[x, y] != top and (x + y) & 1:
                    px[x, y] = 0
    return out


def chr_sheet(name, nes):
    """A .chr file as a 128 px wide image of colours 0..3"""
    b = (ROOT / "levels" / "objects" / "incoming_art" / ("%s.chr" % name)).read_bytes()
    img = Image.new("L", (128, 128))
    for t in range(256):
        d = b[t * 16:t * 16 + 16]
        for y in range(8):
            lo, hi = (d[y], d[y + 8]) if nes else (d[2 * y], d[2 * y + 1])
            for x in range(8):
                img.putpixel((t % 16 * 8 + x, t // 16 * 8 + y), (lo >> (7 - x) & 1) | (hi >> (7 - x) & 1) << 1)
    return img


def save(key, img):
    if img.size != GO.OBJECTS[key][2]:
        raise SystemExit("%s: art %s, object %s" % (key, img.size, GO.OBJECTS[key][2]))
    out = Image.new("P", img.size)
    out.putdata(list(img.getdata()))
    out.putpalette(GREYS)
    out.save(ART_DIR / ("%s.png" % key), transparency=0)


def main():
    ART_DIR.mkdir(parents=True, exist_ok=True)
    for key, (name, nes, box, *flip) in USER_ART.items():
        img = chr_sheet(name, nes).crop(box)
        save(key, img.transpose(Image.FLIP_LEFT_RIGHT) if flip else img)
    for key, box in LINES.items():
        img = Image.new("L", GO.OBJECTS[key][2])
        img.paste(3, box)
        save(key, img)
    for key, boxes in OUTLINES.items():
        img = Image.new("L", GO.OBJECTS[key][2])
        for box in boxes:
            img.paste(3, box)
        save(key, img)
    save("gd193", Image.new("L", (16, 16), 2))
    img = Image.new("L", (16, 16), 2)       # the striped fill: light on top, black below
    img.paste(1, (0, 0, 16, 6))
    save("gd164", img)
    names = gda.sprite_types()
    for key, (sprite, box) in ART.items():
        if key not in GO.DRAWN:
            raise SystemExit("%s is not in tools/gd_objects.py DRAWN" % key)
        img = gda.sprite_art(names.index(sprite))[0]
        if box:
            img = img.crop(box)
        save(key, gb_colours(img, key in ("gd129", "gd130", "gd131")))
    for key, (sprite, box) in SPIKES.items():
        img = gda.sprite_art(names.index(sprite))[0]
        print(key, sprite, img.size, img.getbbox())
        if box:     # GD's low wide spike: GDA's has none, the spike squashed
            img = img.resize((16, 8), Image.NEAREST)
        save(key, img.point([0 if not c else 3 if c >= 11 else 2 if c >= 5 else 1 for c in range(256)]))
    for key, (sprite, box) in BLACK_SAWS.items():
        img = gda.sprite_art(names.index(sprite))[0]
        img = img.crop(box) if box else img
        top = max(img.getdata())
        save(key, img.point([0 if not c else 2 if c == top else 3 for c in range(256)]))


if __name__ == "__main__":
    main()
