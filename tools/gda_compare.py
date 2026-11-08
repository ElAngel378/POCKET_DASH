"""Our object levels as Geometry Dash Advance levels, to compare the two (docs/PLAN_1.6.md, end of
phase 4): palettes, and that every object shows up where it should.

GDA's 16x16 pieces on the block grid go on its l1 / l2 tile layers, as in its own levels (fading
and breaking work there); the others on its SP layer: GDA's sprite objects for its sprite
decorations, saws and spikes, its "metatile as a sprite" object for pieces half a block off the
grid or turned, so they come out as in Tiled. Our colour triggers become GDA colour
triggers with the same NES colour (Famidash's palette). Objects made of quarters (big cogwheel,
spiked wheel...) are one GDA object: the unturned quarter places it, the others add nothing.

    python tools/gda_compare.py test test2      writes ../geometry_dash_advance/levels/<name>.json

The written levels replace GDA's own levels of those names (git checkout there restores them).
"""
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda
import gd_objects as GO
import level_objects as LO
import import_gda_art as IA
from gen_famidash_bg_tables import NES

TMX = ROOT / "levels" / "chr_data" / "tmx"
PICS = ROOT / "levels" / "chr_data" / "objects" / "tiles"

# our object -> GDA metatile (placed as a sprite) or ("S", sprite type). From the levels themselves,
# not by eye: what GDA has in the cell where GD's own Electroman, Clubstep, Clutterfunk place the
# object unturned (GD's Resources/levels/*.txt against GDA's levels/*.tmx). GDA's outlines and light
# lines come with their fill: one piece for what GD stacks (see FILL_UNDER).
METATILE = {
    "gd1": "BASIC_BLOCK", "gd5": "CHK_FILLER", "gd8": "SPIKE_UP", "gd39": "SMALL_SPIKE_BOTTOM",
    "gd40": "SLAB_UP", "gd76": "+_FULL", "gd77": "+_SIDE_UP", "gd78": "+_CORNER_TL",
    "gd81": "+_DB_CRN_TOP", "gd82": "+_DB_SIDE_V", "gd83": "CHK_FULL", "gd103": "MEDIUM_SPIKE_BOTTOM",
    "gd143": "BREAKABLE_BRICK", "gd146": "FADING_BLOCK",
    "gd157": "1.6_WAVE_DECO_MIDDLE", "gd158": "1.6_WAVE_DECO_LFT", "gd159": "1.6_WAVE_DECO_RGT",
    "gd161": "1.6_SIDE_TOP", "gd162": "1.6_CORNER_TL", "gd163": "1.6_INS_CRN_TL",
    "gd164": "1.6_SIDE_TOP_NO_OUT", "gd165": "1.6_DBL_CRN_TOP", "gd166": "1.6_DBL_SIDE_V",
    "gd167": "1.6_DBL_INS_CRN_TOP", "gd168": "1.6_BLOCK", "gd169": "1.6_BLOCK_ALT",
    "gd193": "1.6_FILLER", }
METATILE.update({_k: _g for _k, _g, _n, _s in GO.GDA_PIECES})
METATILE["gda_ground_spikes"] = "GROUND_SPIKE_BOTTOM"
# fills GDA's outlines already contain: an outline on one of these replaces it
FILL_UNDER = {"gd193"}
SPRITE = {k: v[0] for k, v in IA.ART.items()}
SPRITE.update({k: v[0] for k, v in IA.SPIKES.items()})
SPRITE.update({k: v[0] for k, v in IA.GROUND_DECO.items()})
SPRITE.update({k: v[0] for k, v in IA.BLACK_SAWS.items()})
SPRITE.update({"gd129": "MEDIUM_CLOUD", "gd130": "BIG_CLOUD",   # as GD: 130 is the big one (our art is swapped)
               "gd48": "BIG_BG_CLOUDS", "gd49": "SMALL_BG_CLOUDS", "gd113": "BIG_BG_SQUARES",
               "gd114": "MEDIUM_BG_SQUARES", "gd115": "SMALL_BG_SQUARES",
               "gd187": "MEDIUM_OUTLINE_SAW", "gd188": "SMALL_OUTLINE_SAW",
               "gd178": "ONE_POINT_SIX_HALF_SPIKE_V"})
SPRITE.pop("gd146", None)
# crop box of our art in GDA's image (import_gda_art), for the ones cut from it
CROP = {k: v[1] for k, v in list(IA.ART.items()) + list(IA.BLACK_SAWS.items()) if v[1]}
# our sprite decorations (sprites.png cell -> GDA sprite type)
SP_DECO = {112: "PULSING_CIRCUNFERENCE", 113: "PULSING_HEARTH", 73: "PULSING_CIRCLE"}
INV_TF = {v: k for k, v in GO.TRANSFORMS.items()}
# our transform -> GDA's (hflip, vflip, rotation angle or None)
TF = {"": (0, 0, None), "H": (0 + 1, 0, None), "V": (0, 1, None), "R180": (1, 1, None),
      "R90": (0, 0, 90), "R270": (0, 0, 270), "D": (1, 0, 90), "DHV": (1, 0, 270)}


def content_box(img):
    """bbox of the art, without a fill colour covering most of the image (GDA's hills, clouds)"""
    data = list(img.getdata())
    used = [v for v in data if v]
    if not used:
        return None
    top = max(set(used), key=used.count)
    if used.count(top) > 0.4 * len(data) and len(set(used)) > 1:
        img = img.point([0 if v in (0, top) else 255 for v in range(256)])
    return img.getbbox()


def our_box(key):
    a = Image.open(PICS / ("%s.png" % key)).convert("RGBA").split()[3]
    return a.getbbox() or (0, 0) + a.size


def sprite_place(key, x, y, tid):
    """GDA object top left for our object `key` with its top left at x, y"""
    art, ox, oy = gda.sprite_art(tid)
    if key in CROP:
        ix, iy = x - CROP[key][0], y - CROP[key][1]
    elif key in IA.HUB:
        w, h = GO.OBJECTS[key][2]
        ix, iy = x - (w // 2 - IA.HUB[key][0]), y - (h // 2 - IA.HUB[key][1])
    else:
        ob, gb = our_box(key), content_box(art)
        # centres across, bottoms (ground decorations sit on something) down
        ix = x + (ob[0] + ob[2]) // 2 - (gb[0] + gb[2]) // 2
        iy = y + ob[3] - gb[3]
    return ix + ox, iy + oy


def obj(gid, x, top, props, oid):
    return {"gid": gid, "height": 16, "id": oid, "name": "", "rotation": 0, "type": "",
            "visible": True, "width": 16, "x": x, "y": top + 16,
            "properties": props}


def tf_props(tf):
    h, v, rot = TF[tf]
    p = []
    if h:
        p.append({"name": "hflip", "type": "bool", "value": True})
    if v:
        p.append({"name": "vflip", "type": "bool", "value": True})
    if rot is not None:
        p += [{"name": "enable rotation", "type": "bool", "value": True},
              {"name": "rotation angle", "type": "int", "value": rot}]
    return p


def colour(i):
    return "#ff%06x" % NES[i & 0x3F]


def convert(name):
    root = ET.parse(TMX / ("%s.tmx" % name)).getroot()
    w, h = int(root.get("width")), int(root.get("height"))
    mt_names = {n: i for i, (n, _, _) in reversed(list(enumerate(gda.metatiles())))}
    types = gda.sprite_types()
    objs, missing = [], set()
    fills = {mt_names[METATILE[k]] + 1 for k in FILL_UNDER}
    tiles = {"l1": [0] * (w * h), "l2": [0] * (w * h)}
    oid = 1
    for key, x, y, fl in LO.tmx_objects(root):
        tf = INV_TF[fl]
        if key in METATILE:
            mb = Image.open(PICS / ("%s.png" % key)).size
            mid = mt_names[METATILE[key]]
            if not tf and mb == (16, 16) and not x % 16 and not y % 16 and 0 <= y < h * 16:
                i = (y // 16) * w + x // 16
                if key in GO.OUTLINE and tiles["l1"][i] in fills:
                    tiles["l1"][i] = mid + 1
                    continue
                layer = "l1" if not tiles["l1"][i] else "l2" if not tiles["l2"][i] else None
                if layer:
                    tiles[layer][i] = mid + 1
                    continue
            # GDA's piece is a whole cell: our smaller ones (8 px spikes) sit at its bottom middle
            gx, gy = x - (16 - mb[0]) // 2, y - (16 - mb[1])
            objs.append(obj(mid + 1, gx, gy, tf_props(tf), oid))
        elif key in SPRITE:
            if key in CROP and tf:
                continue        # a quarter of a wheel: the unturned one places the whole
            tid = types.index(SPRITE[key])
            gx, gy = sprite_place(key, x, y, tid)
            objs.append(obj(gda.SPRITE_GID + tid, gx, gy, tf_props(tf), oid))
        else:
            missing.add(key)
            continue
        oid += 1
    # sprites: colour triggers, our pulsing decorations
    first = None
    for ts in root.findall("tileset"):
        if ts.get("source", "").endswith("sprites.tsx"):
            first = int(ts.get("firstgid"))
    for group in root.findall("objectgroup"):
        if group.get("name") != "SP":
            continue
        for o in group.findall("object"):
            if o.get("gid") is None:
                continue
            s = int(o.get("gid")) - first
            x, y = int(float(o.get("x"))), int(float(o.get("y"))) - 16
            if s == 256:
                props = {p.get("name"): p.get("value") for p in o.findall("properties/property")}
                objs.append(obj(gda.SPRITE_GID + types.index("COL_TRIGGER"), x, y, [
                    {"name": "Channel", "type": "string", "value": "BG"},
                    {"name": "Color", "type": "color", "value": props.get("Colour", "#fffffeff")},
                    {"name": "Frames", "type": "int", "value": 18}], oid))
            elif s in SP_DECO:
                objs.append(obj(gda.SPRITE_GID + types.index(SP_DECO[s]), x, y, [], oid))
            else:
                missing.add("sprite %d" % s)
                continue
            oid += 1
    objs.sort(key=lambda o: (o["x"], o["y"]))
    template = json.loads((gda.LEVELS / "test.json").read_text())
    out = dict(template)
    out.update(width=w, height=h, nextobjectid=oid)
    out["layers"] = [
        {"data": tiles["l2"], "height": h, "id": 4, "name": "l2", "opacity": 1, "type": "tilelayer",
         "visible": True, "width": w, "x": 0, "y": 0},
        {"data": tiles["l1"], "height": h, "id": 1, "name": "l1", "opacity": 1, "type": "tilelayer",
         "visible": True, "width": w, "x": 0, "y": 0},
        {"draworder": "topdown", "id": 7, "name": "SP", "objects": objs, "opacity": 1,
         "type": "objectgroup", "visible": True, "x": 0, "y": 0}]
    props = {p["name"]: p for p in template["properties"]}
    props["BG"]["value"] = colour(0x11)          # our levels start in NES colour 0x11
    props["Level name"]["value"] = "GB " + name.upper()
    out["properties"] = list(props.values())
    (gda.LEVELS / ("%s.json" % name)).write_text(json.dumps(out, indent=1))
    print("%s: %d GDA objects%s" % (name, len(objs), (", not in GDA: " + ", ".join(sorted(missing))) if missing else ""))


if __name__ == "__main__":
    for n in sys.argv[1:] or ["test", "test2"]:
        convert(n)
