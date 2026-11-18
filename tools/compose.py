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

GROUND = "ground"
# What the colours of the CGB palettes that objects use are (src/famidash_bg.c)
COLOURS = {0: ("sky", "dark", "black", "white"), 2: ("sky", "dark", "black", "player"),
           5: ("sky", "dark", "black", "black"), 6: ("sky", "white", "deep sky", "light sky"),
           7: ("sky", "player2", "mid sky", "light sky")}   # (7's light sky: palette 6's, so a ring over a hill keeps both)
# A cell has one palette: a colour it hasn't becomes the nearest one it has
NEAR = {"player2": ("player", "white"), "player": ("player2", "white"), "white": ("player", "player2")}
# A cell whose objects fit no one palette: its 8x8 quarters choose between two palettes. Its palette
# byte is 0x81 | a bit per quarter (1: the pair's second) << 1 | pair << 5. src/graphics/quarter_pals.c
QUARTER_PAIRS = ((0, 2), (0, 7), (2, 7), (0, 6))
VARIANT_SETS = {None: (None, None), "B": ("B", None), "SB": (None, "B"), "SC": (None, "C"), "NS": (None, None)}
# metatile ids the engine treats by id: never given another look
RESERVED = ({0, 12, 24, BG.SAW_CENTER_MT} | set(BG.SAW_METATILES) | BS.CGB_GROUND_LOOK)
SAW_IDS = {BG.SAW_CENTER_MT} | set(BG.SAW_METATILES)   # reserved only in levels with saws
COL_TOP, COL_BOTTOM, COL_ALL, COL_DEATH, COL_QUAD_BASE = 0x05, 0x06, 0x07, 0x08, 0x20
SOLID_CODES = {0x05, 0x06, 0x07, 0x09}
COL_BREAKABLE, BRICK = 0x16, "gd143"       # on the grid; off it, a plain solid block
SPECIAL_CODES = set(range(0x0A, 0x10))   # orbs, pads
# fills: no collision, but opaque like the blocks they fill
DMG_SLOT_MARGIN = 20      # wide levels: DMG streaming slots kept free (fading looks, Famidash tile variants)
SHARE_GAP = BS.RING + 1    # columns between two looks of one id: the first has left the VRAM ring
FADED_PAL = 7       # the faded look of fading cells on CGB (famidash_bg.c): their tiles, nearer the sky
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
        self.art = {0: {}, 1: {}}      # cgb -> (key, tf) -> {object quarter: pixels | None | GROUND}
        self.opal = {}
        self.kind = {}
        self.mixed = {}
        self.split = {}
        entries = list(GO.FAMIDASH.items()) + list(GO.FAMIDASH_ALT.items())
        self.exact_col = {}           # objects in a cell -> the collision of the metatile they are
        for (m, v), parts in entries:
            self.exact_col.setdefault(frozenset(parts), self.t.col[m])
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
                if not cgb and key in GO.DMG_SWAP:
                    self.art[0][(key, "")] = {q: tuple({2: 3, 3: 2}.get(v, v) for v in p) for q, p in qs.items()}
            self.opal[key] = GO.drawn_pal(key)
            if key in GO.DRAWN_KIND:
                self.kind[key] = GO.DRAWN_KIND[key]
        self.spin = {key: LO.spin_frames(key) for key in GO.SPIN}
        self.ground_keys = {k for k, _tf in self.art[1] for a in [self.art[1][(k, _tf)]] if GROUND in a.values()}
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
                tile = BG.SAW_METATILES.get(m, (None,) * 4)[q]
                if tile is not None and (m != BG.SAW_CENTER_MT or big):
                    p = self.saw_tile(tile, cgb)
                elif cgb and m in BS.CGB_GROUND_LOOK:
                    p = GROUND
            out.append(p)
        return out

    def saw_tile(self, tile, cgb):
        """An old saw's tile in its first frame: a cell with a saw and something else is an ordinary
        tile that stands still, only the saw's free cells use the shared spinning tiles"""
        px = self.bs.sec["SAWS_0"][tile]
        if not cgb:
            rows = BG.dmg_saw_outline([list(px[y * 8:y * 8 + 8]) for y in range(8)])
            px = tuple(v for r in rows for v in r)
        return px if any(px) else None

    def quarter(self, cgb, key, tf, oq, frame=0):
        """pixels (64) of object quarter oq placed with tf (spinning objects: in that frame; DMG only
        has frame 0), None if it is empty"""
        if key in self.spin:
            art = self.spin[key][frame if cgb else 0]
            w, h = GO.OBJECTS[key][2]
            img = {(qx * 8 + i % 8, qy * 8 + i // 8): p[i] for (qx, qy), p in art.items() for i in range(64)}
            if tf:
                img = transform_img(img, w, h, flags_of(tf))
            qx, qy = oq
            px = tuple(img[(qx * 8 + i % 8, qy * 8 + i // 8)] for i in range(64))
            if not cgb and key in GO.DMG_SWAP:
                px = tuple({2: 3, 3: 2}.get(v, v) for v in px)
            return px if any(px) else None
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
                if p == GROUND:
                    raise SystemExit("%s cannot be turned (ground art)" % key)
                for i in range(64):
                    img[(qx * 8 + i % 8, qy * 8 + i // 8)] = 0 if p is None else p[i]
            img = transform_img(untransform_img(img, w, h, fl0), w, h, flags_of(tf))
            qx, qy = oq
            if all((qx * 8 + i % 8, qy * 8 + i // 8) in img for i in range(64)):
                px = tuple(img[(qx * 8 + i % 8, qy * 8 + i // 8)] for i in range(64))
                return None if not any(px) else px
        raise SystemExit("no art for %s %s quarter %s" % (key, tf or "-", oq))

    def covered(self, parts, q):
        """Is quarter q of the cell under a solid object (a block, a slab's half)? Ground decorations
        are behind the blocks (GD): ours are outlines over the sky, a decoration showed through them
        and the quarter took its palette (the block's white outline turned green)"""
        cqx, cqy = q % 2, q // 2
        return any(self.kind[k] == "solid" and ((cqx * 8 - dx) // 8, (cqy * 8 - dy) // 8) in self.LO.mask(k, flags_of(tf))
                   for k, dx, dy, tf in parts)

    def look(self, cgb, parts, frame=0):
        """parts: [(key, dx, dy, tf)] bottom to top -> 4 quarters: pixels or None (empty)"""
        out = []
        shade = cgb and self.shade_cell(parts)
        targets = self.targets(parts) if cgb and not shade else None
        for q in range(4):
            cqx, cqy = q % 2, q // 2
            target = targets[q] if targets else None
            acc = [None] * 64
            hide = self.covered(parts, q)
            for key, dx, dy, tf in parts:
                oq = ((cqx * 8 - dx) // 8, (cqy * 8 - dy) // 8)
                if oq not in self.LO.mask(key, flags_of(tf)) or key in GO.INVISIBLE:
                    continue
                if hide and key in GO.GROUND_DECO:
                    continue
                px = self.quarter(cgb, key, tf, oq, frame)
                if px == GROUND:
                    raise SystemExit("%s: the ground strip cannot share a cell with other objects yet" % key)
                if px is None:
                    continue
                if cgb and key in GO.CGB_RECOLOUR:
                    px = tuple(GO.CGB_RECOLOUR[key].get(v, v) for v in px)
                if shade and key not in GO.SHADES:
                    px = tuple({3: 1, 1: 2}.get(v, v) for v in px)   # white and dark, in palette 6
                elif target is not None and self.opal[key] != target:
                    px = tuple(self.in_palette(v, self.opal[key], target) for v in px)
                opaque = (self.kind[key] == "solid" and key not in GO.OUTLINE and key not in GO.GDA_KEYS) or key in FILLS
                for i in range(64):
                    if opaque or px[i]:
                        acc[i] = px[i]
            out.append(None if all(v is None for v in acc) else tuple(v or 0 for v in acc))
        return out

    def spin_look(self, parts):
        """look(1, parts), a quarter that changes between the frames of a spinning object being
        (frame 0, frame 1, ...)"""
        looks = [self.look(1, parts, f) for f in range(GO.SPIN_FRAMES)]
        if not any(p[0] in self.spin for p in parts):
            return looks[0]
        zero = (0,) * 64
        return [q[0] if len(set(q)) == 1 else tuple(p or zero for p in q) for q in zip(*looks)]

    def shade_cell(self, parts):
        """A background shade piece with only white / black objects (palette 0: outlines, spikes)
        in the cell: palette 6 has the white and a dark shade too, so all of it looks right"""
        return (any(p[0] in GO.SHADES for p in parts)
                and all(p[0] in GO.SHADES or self.opal.get(p[0], 0) == 0 for p in parts))

    def plain_palette(self, parts):
        if not parts:
            return 0
        hit = [p for p in parts if self.kind[p[0]] != "none"]
        return self.opal.get((hit or parts)[-1][0], 0)

    def palette(self, parts):
        if self.shade_cell(parts):
            return 6
        # ground decoration alone: its own palette. With a block or spike in the cell the usual choice
        # (per quarter if that shows more): the decoration's palette has no white, outlines went green
        if all(p[0] in GO.GROUND_DECO or p[0] in GO.INVISIBLE for p in parts):
            return GO.DRAWN_PAL
        split = self.quarter_split(parts)
        return split[0] if split else self.mixed_palette(parts)

    def targets(self, parts):
        """The palette of each quarter of this cell"""
        split = self.quarter_split(parts)
        return split[1] if split else [self.mixed_palette(parts)] * 4

    @staticmethod
    def in_palette(v, own, target):
        """Colour v of palette own, as a colour of palette target"""
        if own not in COLOURS or target not in COLOURS:
            return v
        role, to = COLOURS[own][v], COLOURS[target]
        for wanted in (role,) + NEAR.get(role, ()):
            if wanted in to:
                return to.index(wanted)
        return v

    def quarter_split(self, parts):
        """(palette byte, palette of each quarter) if letting the quarters differ shows more of the
        cell's objects in their own colours than any one palette, else None"""
        key = tuple(parts)
        if key in self.split:
            return self.split[key]
        self.split[key] = None
        pals = {self.opal.get(p[0], 0) for p in parts if p[0] not in GO.INVISIBLE}
        if len(pals) < 2 or not all(p in COLOURS for p in pals) or any(p[0] in GO.FADE for p in parts):
            return None
        shown = self.shown(parts)
        exact = lambda pal, q: sum(r in COLOURS[pal] for r in shown[q])
        one = self.mixed_palette(parts)
        best = max(sum(exact(one, q) for q in range(4)), 0)
        for n, (a, b) in enumerate(QUARTER_PAIRS):
            mask, total = 0, 0
            for q in range(4):
                if exact(b, q) > exact(a, q):
                    mask |= 1 << q
                total += max(exact(a, q), exact(b, q))
            if total > best and 0 < mask < 15:
                best = total
                self.split[key] = (0x81 | mask << 1 | n << 5, [(b if mask >> q & 1 else a) for q in range(4)])
        return self.split[key]

    def shown(self, parts):
        """For each quarter, what colour each pixel it shows is meant to be"""
        out = []
        for q in range(4):
            cqx, cqy = q % 2, q // 2
            top = {}
            hide = self.covered(parts, q)
            for k, dx, dy, tf in parts:
                oq = ((cqx * 8 - dx) // 8, (cqy * 8 - dy) // 8)
                if oq not in self.LO.mask(k, flags_of(tf)) or k in GO.INVISIBLE:
                    continue
                if hide and k in GO.GROUND_DECO:
                    continue
                px = self.quarter(1, k, tf, oq)
                if px is None or px == GROUND:
                    continue
                if k in GO.CGB_RECOLOUR:
                    px = tuple(GO.CGB_RECOLOUR[k].get(v, v) for v in px)
                for i, v in enumerate(px):
                    if v:
                        top[i] = COLOURS[self.opal[k]][v]
            out.append(list(top.values()))
        return out

    def mixed_palette(self, parts):
        """The palette that shows the cell's objects best: most of the pixels they show in their own
        colour, then in the nearest; ties (one palette in the cell) as before"""
        plain = self.plain_palette(parts)
        pals = {self.opal.get(p[0], 0) for p in parts if p[0] not in GO.INVISIBLE}
        if len(pals) < 2 or not all(p in COLOURS for p in pals):
            return plain
        key = tuple(parts)
        if key in self.mixed:
            return self.mixed[key]
        shown = [r for q in self.shown(parts) for r in q]

        def score(pal):
            have = COLOURS[pal]
            return sum(2 if r in have else 1 if any(n in have for n in NEAR.get(r, ())) else 0 for r in shown)

        best = max(COLOURS, key=lambda p: (score(p), p == plain, p in pals))
        self.mixed[key] = best
        return best

    def in_radius(self, key, tf, qx, qy):
        """Is quarter (qx, qy) of a placed saw inside its hitbox (GO.HIT_RADIUS)? Its centre is the
        wheel's: the object's middle, or the inner corner of a piece"""
        w, h = GO.OBJECTS[key][2]
        pieces = GO.SPIN.get(key, 1)
        cx, cy = (w, h) if pieces == 4 else (w / 2, h / 2)
        fl = flags_of(tf)
        if fl & GO.FLIP_D:
            cx, cy, w, h = cy, cx, h, w
        if fl & GO.FLIP_H:
            cx = w - cx
        if fl & GO.FLIP_V:
            cy = h - cy
        d = ((qx * 8 + 4 - cx) ** 2 + (qy * 8 + 4 - cy) ** 2) ** 0.5
        return d <= max(GO.HIT_RADIUS[key] + 2, 6)

    def collision(self, parts, lookup):
        hit = [p for p in parts if self.kind[p[0]] != "none"]
        if not hit:
            return 0, None
        like = []
        for key, dx, dy, tf in hit:
            if key in GO.HIT_LIKE:
                key, shift = GO.HIT_LIKE[key]
                dy += 0 if tf else shift
            like.append((key, dx, dy, tf))
        found = lookup(frozenset(like))
        if len(found) == 1:
            return self.t.col[found[0]], None
        if frozenset(like) in self.exact_col:
            return self.exact_col[frozenset(like)], None
        if any(self.kind[p[0]] == "special" for p in hit):
            # an orb's or pad's outline with solid / deadly objects in its cell: the cell collides as
            # those (the orb or pad itself is its sprite; the outline's own type does nothing)
            return self.collision([p for p in parts if self.kind[p[0]] != "special"], lookup)
        solid = deadly = 0
        for key, dx, dy, tf in hit:
            for qx, qy in self.LO.mask(key, flags_of(tf)):
                if key in GO.HIT_RADIUS and not self.in_radius(key, tf, qx, qy):
                    continue
                if key in GO.HIT_QUARTERS and (qx, qy) not in self.LO.turned(GO.HIT_QUARTERS[key], 2, 2, flags_of(tf)):
                    continue
                cx, cy = (dx + 8 * qx) // 8, (dy + 8 * qy) // 8
                if 0 <= cx < 2 and 0 <= cy < 2:
                    bit = 1 << (cx + 2 * cy)
                    if self.kind[key] == "solid":
                        solid |= bit
                    else:
                        deadly |= bit
        deadly &= ~solid
        if not solid and not deadly:
            return 0, None      # e.g. a saw's corner, outside its circle
        if solid == 15:
            if any(p[0] == BRICK and not p[1] and not p[2] for p in hit):
                return COL_BREAKABLE, ("a brick shares its cell: all of it breaks" if len(parts) > 1 else None)
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

    def painted(self, look, over):
        """A cell's DMG look (4 quarters) with the pixels {(x, y): colour} painted over it"""
        out = []
        for q in range(4):
            px = list(look[q] or (0,) * 64)
            for (x, y), v in over.items():
                if x // 8 == q % 2 and y // 8 == q // 2:
                    px[(y % 8) * 8 + x % 8] = v
            out.append(tuple(px) if any(px) else None)
        return tuple(out)

    def make(self, name, grid, novel, lookup, over=None):
        """novel: [(cx, cy, parts bottom to top)] -> fills grid, returns (metatiles {id: (cgb quarters,
        dmg quarters, collision, CGB palette, spinning objects in it)}, warnings). The level's collision and palette tables
        hold them (tools/build_bg_tiles.py), so any id the level does not use will do.
        over: [{(x, y): colour}], objects painted over the DMG look of the cells they cover (the DMG's
        objects and decorations, tools/dmg_deco.py), the most important first. A cell with paint is
        a metatile of its own (on CGB it looks and collides as before), so an object is painted
        whole while the level has metatile ids left for it, or not at all."""
        from collections import Counter
        used = {m for row in grid for m in row if m >= 0}
        cells, out, warn = {}, {}, []
        # a level without saws gives their ids to its own cells (the engine's saw handling is off there:
        # bg_level_info saws, built from the Famidash ids the level uses, build_bg_tiles.py)
        reserved = RESERVED if used & SAW_IDS else RESERVED - SAW_IDS
        free = [m for m in range(256) if m not in used and m not in reserved]
        todo = []       # the cells with fading objects (never painted)
        plain = {}      # cell -> its look without paint: (key, spin, is a metatile of the level's own)
        for cx, cy, parts in novel:
            col, w = self.collision(parts, lookup)
            if w:
                warn.append("%s cell %d, %d: %s" % (name, cx, cy, w))
            if col is None:
                col = 0
            pal = self.palette(parts)
            k = (tuple(self.spin_look(parts)), tuple(self.look(0, parts)), pal, col)
            rest = [p for p in parts if p[0] not in GO.FADE]
            spin = sorted({p[0] for p in parts if p[0] in self.spin})
            if len(rest) != len(parts):
                hidden = (tuple(self.spin_look(rest)), tuple(self.look(0, rest)),
                          self.palette(rest) if rest else pal, col)
                todo.append((cx, cy, k, hidden, spin))
                plain[(cx, cy)] = None
            else:
                plain[(cx, cy)] = (k, spin, True)
        # cells with fading objects: ids in one block, each with its faded look n ids further, so
        # the game finds them without a table (bg_stream.c fade_*)
        fading = list(dict.fromkeys(t[2] for t in todo))
        if fading:
            n = len(fading)
            block = next((f for f in free if all(f + i in free for i in range(2 * n))), None)
            if block is None:
                raise SystemExit("%s: no %d metatile ids in a row left for fading objects" % (name, 2 * n))
            for i, k in enumerate(fading):
                cells[k] = block + i
            for f in range(block, block + 2 * n):
                free.remove(f)
            for cx, cy, k, h, spin in todo:
                m = cells[k]
                out[m] = (k[0], k[1], k[3], k[2], spin, m + n)
                out[m + n] = (k[0], h[1], h[3], FADED_PAL, spin, None)
                grid[cy][cx] = m
        # The cells without paint that are the level's own: a look is an id.
        own = {c: p[0] for c, p in plain.items() if p is not None}
        own_count = Counter(own.values())
        # Too many for an id each ("wide"): the level's own cells share ids like the painted ones
        # below, any two looks with the same collision, SHARE_GAP columns apart. The id then changes
        # look on CGB as well ("cgb_at") and its CGB palette ("pal_at"); its collision stays, the game
        # reads that by id.
        wide = len(own_count) > len(free)
        # The cells with paint differ from the cell under the paint on the DMG only, so painted looks
        # of the same cell share an id where they are SHARE_GAP columns apart: the id shows one, then
        # the other (tools/bg_stream.py points its quarters at other tiles there: "dmg_at").
        painted = {}

        solo = set()    # wide: looks with an id of their own (below)

        def tracks(painted):
            """{(cgb look, palette, collision): [[(first column, last column, look), ...] of an id]}
            (not sharing: an id each look; wide: cgb look and palette None, but for the looks in solo)"""
            cols = {}
            for (cx, _), k in painted.items():
                cols.setdefault(k, set()).add(cx)
            items = {}
            for k, cs in cols.items():
                cs = sorted(cs)
                first = cs[0]
                for i, c in enumerate(cs):
                    if i + 1 == len(cs) or (share and cs[i + 1] - c >= SHARE_GAP):
                        base = ((k[0], k[2], k[3], k) if k in solo else (None, None, k[3])) if wide else (k[0], k[2], k[3])
                        items.setdefault(base, []).append((first, c, k))
                        first = cs[i + 1] if i + 1 < len(cs) else None
            out = {}
            for base, its in items.items():
                ids = out[base] = []
                for it in sorted(its, key=lambda t: t[:2]):
                    for t in ids:
                        if share and it[0] - t[-1][1] >= SHARE_GAP:
                            t.append(it)
                            break
                    else:
                        ids.append([it])
            return out

        big_saw = any(m in BG.BIG_SAW_PARTS for row in grid for m in row)
        fixed = {}

        def plain_of(c):
            if c not in plain:
                m = grid[c[1]][c[0]]
                if m in reserved - {0, 12, 24}:   # (12, 24: background and pit spikes, nothing goes by their ids)
                    plain[c] = None
                else:
                    if m not in fixed:
                        fixed[m] = (tuple(BS.quarters(self.bs, name, m, 1, big_saw)),
                                    tuple(BS.quarters(self.bs, name, m, 0, big_saw)), self.t.pal[m], self.t.col[m])
                    plain[c] = (fixed[m], [], False)
            return plain[c]

        # (ids are only shared in a level that has too few: its tiles' records then say where the ids
        # point, so a respawn has to read them all again, bg_stream.c bs_tables_ok)
        h, w = len(grid), len(grid[0])
        if wide:
            share = True
            painted, own_count = dict(own), Counter()
            n = sum(len(t) for t in tracks(painted).values())
            if n > len(free):
                raise SystemExit("%s: no metatile id left: too many different cells (%d, %d ids with sharing, %d ids)"
                                 % (name, len(set(own.values())), n, len(free)))
        start = (own_count, painted)
        if wide:
            # With ids to spare the DMG's streaming slots are what runs out: an object is painted only
            # while no 16 columns (the VRAM ring) need more different tiles than the slots hold
            # (tools/bg_stream.py allocate; the saws' tiles have slots of their own)
            saw_tiles = {BG.SAW_METATILES[m][q] for row in grid for m in row
                         if m in BG.SAW_METATILES and m not in own_count and (m != BG.SAW_CENTER_MT or big_saw)
                         for q in range(4) if BG.SAW_METATILES[m][q] is not None}
            tile_room = len([t for t in list(range(BG.DMG_BG_SLOTS)) + BG.DMG_OBJECT_SLOTS
                             if t not in BG.DMG_RESERVED_TILES]) - len(saw_tiles) - DMG_SLOT_MARGIN
            col_t = [Counter() for _ in range(w)]
            for y in range(h):
                for x in range(w):
                    p = plain_of((x, y))
                    for px in (p[0][1] if p else ()):
                        if px:
                            col_t[x][px] += 1
        dmg_full = 0
        for share in ((True,) if wide else (False, True)):
            own_count, painted = start
            paint, dropped = {}, 0
            for px in over or []:
                by_cell = {}
                for (x, y), v in px.items():
                    if 0 <= x < w * 16 and 0 <= y < h * 16:
                        by_cell.setdefault((x // 16, y // 16), {})[(x % 16, y % 16)] = v
                new, own2, painted2 = {}, Counter(own_count), dict(painted)
                delta = {}      # wide: column -> Counter of DMG tiles it gains (+) and loses (-)
                for c, o in by_cell.items():
                    p = plain_of(c)
                    if p is None:
                        continue
                    k, _, mine = p
                    merged = {**paint.get(c, {}), **o}
                    kd = (k[0], self.painted(k[1], merged), k[2], k[3])
                    new[c] = merged
                    if wide:
                        d = delta.setdefault(c[0], Counter())
                        for px in (painted2[c][1] if c in painted2 else k[1]):
                            if px:
                                d[px] -= 1
                        for px in kd[1]:
                            if px:
                                d[px] += 1
                    if c in painted2:
                        del painted2[c]
                    elif mine:
                        own2[k] -= 1
                        if not own2[k]:
                            del own2[k]
                    if kd[1] != k[1] or (wide and mine):
                        painted2[c] = kd
                    elif mine:
                        own2[k] += 1
                if len(own2) + sum(len(t) for t in tracks(painted2).values()) > len(free):
                    dropped += 1
                    continue
                if wide and delta:
                    after = {x: col_t[x] + Counter() for x in delta}
                    for x, d in delta.items():
                        after[x].update(d)
                        after[x] += Counter()     # (drops the tiles no longer there)
                    lo, hi = min(delta), max(delta)
                    if any(len(set().union(*(after.get(x, col_t[x]) for x in range(max(0, a), min(w, a + BS.RING)))))
                           > tile_room for a in range(lo - BS.RING + 1, hi + 1)):
                        dmg_full += 1
                        continue
                    for x in delta:
                        col_t[x] = after[x]
                own_count, painted = own2, painted2
                paint.update(new)
            if not dropped:
                break
        if dropped:
            warn.append("%s: no metatile ids left to paint %d of the DMG's objects and decorations" % (name, dropped))
        if dmg_full:
            warn.append("%s: %d of the DMG's objects and decorations not painted: too many tiles in 16 columns" % (name, dmg_full))
        if wide:
            # The ids left over go to the looks shown in the most places: an id of their own, so
            # the game never applies a look change for them (each one costs a stream record a
            # column, bs_apply)
            runs = Counter(it[2] for ids in tracks(painted).values() for t in ids for it in t)
            for k, r in runs.most_common():
                if r < 2:
                    break
                solo.add(k)
                if sum(len(t) for t in tracks(painted).values()) > len(free):
                    solo.discard(k)
        for c in sorted(own):
            if c in painted:
                continue
            k = own[c]
            if k not in cells:
                cells[k] = free.pop(0)
                out[cells[k]] = (k[0], k[1], k[3], k[2], plain[c][1], None)
            grid[c[1]][c[0]] = cells[k]
        spin = {}
        for c, k in painted.items():
            spin.setdefault(k, set()).update(plain[c][1])
        where = {}      # painted look -> [(first column, last column, id)]
        for base, ids in sorted(tracks(painted).items(), key=lambda kv: (min(t[0][:2] for t in kv[1]), kv[0][0] is None)):
            for t in ids:
                m = free.pop(0)
                sp = sorted(set().union(*(spin[it[2]] for it in (t if wide else (x for ts in ids for x in ts)))))
                if wide:
                    dmg_at = [[it[0], it[2][1]] for it in t] if len({it[2][1] for it in t}) > 1 else None
                    cgb_at = [[it[0], it[2][0]] for it in t] if len({it[2][0] for it in t}) > 1 else None
                    pal_at = [[it[0], it[2][2]] for it in t] if len({it[2][2] for it in t}) > 1 else None
                else:
                    dmg_at, cgb_at, pal_at = ([[it[0], it[2][1]] for it in t] if len(t) > 1 else None), None, None
                out[m] = (t[0][2][0], t[0][2][1], base[2], t[0][2][2], sp, None, dmg_at, cgb_at, pal_at)
                for first, last, k in t:
                    where.setdefault(k, []).append((first, last, m))
        for (cx, cy), k in painted.items():
            grid[cy][cx] = next(m for first, last, m in where[k] if first <= cx <= last)
        return out, warn
