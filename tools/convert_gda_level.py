"""Converts Geometry Dash Advance levels (../geometry_dash_advance/levels/*.tmx) to our TMX format.

GDA levels have two tile layers, l1 (drawn on top, GBA BG0) and l2. Ours have one: a cell takes
l1's metatile, or l2's where l1 is empty. Each GDA metatile becomes the catalog object that
levels/objects/gda_objects.json names for it (GDA name -> our object name, null = we have none).

    python tools/convert_gda_level.py --learn            (re)make gda_objects.json
    python tools/convert_gda_level.py --report           GDA objects we are missing, over all levels
    python tools/convert_gda_level.py clubstep [out.tmx] convert one level (default:
                                                         levels/chr_data/tmx/gda_<name>.tmx)

--learn: the 1.6 objects map by their gda_name property; the rest is learned from the levels both
games have (Famidash's and GDA's versions of the same level, lined up): each GDA metatile gets the
object most often in its place, when that is clear (MIN_SHARE) and the collisions agree. Edit the
JSON by hand to fix or add entries; --learn keeps hand edits (entries marked in "manual").

Missing objects are left empty in the converted map; the sprites (GDA's SP object layer) are not
converted yet.
"""
import collections
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GDA = ROOT.parent / "geometry_dash_advance"
OUR_TMX = ROOT / "levels" / "chr_data" / "tmx"
MAPPING = ROOT / "levels" / "objects" / "gda_objects.json"
sys.path.insert(0, str(ROOT / "tools"))
from objects_catalog import SPRITES_FIRSTGID, load_catalog  # noqa: E402

# Levels both games have (GDA name: ours)
SHARED = {n: n for n in ("backontrack", "baseafterbase", "cantletgo", "clutterfunk", "cycles", "dryout",
                         "jumper", "leveleasy", "polargeist", "stereomadness", "theoryofeverything",
                         "timemachine", "xstep", "clubstep", "electromanadventures")}
SHARED["ultimatedestruction"] = "ultiatedestruction"
MIN_SHARE, MIN_VOTES = 0.6, 3
# Set by hand (GDA name pattern -> our object), where the shared levels do not decide it: GDA
# fills ground, ceilings and black blocks with filler pieces where Famidash has ground looks
RULES = [
    (r"((COL\d|LBG)_)?BLK_FILLER", "BLACK_NONE"),
    (r"CEILING_\d", "BG6"), (r"CEILING_[A-F]", "BG5"),
    (r"GROUND_\d", "GROUND_TOP"), (r"GROUND_[A-F]", "GROUND_EDGE_TOP"),
    (r"INVISIBLE_FLOOR_CEIL", "SOLID_EMPTY_TILE"),
    (r"GROUND_SPIKE_BOTTOM", "BOTTOM_BACKGROUND_SPIKES"), (r"GROUND_SPIKE_TOP", "TOP_BACKGROUND_SPIKES"),
    (r"GROUND_SPIKE_LEFT", "PITSPIKES_LEFT"), (r"GROUND_SPIKE_RIGHT", "PITSPIKES_RIGHT"),
    (r"GROUND_WAVY_SPIKE_BOTTOM", "BOTTOM_WAVYPIT"), (r"GROUND_WAVY_SPIKE_TOP", "TOP_WAVYPIT"),
    (r"(1\.6_)?SMALL_SPIKE_LEFT", "HALF_SPIKE_LEFT"), (r"(1\.6_)?SMALL_SPIKE_RIGHT", "HALF_SPIKE_RIGHT"),
    (r"(1\.6_)?SMALL_SPIKE_TOP", "HALF_SPIKE_TOP"), (r"(1\.6_)?SMALL_SPIKE_BOTTOM", "SPIKE_HALF"),
]

# GDA collision -> the kind of collision our object must have to stand in for it
SOLID, NONE, HAZARD = "solid", "none", "hazard"


def gda_kind(col):
    if col in ("COL_NONE",):
        return NONE
    if "SPIKE" in col or col in ("COL_DEATH",):
        return HAZARD
    if col in ("COL_FULL", "COL_FLOOR_CEIL") or "SLAB" in col or "MINIBLOCK" in col:
        return SOLID
    return None   # orbs, slopes, ...: not checked


def our_kind(col):
    if col == 0:
        return NONE
    if col in (1, 2, 3, 4, 8, 16, 17, 18, 19, 20, 21) or 0x2C <= col < 0x3A:
        return HAZARD
    return SOLID


def gda_metatiles():
    """[(name, collision type)] in GDA metatile id order"""
    src = (GDA / "src" / "metatiles.c").read_text()
    return [(m.group(1), m.group(2)) for m in
            re.finditer(r'Metatile\("([^"]*)",(?:\s*TILE\([^)]*\),){4}\s*(\w+)\)', src)]


def read_layers(path):
    root = ET.parse(path).getroot()
    w, h = int(root.get("width")), int(root.get("height"))
    layers = {}
    for layer in root.findall("layer"):
        v = [int(x) & 0x0FFFFFFF for x in layer.find("data").text.replace("\n", "").split(",") if x.strip()]
        layers[layer.get("name") or "map"] = [v[y * w:(y + 1) * w] for y in range(h)]
    return w, h, layers


def gda_map(path):
    """GDA level -> (w, h, rows of GDA metatile ids, -1 = empty): l1 on top of l2"""
    w, h, layers = read_layers(path)
    l1, l2 = layers["l1"], layers.get("l2")
    pick = lambda a, b: a if a > 1 else (b if b and b > 1 else 0)
    return w, h, [[pick(l1[y][x], l2[y][x] if l2 else 0) - 1 for x in range(w)] for y in range(h)]


# GDA block sets that are our terrain sets (tools/make_terrain_sets.py): their pieces map by the
# sides their names say are outlined (air there), which the shared levels cannot always decide
TERRAIN_SETS = {"CHK": "Checkerboard blocks", "BRICKS": "Checkerboard blocks (set B)",
                "X": "X blocks", "BLK": "Black blocks"}
PIECE = re.compile(r"(CHK|BRICKS|X|BLK)_(SIDE_(UP|DOWN|LEFT|RIGHT|TOP|BOTTOM)|CORNER_[TB][LR]|INS_CRN_[TB][LR]|"
                   r"DB_CRN_(TOP|BOT|LFT|RGT)|DB_SIDE_[HV]|FULL|FILLER)")


def name_air(name):
    """The sides a block set piece's name says are air (outlined), as a set of T, B, L, R; DB_SIDE_H
    gives both readings ("TB" or "LR"): GDA's sets do not agree on it"""
    rest = name.split("_", 1)[1]
    side = {"UP": "T", "TOP": "T", "DOWN": "B", "BOTTOM": "B", "BOT": "B", "LEFT": "L", "LFT": "L",
            "RIGHT": "R", "RGT": "R"}
    if rest.startswith("SIDE_"):
        return [{side[rest[5:]]}]
    if rest.startswith("CORNER_"):
        return [set(rest[7:])]
    if rest.startswith("DB_CRN_"):
        open_side = {"T": "B", "B": "T", "L": "R", "R": "L"}[side[rest[7:]]]
        return [set("TBLR") - {open_side}]
    if rest.startswith("DB_SIDE_"):
        return [{"T", "B"}, {"L", "R"}]
    return [set("TBLR")] if rest == "FULL" else [set()]


def gda_terrains():
    """{GDA metatile name: wangid} of the pieces of TERRAIN_SETS, read from GDA's art (white = the
    outline) like tools/make_terrain_sets.py reads ours: GDA's names are not consistent (its black
    blocks' DB_SIDE_H / V are the other way round from its checkerboards')"""
    import import_gda_16
    import make_terrain_sets
    chr_ = (GDA / "graphics" / "tileset.chr").read_bytes()
    pal = (GDA / "graphics" / "tileset.pal").read_bytes()
    out = {}
    for name, tiles, _ in import_gda_16.gda_metatiles():
        if not PIECE.fullmatch(name):
            continue
        px = import_gda_16.gda_art(tiles, chr_, pal)
        quarters = [[[px[(q >> 1) * 8 + y][(q & 1) * 8 + x] for x in range(8)] for y in range(8)] for q in range(4)]
        w = make_terrain_sets.terrain(quarters)
        air = {k for k, v in zip("TRBL", w[0::2]) if not v}
        if air in name_air(name):   # the art and the name agree
            out[name] = w
    return out


def terrain_pieces():
    """{terrain set name: {wangid: catalog object name}} from objects.tsx"""
    import make_terrain_sets
    cat = load_catalog()
    out = {}
    for name, _, pattern in make_terrain_sets.STYLES:
        pieces = {}
        for i, o in sorted(cat.objects.items()):
            if re.fullmatch(pattern, o["name"]):
                w = make_terrain_sets.TERRAIN_OVERRIDES.get(o["name"]) or                     make_terrain_sets.notched(o["name"], make_terrain_sets.terrain(cat.art(i)))
                pieces.setdefault(w, o["name"])
        out[name] = pieces
    return out


def learn():
    cat = load_catalog()
    terrain = terrain_pieces()
    gda_terrain = gda_terrains()
    names = gda_metatiles()
    mapping = json.loads(MAPPING.read_text()) if MAPPING.exists() else {"manual": [], "objects": {}}
    votes = collections.defaultdict(collections.Counter)
    for gname, oname in SHARED.items():
        gw, gh, gm = gda_map(GDA / "levels" / (gname + ".tmx"))
        ow, oh, ol = read_layers(OUR_TMX / (oname + ".tmx"))
        om = next(v for k, v in ol.items() if k != "SP")
        dy = oh - gh   # bottoms lined up (the ground); x offset found by matching filled cells

        def agree(dx):
            return sum(1 for y in range(max(0, -dy), gh) if 0 <= y + dy < oh
                       for x in range(0, gw, 3) if 0 <= x + dx < ow and gm[y][x] >= 0 and om[y + dy][x + dx] > 1)
        dx = max(range(-8, 9), key=agree)
        for y in range(gh):
            for x in range(gw):
                if gm[y][x] >= 0 and 0 <= y + dy < oh and 0 <= x + dx < ow and om[y + dy][x + dx] > 0:
                    votes[gm[y][x]][om[y + dy][x + dx] - 1] += 1
    by_gda = {o["gda_name"]: o["name"] for o in cat.objects.values() if o.get("gda_name")}
    objects = {}
    for gid, (gname, col) in enumerate(names):
        rule = next((o for r, o in RULES if re.fullmatch(r, gname)), None)
        if gname in mapping["manual"]:
            objects[gname] = mapping["objects"].get(gname)
        elif rule:
            objects[gname] = rule
        elif gname in gda_terrain and gda_terrain[gname] in terrain[TERRAIN_SETS[gname.split("_")[0]]]:
            objects[gname] = terrain[TERRAIN_SETS[gname.split("_")[0]]][gda_terrain[gname]]
        elif gname in by_gda:
            objects[gname] = by_gda[gname]
        elif gid in votes:
            (oid, n), = votes[gid].most_common(1)
            total = sum(votes[gid].values())
            o = cat.objects.get(oid)
            ok = o and n >= MIN_VOTES and n >= MIN_SHARE * total and \
                gda_kind(col) in (None, our_kind(o["collision"]))
            objects[gname] = o["name"] if ok else None
    # GDA's colour channel copies (COL1..4_X, LBG_X, 1.7_COL1_X, ...): we have one look per object,
    # so a copy is the object its plain name maps to
    for gname, _ in names:
        plain = "_".join(t for t in gname.split("_") if not re.fullmatch(r"COL\d|LBG", t))
        if not objects.get(gname) and plain != gname and objects.get(plain) and gname not in mapping["manual"]:
            objects[gname] = objects[plain]
    mapping["objects"] = objects
    MAPPING.write_text(json.dumps(mapping, indent=1, sort_keys=False) + "\n")
    known = sum(1 for v in objects.values() if v)
    print("%s: %d GDA metatiles mapped, %d without an object" % (MAPPING.name, known, len(objects) - known))


def load_mapping():
    """GDA metatile id -> catalog tile id (None: no object)"""
    cat = load_catalog()
    by_name = cat.by_name
    objects = json.loads(MAPPING.read_text())["objects"]
    return [by_name.get(objects.get(n) or "") for n, _ in gda_metatiles()]


def gda_name(names, g):
    return names[g][0] if g < len(names) else "unknown id %d" % g


def level_names():
    return sorted(p.stem for p in (GDA / "levels").glob("*.tmx"))


def report():
    names = gda_metatiles()
    ids = load_mapping()
    missing = collections.defaultdict(lambda: [0, set()])
    for level in level_names():
        _, _, gm = gda_map(GDA / "levels" / (level + ".tmx"))
        for row in gm:
            for g in row:
                if g > 0 and (g >= len(ids) or ids[g] is None):
                    missing[g][0] += 1
                    missing[g][1].add(level)
    # by object set: GDA's colour channel copies (COL1..4, LBG) and orientations together
    groups = collections.defaultdict(lambda: [set(), 0, set()])
    for g, (n, levels) in missing.items():
        name = gda_name(names, g)
        tokens = [t for t in name.split("_") if not re.fullmatch(r"COL\d|LBG", t)]
        fam = "_".join(tokens[:2]) if re.fullmatch(r"1\.\d|1\d|16", tokens[0]) and len(tokens) > 1 else tokens[0]
        groups[fam][0].add(name)
        groups[fam][1] += n
        groups[fam][2] |= levels
    lines = ["# GDA objects we do not have", "",
             "Made by `python tools/convert_gda_level.py --report`: the Geometry Dash Advance metatiles placed in",
             "its levels that `levels/objects/gda_objects.json` has no object for, by object set (GDA's",
             "colour channel copies and orientations together). Converted levels leave them empty.", "",
             "| set | kinds | cells | levels | examples |", "|---|---|---|---|---|"]
    for fam, (kinds, n, levels) in sorted(groups.items(), key=lambda kv: -kv[1][1]):
        lv = sorted(levels)
        shared = sorted(l for l in lv if l in SHARED)
        where = "%d (%s)" % (len(lv), ", ".join(shared) if shared else "GDA only")
        ex = ", ".join(sorted(kinds)[:3]) + (", ..." if len(kinds) > 3 else "")
        lines.append("| %s | %d | %d | %s | %s |" % (fam, len(kinds), n, where, ex))
    out = ROOT / "docs" / "GDA_MISSING_OBJECTS.md"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("%d object sets missing, %d kinds: %s" % (len(groups), sum(len(k) for k, _, _ in groups.values()),
                                                     out.relative_to(ROOT)))


TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.11.2" orientation="orthogonal" renderorder="right-down" width="{w}" height="{h}" tilewidth="16" tileheight="16" infinite="0" nextlayerid="3" nextobjectid="1">
 <tileset firstgid="1" source="../../objects/objects.tsx"/>
 <tileset firstgid="{sp}" source="../../objects/sprites.tsx"/>
 <layer id="1" width="{w}" height="{h}">
  <data encoding="csv">
{map}
</data>
 </layer>
 <layer id="2" name="SP" width="{w}" height="{h}">
  <data encoding="csv">
{sp_layer}
</data>
 </layer>
</map>
"""


def csv(rows):
    return ",\n".join(",".join(map(str, r)) for r in rows)


def convert(level, out=None):
    ids = load_mapping()
    w, h, gm = gda_map(GDA / "levels" / (level + ".tmx"))
    lost = collections.Counter()
    rows = []
    for row in gm:
        out_row = []
        for g in row:
            tid = ids[g] if 0 <= g < len(ids) else None
            if g > 0 and tid is None:
                lost[g] += 1
            out_row.append(tid + 1 if tid is not None else 0)
        rows.append(out_row)
    out = Path(out) if out else OUR_TMX / ("gda_%s.tmx" % level)
    out.write_text(TEMPLATE.format(w=w, h=h, sp=SPRITES_FIRSTGID, map=csv(rows),
                                   sp_layer=csv([[0] * w for _ in range(h)])), encoding="utf-8")
    names = gda_metatiles()
    print("%s -> %s (%dx%d)" % (level, out.relative_to(ROOT) if out.is_relative_to(ROOT) else out, w, h))
    if lost:
        print("  left empty, no object for: " + ", ".join("%s x%d" % (gda_name(names, g), n) for g, n in lost.most_common()))


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args:
        print(__doc__)
    elif args[0] == "--learn":
        learn()
    elif args[0] == "--report":
        report()
    else:
        convert(args[0], args[1] if len(args) > 1 else None)
