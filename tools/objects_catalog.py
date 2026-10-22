"""Reads the object catalog: levels/objects/objects.tsx (a Tiled tileset), objects.png (the art)
and objects_dmg.png (DMG-only art).

    from objects_catalog import load_catalog
    cat = load_catalog()
    cat.objects[i]  -> {"name", "collision", "palette", "famidash_id", "dmg_art"} (tile id i)
    cat.by_name[name] -> tile id
    cat.art(i)      -> 4 quarters (TL, TR, BL, BR) of 8x8 colour indices (0 white .. 3 black)
    cat.dmg_art(i)  -> the same for its DMG art (tile dmg_art of objects_dmg.png, or its own art)

Tiled shows objects_preview.png, made from objects.png by write_preview() (CGB colours, markers).
See levels/objects/README.md for what the properties mean.
"""
import re
import xml.etree.ElementTree as ET
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CATALOG_DIR = ROOT / "levels" / "objects"
TSX = CATALOG_DIR / "objects.tsx"
ART = CATALOG_DIR / "objects.png"
DMG_ART = CATALOG_DIR / "objects_dmg.png"
PREVIEW = CATALOG_DIR / "objects_preview.png"
COLUMNS = 16
# The sprite tileset comes after the catalog in a level's TMX: room for 4096 objects
SPRITES_FIRSTGID = 4097


def _index(v):
    """grey level -> colour index (white 0 .. black 3)"""
    return min(range(4), key=lambda i: abs(v - (255 - 85 * i)))


def _art(px, tid):
    ox, oy = (tid % COLUMNS) * 16, (tid // COLUMNS) * 16
    return tuple(tuple(tuple(_index(px[ox + (q & 1) * 8 + x, oy + (q >> 1) * 8 + y])
                             for x in range(8)) for y in range(8)) for q in range(4))


class Catalog:
    def __init__(self, tsx=TSX):
        root = ET.parse(tsx).getroot()
        self._px = Image.open(ART).convert("L").load()
        self._dmg_px = Image.open(DMG_ART).convert("L").load()
        self.objects = {}
        for tile in root.findall("tile"):
            props = {}
            for p in tile.find("properties").findall("property"):
                v = p.get("value")
                t = p.get("type", "string")
                props[p.get("name")] = int(v) if t == "int" else (v == "true") if t == "bool" else v
            self.objects[int(tile.get("id"))] = props
        self.by_name = {o["name"]: i for i, o in self.objects.items()}
        assert len(self.by_name) == len(self.objects), "object names must be unique"

    def art(self, tid):
        return _art(self._px, tid)

    def dmg_art(self, tid):
        d = self.objects[tid].get("dmg_art", -1)
        return _art(self._dmg_px, d) if d >= 0 else self.art(tid)


def load_catalog():
    return Catalog()


# Level ids the engine treats specially (src/player.c, src/graphics/mt_renderer.c,
# tools/build_bg_tiles.py SAW_METATILES): only the object with that famidash_id may have them.
ENGINE_IDS = {0, 1, 2, 5, 6, 136, 137, 12, 24, 4, 8, 9, 10, 11, 120, 125, 127} | set(range(0x74, 0x80))


def tmx_level_objects(tmx_path):
    """Catalog tile ids the map layer of a level's TMX uses (None: the TMX is not converted)"""
    root = ET.parse(tmx_path).getroot()
    if not any("objects.tsx" in (t.get("source") or "") for t in root.findall("tileset")):
        return None
    layer = next(l for l in root.findall("layer") if l.get("name") != "SP")
    tids = set()
    for v in layer.find("data").text.split(","):
        gid = int(v) & 0x0FFFFFFF if v.strip() else 0
        if gid:
            tids.add(gid - 1)
    return tids


def assign_level_ids(cat, tids):
    """Level id (the map byte, 0..255) of each catalog object a level uses: its famidash_id when it
    has one and no other object of the level has it, else the lowest id free in the level (never an
    ENGINE_ID). tools/build_levels.py (map bytes) and tools/build_bg_tiles.py (tables) both use it."""
    ids, taken = {}, set()
    bad = sorted(t for t in tids if t not in cat.objects)
    if bad:
        raise SystemExit("objects.tsx tiles %s are placed in a level but have no properties (name, "
                         "collision, palette, famidash_id: levels/objects/README.md)" % bad)
    for tid in sorted(tids):
        fid = cat.objects[tid].get("famidash_id", -1)
        if fid >= 0 and fid not in taken:
            ids[tid] = fid
            taken.add(fid)
    free = (i for i in range(1, 256) if i not in taken and i not in ENGINE_IDS)
    for tid in sorted(tids):
        if tid not in ids:
            fid = cat.objects[tid].get("famidash_id", -1)
            if fid in ENGINE_IDS and fid >= 0:
                raise SystemExit("two objects of a level have engine id %d (%s)" % (fid, cat.objects[tid]["name"]))
            i = next(free, None)
            if i is None:
                raise SystemExit("a level uses more than 256 kinds of object")
            ids[tid] = i
    return ids


# Preview colours of the 4 colour indices per CGB palette (src/famidash_bg.c with a blue sky).
# Index 0 is the sky: transparent, so Tiled's background colour shows through.
DARK_SKY, SKY, BLACK, WHITE = (0, 33, 115), (8, 74, 197), (0, 0, 0), (255, 255, 255)
PREVIEW_PALETTES = {
    0: (None, DARK_SKY, BLACK, WHITE),
    1: (None, DARK_SKY, SKY, WHITE),
    2: (None, DARK_SKY, BLACK, (123, 255, 0)),   # background decorations: the accent colour
    5: (None, DARK_SKY, BLACK, BLACK),           # fake spikes
}
COL_NONE, COL_TOP, COL_BOTTOM, COL_FLOOR_CEIL = 0, 5, 6, 9
FAKE_MARK, INVISIBLE_MARK, ONE_SIDED_MARK = (255, 0, 255, 255), (255, 40, 40, 255), (255, 220, 0, 255)


def write_preview():
    """objects_preview.png (the image Tiled shows) from objects.png, in CGB colours with markers:
    magenta corner = fake (looks like a hazard or a block, does nothing), red dashed outline =
    invisible (collision without art), yellow edge = solid from that side only. Also keeps
    objects.tsx's image size and tile count in step with objects.png, so new rows show in Tiled."""
    art = Image.open(ART).convert("L")
    cols, rows = art.width // 16, art.height // 16
    objects = Catalog().objects
    out = Image.new("RGBA", art.size, (0, 0, 0, 0))
    px, apx = out.load(), art.load()
    for tid in range(cols * rows):
        o = objects.get(tid)
        pal = PREVIEW_PALETTES.get(o["palette"] if o else 0, PREVIEW_PALETTES[0])
        ox, oy = (tid % cols) * 16, (tid // cols) * 16
        blank = True
        for y in range(16):
            for x in range(16):
                i = _index(apx[ox + x, oy + y])
                if i:
                    blank = False
                    px[ox + x, oy + y] = pal[i] + (255,)
        if not o:
            continue
        col = o["collision"]
        if "FAKE" in o["name"] or o["palette"] == 5:
            for y in range(4):
                for x in range(4 - y):
                    px[ox + x, oy + y] = FAKE_MARK
        if (blank or "INVIS" in o["name"]) and col != COL_NONE:
            for k in range(0, 16, 2):
                for x, y in ((k, 0), (k + 1, 15), (0, k + 1), (15, k)):
                    px[ox + x, oy + y] = INVISIBLE_MARK
        for edge, cols_ in ((0, (COL_TOP, COL_FLOOR_CEIL)), (15, (COL_BOTTOM, COL_FLOOR_CEIL))):
            if col in cols_:
                for x in range(16):
                    px[ox + x, oy + edge] = ONE_SIDED_MARK
    try:
        out.save(PREVIEW)
    except OSError as e:   # open in another program (Tiled): the preview just stays as it was
        print("warning: could not write %s (%s)" % (PREVIEW.name, e))
    tsx = TSX.read_text(encoding="utf-8")
    new = re.sub(r'tilecount="\d+"', 'tilecount="%d"' % (cols * rows), tsx, count=1)
    new = re.sub(r'<image source="[^"]*" width="\d+" height="\d+"/>',
                 '<image source="%s" width="%d" height="%d"/>' % (PREVIEW.name, art.width, art.height),
                 new, count=1)
    if new != tsx:
        TSX.write_text(new, encoding="utf-8")


if __name__ == "__main__":
    write_preview()


SPRITES_TSX = CATALOG_DIR / "sprites.tsx"
SPRITES_ART = CATALOG_DIR / "sprites.png"
SPRITES_PREVIEW = CATALOG_DIR / "sprites_preview.png"


def write_sprite_preview():
    """sprites_preview.png (the image Tiled shows for sprites.tsx) from sprites.png: the sprites
    the game does not support (engine_id -1) dimmed and crossed out"""
    from PIL import ImageDraw
    img = Image.open(SPRITES_ART).convert("RGBA")
    root = ET.parse(SPRITES_TSX).getroot()
    cols = int(root.get("columns"))
    draw = ImageDraw.Draw(img)
    for tile in root.findall("tile"):
        props = {p.get("name"): p.get("value") for p in tile.iter("property")}
        if int(props.get("engine_id", -1)) >= 0:
            continue
        i = int(tile.get("id"))
        ox, oy = (i % cols) * 16, (i // cols) * 16
        cell = img.crop((ox, oy, ox + 16, oy + 16))
        cell.putalpha(cell.getchannel("A").point(lambda a: a * 30 // 100))
        img.paste(cell, (ox, oy))
        draw.line((ox + 2, oy + 2, ox + 13, oy + 13), fill=(255, 40, 40, 255))
        draw.line((ox + 13, oy + 2, ox + 2, oy + 13), fill=(255, 40, 40, 255))
    try:
        img.save(SPRITES_PREVIEW)
    except OSError as e:
        print("warning: could not write %s (%s)" % (SPRITES_PREVIEW.name, e))
