"""Reads the object catalog: levels/objects/objects.tsx (a Tiled tileset) + objects.png.

    from objects_catalog import load_catalog
    cat = load_catalog()
    cat.objects[i]  -> {"name", "collision", "palette", "famidash_id", "dmg_art"} (tile id i)
    cat.by_name[name] -> tile id
    cat.art(i)      -> 4 quarters (TL, TR, BL, BR) of 8x8 colour indices (0 white .. 3 black)

See tools/make_object_catalog.py for what the properties mean.
"""
import xml.etree.ElementTree as ET
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CATALOG_DIR = ROOT / "levels" / "objects"
TSX = CATALOG_DIR / "objects.tsx"
# The sprite tileset comes after the catalog in a level's TMX: room for 4096 objects
SPRITES_FIRSTGID = 4097


class Catalog:
    def __init__(self, tsx=TSX):
        root = ET.parse(tsx).getroot()
        self.columns = int(root.get("columns"))
        self.tilecount = int(root.get("tilecount"))
        image = root.find("image")
        img = Image.open(tsx.parent / image.get("source")).convert("L")
        self._px = img.load()
        self.objects = {}
        self.dmg_only = set()
        for tile in root.findall("tile"):
            tid = int(tile.get("id"))
            props = {}
            for p in tile.find("properties").findall("property"):
                v = p.get("value")
                t = p.get("type", "string")
                props[p.get("name")] = int(v) if t == "int" else (v == "true") if t == "bool" else v
            if props.get("dmg_only"):
                self.dmg_only.add(tid)
            else:
                self.objects[tid] = props
        self.by_name = {o["name"]: i for i, o in self.objects.items()}
        assert len(self.by_name) == len(self.objects), "object names must be unique"

    def art(self, tid):
        ox, oy = (tid % self.columns) * 16, (tid // self.columns) * 16
        idx = lambda v: min(range(4), key=lambda i: abs(v - (255 - 85 * i)))
        return tuple(tuple(tuple(idx(self._px[ox + (q & 1) * 8 + x, oy + (q >> 1) * 8 + y])
                                 for x in range(8)) for y in range(8)) for q in range(4))

    def dmg_art(self, tid):
        d = self.objects[tid].get("dmg_art", -1)
        return self.art(d if d >= 0 else tid)


def load_catalog():
    return Catalog()


# Level ids the engine treats specially (src/player.c, src/graphics/mt_renderer.c,
# tools/build_bg_tiles.py SAW_METATILES): only the object with that famidash_id may have them.
ENGINE_IDS = {0, 1, 2, 5, 6, 136, 137, 12, 24, 4, 8, 9, 10, 11, 120, 125, 127} | set(range(0x74, 0x80))


def tmx_level_objects(tmx_path):
    """Catalog tile ids the map layer of a level's TMX uses (None: the TMX is not converted)"""
    import xml.etree.ElementTree as ET
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
