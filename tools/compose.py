"""Cells no Famidash metatile shows (docs/PLAN_1.6.md, phase 2): any stack of objects at any 8 px
offset. tools/level_objects.py compile_tmx hands them here; each different cell becomes a metatile
of its own in that level, made at build time:

- look: the objects' art, bottom to top per 8x8 quarter. Solid objects (blocks, slabs) are opaque;
  the others (spikes, decorations) show what is behind them where their colour is 0.
- palette (CGB, one per metatile): the topmost solid / deadly object's, else the topmost one's.
- collision: if the solid / deadly objects alone make a Famidash metatile, that metatile's (exactly
  as before); otherwise per 8x8 quarter, solid or deadly (player.c col_quads).
- id: a metatile id the level does not use, with that palette (the palettes and the engine's
  special ids stay as in famidash_metatiles_dmg.c; the collision table is per level).

The objects' art comes from the Famidash metatiles that show them (tools/gd_objects.py FAMIDASH), on
CGB and on DMG; a rotation Famidash never shows is made by turning the art.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_bg_tiles as BG
import bg_stream as BS
import gd_objects as GO

SAW, GROUND = "saw", "ground"
VARIANT_SETS = {None: (None, None), "B": ("B", None), "SB": (None, "B"), "SC": (None, "C"), "NS": (None, None)}
# metatile ids the engine treats by id: never given another look
RESERVED = ({0, 12, 24, BG.SAW_CENTER_MT} | set(BG.SAW_METATILES) | BS.CGB_GROUND_LOOK)
COL_TOP, COL_BOTTOM, COL_ALL, COL_DEATH, COL_QUAD_BASE = 0x05, 0x06, 0x07, 0x08, 0x20
SOLID_CODES = {0x05, 0x06, 0x07, 0x09}
SPECIAL_CODES = set(range(0x0A, 0x10))   # orbs, pads
# fills: no collision, but opaque like the blocks they fill
FILLS = {"gd5", "gd120", "fd_x_none", "fd_black_none", "fd_bg5", "fd_bg6"}


def c_array(src, name):
    a = src.index(name)
    return list(map(int, re.findall(r'\d+', src[src.index("{", a):src.index("};", a)])))


class Tables:
    def __init__(self):
        src = (ROOT / "include" / "famidash_metatiles_dmg.c").read_text()
        self.pal = c_array(src, "famidash_metatile_palettes[FAMIDASH_NUM_METATILES] =")
        self.col = c_array(src, "famidash_metatile_collision[FAMIDASH_NUM_METATILES] =")
        p = (ROOT / "src" / "player.c").read_text()
        a = p.index("col_quads[COL_QUAD_COUNT] = {")
        self.quads = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', p[a:p.index("};", a)])]


def flags_of(tf):
    return GO.TRANSFORMS[tf]


def transform_img(img, w, h, fl):
    """img: {(x, y): v} of a w x h picture at no rotation -> the picture placed with flags fl"""
    out = {}
    for (x, y), v in img.items():
        W, H = w, h
        if fl & GO.FLIP_D:
            x, y, W, H = y, x, h, w
        if fl & GO.FLIP_H:
            x = W - 1 - x
        if fl & GO.FLIP_V:
            y = H - 1 - y
        out[(x, y)] = v
    return out


def untransform_img(img, w, h, fl):
    """the inverse: a picture placed with flags fl (w x h at no rotation) -> at no rotation"""
    W, H = (h, w) if fl & GO.FLIP_D else (w, h)
    out = {}
    for (x, y), v in img.items():
        if fl & GO.FLIP_V:
            y = H - 1 - y
        if fl & GO.FLIP_H:
            x = W - 1 - x
        if fl & GO.FLIP_D:
            x, y = y, x
        out[(x, y)] = v
    return out


class Composer:
    def __init__(self):
        import level_objects as LO
        self.LO = LO
        self.t = Tables()
        self.bs = BS.Art()
        self.art = {0: {}, 1: {}}      # cgb -> (key, tf) -> {object quarter: pixels | None | SAW | GROUND}
        self.opal = {}
        self.kind = {}
        entries = list(GO.FAMIDASH.items()) + list(GO.FAMIDASH_ALT.items())
        for (m, v), parts in entries:
            big = (m in BG.BIG_SAW_PARTS or m == BG.SAW_CENTER_MT) and v != "NS"
            for cgb in (1, 0):
                qs = self.cell_quarters(m, v, cgb, big)
                for cq, (i, oq) in LO.parts_quarters(parts).items():
                    key, _, _, tf = parts[i]
                    if key not in GO.INVISIBLE:
                        self.art[cgb].setdefault((key, tf), {}).setdefault(oq, qs[cq[0] + 2 * cq[1]])
            for key, dx, dy, tf in parts:
                self.opal.setdefault(key, self.t.pal[m])
            if len(parts) == 1:
                self.kind.setdefault(parts[0][0], self.kind_of_code(self.t.col[m]))
        for key, qs in LO.drawn_art().items():
            for cgb in (1, 0):
                self.art[cgb][(key, "")] = dict(qs)
            self.opal[key] = GO.drawn_pal(key)
            if key in GO.DRAWN_KIND:
                self.kind[key] = GO.DRAWN_KIND[key]
        # black is colour 2 on CGB, 3 on DMG: the dark fill shows what Famidash's black fill shows
        for cgb in (1, 0):
            self.art[cgb][("gd193", "")] = self.art[cgb][("fd_black_none", "")]
        for key in GO.OBJECTS:
            self.kind.setdefault(key, "none")

    def kind_of_code(self, c):
        if c == 0:
            return "none"
        if c in SPECIAL_CODES:
            return "special"
        if c in SOLID_CODES:
            return "solid"
        if c >= COL_QUAD_BASE and c - COL_QUAD_BASE < len(self.t.quads):
            return "solid" if self.t.quads[c - COL_QUAD_BASE] & 0x0F else "deadly"
        return "deadly"

    def cell_quarters(self, m, v, cgb, big):
        px = BS.quarters(self.bs, None, m, cgb, big, VARIANT_SETS[v])
        out = []
        for q, p in enumerate(px):
            if p is None:
                saw = BG.SAW_METATILES.get(m, (None,) * 4)[q] is not None and (m != BG.SAW_CENTER_MT or big)
                p = SAW if saw else GROUND if (cgb and m in BS.CGB_GROUND_LOOK) else None
            out.append(p)
        return out

    def quarter(self, cgb, key, tf, oq):
        """pixels (64) of object quarter oq placed with tf, None if it is empty"""
        a = self.art[cgb].get((key, tf))
        if a is not None and oq in a:
            return a[oq]
        # turn the art Famidash shows in another orientation
        w, h = GO.OBJECTS[key][2]
        for tf0, a0 in sorted(self.art[cgb].items()):
            if tf0[0] != key or tf0 == (key, tf):
                continue
            fl0 = flags_of(tf0[1])
            img = {}
            for (qx, qy), p in a0.items():
                if p in (SAW, GROUND):
                    raise SystemExit("%s cannot be turned (saw / ground art)" % key)
                for i in range(64):
                    img[(qx * 8 + i % 8, qy * 8 + i // 8)] = 0 if p is None else p[i]
            img = transform_img(untransform_img(img, w, h, fl0), w, h, flags_of(tf))
            qx, qy = oq
            if all((qx * 8 + i % 8, qy * 8 + i // 8) in img for i in range(64)):
                px = tuple(img[(qx * 8 + i % 8, qy * 8 + i // 8)] for i in range(64))
                return None if not any(px) else px
        raise SystemExit("no art for %s %s quarter %s" % (key, tf or "-", oq))

    def look(self, cgb, parts):
        """parts: [(key, dx, dy, tf)] bottom to top -> 4 quarters: pixels or None (empty)"""
        out = []
        for q in range(4):
            cqx, cqy = q % 2, q // 2
            acc = [None] * 64
            for key, dx, dy, tf in parts:
                oq = ((cqx * 8 - dx) // 8, (cqy * 8 - dy) // 8)
                if oq not in self.LO.mask(key, flags_of(tf)) or key in GO.INVISIBLE:
                    continue
                px = self.quarter(cgb, key, tf, oq)
                if px in (SAW, GROUND):
                    raise SystemExit("%s: saws and the ground strip cannot share a cell with other objects yet" % key)
                if px is None:
                    continue
                opaque = self.kind[key] == "solid" or key in FILLS
                for i in range(64):
                    if opaque or px[i]:
                        acc[i] = px[i]
            out.append(None if all(v is None for v in acc) else tuple(v or 0 for v in acc))
        return out

    def palette(self, parts):
        hit = [p for p in parts if self.kind[p[0]] != "none"]
        return self.opal.get((hit or parts)[-1][0], 0)

    def collision(self, parts, lookup):
        hit = [p for p in parts if self.kind[p[0]] != "none"]
        if not hit:
            return 0, None
        found = lookup(frozenset(hit))
        if len(found) == 1:
            return self.t.col[found[0]], None
        if any(self.kind[p[0]] == "special" for p in hit):
            return None, "an orb or pad shares its cell with other solid / deadly objects"
        solid = deadly = 0
        for key, dx, dy, tf in hit:
            for qx, qy in self.LO.mask(key, flags_of(tf)):
                cx, cy = (dx + 8 * qx) // 8, (dy + 8 * qy) // 8
                if 0 <= cx < 2 and 0 <= cy < 2:
                    bit = 1 << (cx + 2 * cy)
                    if self.kind[key] == "solid":
                        solid |= bit
                    else:
                        deadly |= bit
        deadly &= ~solid
        if solid == 15:
            return COL_ALL, None
        if deadly == 15 and not solid:
            return COL_DEATH, None
        if not deadly and solid in (0x3, 0xC):
            return (COL_TOP if solid == 0x3 else COL_BOTTOM), None    # half blocks: as slabs
        v = deadly << 4 | solid
        if v in self.t.quads:
            return COL_QUAD_BASE + self.t.quads.index(v), None
        # the nearest quadrant type (fewest quarters different)
        best = min(range(len(self.t.quads)), key=lambda k: bin(self.t.quads[k] ^ v).count("1"))
        return COL_QUAD_BASE + best, "no quadrant type solid %X deadly %X: used %02X" % (solid, deadly, self.t.quads[best])

    def make(self, name, grid, novel, lookup):
        """novel: [(cx, cy, parts bottom to top)] -> fills grid, returns (metatiles {id: (cgb quarters,
        dmg quarters, collision, CGB palette)}, warnings). The level's collision and palette tables
        hold them (tools/build_bg_tiles.py), so any id the level does not use will do."""
        used = {m for row in grid for m in row if m >= 0}
        cells, out, warn = {}, {}, []
        free = [m for m in range(256) if m not in used and m not in RESERVED]
        for cx, cy, parts in novel:
            col, w = self.collision(parts, lookup)
            if w:
                warn.append("%s cell %d, %d: %s" % (name, cx, cy, w))
            if col is None:
                col = 0
            pal = self.palette(parts)
            k = (tuple(self.look(1, parts)), tuple(self.look(0, parts)), pal, col)
            if k not in cells:
                if not free:
                    raise SystemExit("%s: no metatile id left (cell %d, %d): too many different cells"
                                     % (name, cx, cy))
                m = free.pop(0)
                cells[k] = m
                out[m] = (k[0], k[1], col, pal)
            grid[cy][cx] = cells[k]
        return out, warn
