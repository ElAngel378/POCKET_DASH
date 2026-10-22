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
