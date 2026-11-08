"""What the sky and the ground look like along each level, for Tiled: one long image of the parallax
and one of the ground per level (levels/chr_data/previews), in the game's art and shades, coloured
by the level's start colours and colour triggers.

    python tools/level_previews.py             every level (tools/build_levels.py runs this)
    python tools/level_previews.py stereomadness

Each TMX's two image layers point at them. Reload the level in Tiled after a build to see changed
triggers. The sky scrolls 1:1 with the map here, so a colour change is where its trigger is; a
fade's length assumes normal speed.
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_famidash_bg_tables as BG
import tmx2sprites as T
from gd_objects import ROWS_BELOW

ROOT = Path(__file__).resolve().parent.parent
GFX = ROOT / "levels" / "chr_data"
TMX_DIRS = [GFX / "tmx", GFX / "examples"]
OUT = GFX / "previews"
SECONDS = {"0.3 s": 0.3, "instant": 0, "0.5 s": 0.5, "1 s": 1, "2 s": 2, "4 s": 4, "10 s": 10}
PX_PER_SECOND = 166     # normal speed: where a fade ends on the map is approximate
PARALLAX_H = 48
GROUND_H = 16
_WHITE = (255, 255, 255)


def scaled(colour, f):
    return tuple(int(v * f) for v in colour)


def sky_shades(i):
    """colours 0..3 of the parallax palette (gen_famidash_bg_tables.py sky)"""
    c = BG.rgb8(i)
    return (c, scaled(c, 0.52), scaled(c, 0.68), scaled(c, 0.40))


def ground_shades(i):
    """colours 0..3 of the ground grid palette (gen_famidash_bg_tables.py ground)"""
    c = BG.rgb8(i)
    dark, grid = BG.rgb8(BG.one_shade_darker(i)), scaled(c, 0.16)
    if i < 0x10:
        dark, grid = scaled(c, 0.62), scaled(c, 0.36)
    return (_WHITE, c, dark, grid)


def master(path):
    """A pattern as colour indices (captured from the game: 0, 85, 170, 255 = colours 0..3)"""
    grey = Image.open(path).convert("L")
    out = Image.new("P", grey.size)
    out.putdata([v // 85 for v in grey.getdata()])
    return out


def tiled_strip(pattern, w, h, roll=0):
    """The pattern repeated; roll: rows of it above the top of the strip"""
    out = Image.new("P", (w, h))
    for x in range(0, w, pattern.width):
        for y in range(-roll, h, pattern.height):
            out.paste(pattern, (x, y))
    return out


def track(width, start, triggers, shades):
    """[(from x, to x, 4 RGB colours)] along the level"""
    spans, x, cur = [], 0, shades(start)
    for tx, index, seconds in sorted(triggers) + [(width, None, 0)]:
        tx = max(x, min(tx, width))
        if tx > x:
            spans.append((x, tx, cur))
        x = tx
        if index is None:
            break
        new = shades(index)
        fade = int(seconds * PX_PER_SECOND)
        for i in range(min(fade, width - x)):
            f = i / fade
            spans.append((x + i, x + i + 1, tuple(tuple(int(a[k] + (b[k] - a[k]) * f) for k in range(3)) for a, b in zip(cur, new))))
        x = min(x + fade, width)
        cur = new
    return spans


def paint(master, spans):
    out = Image.new("RGB", master.size)
    for x0, x1, colours in spans:
        piece = master.crop((x0, 0, x1, master.height))
        piece.putpalette([v for c in colours for v in c] + [0] * (768 - 12))
        out.paste(piece.convert("RGB"), (x0, 0))
    return out


def preview(path, parallax, ground):
    name = path.stem
    root = ET.parse(path).getroot()
    w, h = int(root.get("width")) * 16, (int(root.get("height")) - ROWS_BELOW) * 16
    props = {p.get("name"): p.get("value") for p in root.findall("properties/property")}
    first = next(int(t.get("firstgid")) for t in root.findall("tileset") if t.get("source", "").endswith("sprites.tsx"))

    def index(value):
        return T.TRIGGER_COLOURS[T.nearest_colour(value, T.TRIGGER_COLOURS, lambda n: T.NES[n])]

    triggers = {T.BG_TRIGGER: [], T.GROUND_TRIGGER: []}
    for group in root.findall("objectgroup"):
        if group.get("name") == "SP":
            for o in group.findall("object"):
                tile = int(o.get("gid", first)) - first
                if tile in triggers:
                    p = {q.get("name"): q.get("value") for q in o.findall("properties/property")}
                    triggers[tile].append((int(float(o.get("x"))), index(p.get("Colour", "#fffffeff")), SECONDS.get(p.get("Fade", "0.3 s"), 0.3)))
    default = "#ff%06x" % T.NES[17]
    OUT.mkdir(exist_ok=True)
    paint(tiled_strip(parallax, w, h, (116 - h) % PARALLAX_H), track(w, index(props.get("BG colour", default)), triggers[T.BG_TRIGGER], sky_shades)).save(OUT / (name + "_bg.png"))
    paint(tiled_strip(ground, w, GROUND_H), track(w, index(props.get("Ground colour", default)), triggers[T.GROUND_TRIGGER], ground_shades)).save(OUT / (name + "_ground.png"))
    point_layers(path, name, h)


def point_layers(path, name, height):
    """The TMX's first image layer shows the sky, the second the ground"""
    with path.open(newline="") as f:
        text = f.read()
    layers = list(re.finditer(r'(<imagelayer id="(\d+)")[^>]*>(\s*)<image [^>]*/>', text))
    if len(layers) < 2:
        return
    sky = '<imagelayer id="%s" name="BG preview">%s<image source="../previews/%s_bg.png"/>' % (layers[0].group(2), layers[0].group(3), name)
    floor = '<imagelayer id="%s" name="Ground preview" offsetx="0" offsety="%d">%s<image source="../previews/%s_ground.png"/>' % (layers[1].group(2), height, layers[1].group(3), name)
    new = text[:layers[0].start()] + sky + text[layers[0].end():layers[1].start()] + floor + text[layers[1].end():]
    if new != text:
        with path.open("w", newline="") as f:
            f.write(new)


def main(names=None):
    parallax, ground = master(GFX / "parallax_preview.png"), master(GFX / "ground_preview.png")
    for folder in TMX_DIRS:
        for path in sorted(folder.glob("*.tmx")):
            if not names or path.stem in names:
                preview(path, parallax, ground)


if __name__ == "__main__":
    main(sys.argv[1:])
