"""The GDA metatiles we do not have yet (by levels/objects/gda_ours.json), colour channel copies
counted once.

    python tools/gda_pieces.py            list the pieces GDA's 1.0-1.6 official levels use
    python tools/gda_pieces.py --all      every piece any GDA level uses
"""
import collections
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda  # noqa: E402
import gda_match  # noqa: E402

COLOUR = re.compile(r"COL\d|LBG")


def plain(name):
    """a GDA metatile name without its colour channel parts (COL1_X, 1.7_COL2_X, LBG_X -> X / 1.7_X)"""
    return "_".join(t for t in name.split("_") if not COLOUR.fullmatch(t))


def have():
    """plain names of the GDA metatiles one of our objects is (gda_ours.json)"""
    out = set()
    for m in json.loads((ROOT / "levels" / "objects" / "gda_ours.json").read_text()).values():
        if m["gda"]:
            out |= {plain(p) for k, p in gda_match.parse_label(m["gda"]) if k in ("MT", "SPMT")}
    return out


def missing(levels=None):
    """{plain name: (GDA metatile id to take the art and collision from, levels using it)} of the
    pieces we lack that `levels` (default: the shared 1.0-1.6 official levels; "all": any) use.
    The art comes from the plain metatile when GDA has one, else its first colour copy."""
    mts = gda.metatiles()
    first = {}
    for i, (n, _, _) in enumerate(mts):
        if i == 0:
            continue
        p = plain(n)
        if p not in first or (n == p and mts[first[p]][0] != p):
            first[p] = i
    used = collections.defaultdict(set)
    for name in gda.level_names():
        if levels != "all" and name not in (levels or gda_match.SHARED):
            continue
        w, h, layers, objs = gda.read_level(name)
        gids = [g for ln in ("l1", "l2") for row in layers.get(ln, []) for g in row]
        gids += [o["gid"] for o in objs if o["gid"] < gda.SPRITE_GID]
        for g in gids:
            if 1 < g <= len(mts):
                used[plain(mts[g - 1][0])].add(name)
    got = have() | {"EMPTY", "AIR"}
    return {p: (first[p], sorted(used[p])) for p in sorted(used) if p not in got}


if __name__ == "__main__":
    m = missing("all" if "--all" in sys.argv else None)
    mts = gda.metatiles()
    for p, (i, lv) in m.items():
        print("%-36s %-34s %s" % (p, mts[i][2], ", ".join(lv[:4]) + (" ..." if len(lv) > 4 else "")))
    print(len(m), "pieces")
