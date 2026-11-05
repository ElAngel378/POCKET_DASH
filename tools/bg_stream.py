"""Background tile streaming (docs/PLAN_1.6.md, phase 1): each level's own tiles, loaded into VRAM
column by column as the level scrolls.

A level's pool is the tiles its metatiles show (per platform: CGB / DMG art, the level's Famidash
tile sets, DMG fake spikes). Each map column gets records "pool tile T into BG slot S"; the game
applies a column's records before it draws the column: it uploads T and points every metatile
quarter showing T at S. A tile keeps its slot while a column of the 16-column VRAM ring uses it,
so a slot only changes hands when the last column using its tile has left the ring.

Not streamed (handled as before): the transparent tile 12 (CGB: the parallax shows there), the
saws (CGB: VRAM bank 1; DMG: their own slots, see build_bg_tiles.py), CGB's ground look metatiles
(drawn with the ground strip tiles).
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_bg_tiles as BG

RING = 16              # map columns in the VRAM ring (mt_renderer.c)
TRANSPARENT = 12       # sheet tile: empty (CGB: the parallax)
BLACK_FILL_SLOT = 26
CGB_GROUND_LOOK = {1, 2, 5, 6, 136, 137}   # mt_renderer.c gnd_look_tab


def flipped(px):
    return tuple(v for y in range(8) for v in reversed(px[y * 8:y * 8 + 8]))


class Art:
    """The tile pixels of bg_tiles.png, by section"""
    def __init__(self):
        tiles = BG.read_png()
        self.sec = {n: [tuple(v for r in t for v in r) for t in tiles[row * 16: row * 16 + c]]
                    for n, row, c in BG.SECTIONS}
        import re
        src = (ROOT / "include" / "famidash_metatiles_dmg.c").read_text()
        a = src.index("const uint8_t metatiles[FAMIDASH_NUM_METATILES][4]")
        self.mt = [list(map(int, m)) for m in re.findall(r'\{ (\d+), (\d+), (\d+), (\d+) \}', src[a:src.index("};", a)])]


def level_sets(name):
    s = BG.LEVEL_SETS.get(name, {})
    return s.get("blocks"), s.get("spikes")


def quarters(art, name, m, cgb, big_saw, sets=None):
    """The 4 quarters of metatile m in this level: None (not streamed: transparent, saw, ground
    look) or the tile's pixels. sets: (blocks, spikes) instead of the level's"""
    blocks, spikes = sets if sets is not None else level_sets(name)
    out = []
    for q in range(4):
        saw = BG.SAW_METATILES.get(m, (None,) * 4)[q]
        if m == BG.SAW_CENTER_MT and not big_saw:
            saw = None
        if saw is not None or (cgb and m in CGB_GROUND_LOOK):
            out.append(None)
            continue
        if spikes == "B" and any(o[:2] == (m, q) for o in BG.SPIKES_B_OVERRIDES):
            out.append(art.sec["SPIKES_B_CGB"][[o for o in BG.SPIKES_B_OVERRIDES if o[:2] == (m, q)][0][2]])
            continue
        t = art.mt[m][q]
        if t == TRANSPARENT:
            out.append(None)
            continue
        if blocks == "B" and t in BG.BLOCKS_B_SLOTS:
            px = art.sec["BLOCKS_B_CGB" if cgb else "BLOCKS_B_DMG"][BG.BLOCKS_B_SLOTS.index(t)]
        elif blocks == "B" and t in BG.BLOCKS_B2_SLOTS:
            px = art.sec["BLOCKS_B2_CGB" if cgb else "BLOCKS_B2_DMG"][BG.BLOCKS_B2_SLOTS.index(t)]
        elif spikes == "B" and t in BG.SPIKES_B_SLOTS:
            px = art.sec["SPIKES_B_CGB"][BG.SPIKES_B_SLOTS.index(t)]
        elif spikes == "C" and t in BG.SPIKES_C_SLOTS:
            px = art.sec["SPIKES_C"][BG.SPIKES_C_SLOTS.index(t)]
        elif cgb and t == BLACK_FILL_SLOT:
            px = art.sec["BLACK_FILL"][0]
        else:
            px = art.sec["BASE"][t]
        if not cgb and m in BG.FAKE_SPIKE_MTS:
            # DMG fake spikes: colour 3 (the white outline) drawn as 2 (CGB: palette 5)
            px = tuple(2 if v == 3 else v for v in px)
        out.append(px)
    return out


class Stream:
    """One level on one platform: pool, slots, records"""
    def __init__(self, art, name, grid, cgb, slots, novel=None):
        """novel: the level's own metatiles (tools/compose.py): id -> {"cgb": 4 quarters, "dmg": ...}"""
        self.name, self.cgb = name, cgb
        big_saw = any(m in BG.BIG_SAW_PARTS for col in grid for m in col)
        used = sorted({m for col in grid for m in col})
        novel = novel or {}
        self.mq = {m: [tuple(q) if q else None for q in novel[m]["cgb" if cgb else "dmg"]] if m in novel
                   else quarters(art, name, m, cgb, big_saw) for m in used}
        pool, index = [], {}
        self.uses = {}          # pool tile -> [(metatile, quarter)]
        for m in used:
            for q, px in enumerate(self.mq[m]):
                if px is None:
                    continue
                if px not in index:
                    index[px] = len(pool)
                    pool.append(px)
                self.uses.setdefault(index[px], []).append((m, q))
        self.pool, self.index = pool, index
        self.col_tiles = [sorted({index[px] for m in set(col) for px in self.mq[m] if px is not None})
                          for col in grid]
        self.slots = list(slots)
        self.allocate()

    def allocate(self):
        """records[c]: [(pool tile, slot)] to apply before column c is drawn. A tile that left the
        ring stays in its slot until the slot is needed (the oldest one goes first): if it comes
        back before that, it is still there and needs no record."""
        free = list(self.slots)     # not in use by the ring: empty ones first, then oldest freed
        holder, slot_of, count = {}, {}, {}
        self.records, self.peak = [], 0
        cols = self.col_tiles
        for c, tiles in enumerate(cols):
            # column c - RING leaves the ring as column c comes in
            if c >= RING:
                for t in cols[c - RING]:
                    count[t] -= 1
                    if not count[t]:
                        del count[t]
                        free.append(slot_of[t])
            rec = []
            for t in tiles:
                if t not in count:
                    if t in slot_of:
                        free.remove(slot_of[t])       # still there
                    else:
                        if not free:
                            raise SystemExit("%s (%s): more than %d tiles in %d columns at column %d"
                                             % (self.name, "CGB" if self.cgb else "DMG", len(self.slots), RING, c))
                        s = free.pop(0)
                        if s in holder:
                            del slot_of[holder[s]]
                        holder[s] = t
                        slot_of[t] = s
                        rec.append((t, s))
                count[t] = count.get(t, 0) + 1
            self.peak = max(self.peak, len(count))
            self.records.append(rec)
        self.hoist()

    def hoist(self):
        """Moves every record as early as its slot allows: right after the slot's previous tile
        was last needed (its last column out of the ring), at the earliest column 0. A level whose
        tiles all fit gets them all at column 0, loaded before it starts; the others stream early
        and spread out (a column brings fewer tiles at once)."""
        by_slot = {}
        for c, rec in enumerate(self.records):
            for t, s in rec:
                by_slot.setdefault(s, []).append((c, t))
        uses = {}                       # tile -> columns using it
        for c, tiles in enumerate(self.col_tiles):
            for t in tiles:
                uses.setdefault(t, []).append(c)
        moved = [[] for _ in self.records]
        slot_prev = {}      # slot -> (original column, tile, column moved to) of its last record
        tile_prev = {}      # tile -> column its last record moved to (a tile's records keep their order)
        for c, rec in enumerate(self.records):
            for t, s in rec:
                e = 0
                if s in slot_prev:
                    pc, pt, pe = slot_prev[s]
                    last = max([u for u in uses[pt] if pc <= u < c], default=pc)
                    e = max(pe + 1, last + RING)
                if t in tile_prev:
                    e = max(e, tile_prev[t] + 1)
                e = min(e, c)
                moved[e].append((t, s))
                slot_prev[s] = (c, t, e)
                tile_prev[t] = e
        self.records = moved

    def simulate(self, grid):
        """Applies the records like the game and checks every metatile of the ring each column"""
        vram, mt = {}, {}
        for c, col in enumerate(grid):
            for t, s in self.records[c]:
                vram[s] = self.pool[t]
                for m, q in self.uses[t]:
                    mt[(m, q)] = s
            for k in range(max(0, c - RING + 1), c + 1):
                for m in set(grid[k]):
                    for q, px in enumerate(self.mq[m]):
                        if px is not None and vram.get(mt.get((m, q))) != px:
                            raise SystemExit("%s: column %d (ring at %d), metatile %d quarter %d shows the wrong tile"
                                             % (self.name, k, c, m, q))


def tile_bytes(px):
    out = bytearray()
    for y in range(8):
        lo = hi = 0
        for v in px[y * 8:y * 8 + 8]:
            lo = (lo << 1) | (v & 1)
            hi = (hi << 1) | (v >> 1)
        out += bytes([lo, hi])
    return bytes(out)


def record_bytes(st):
    """Pool size, flags (1: every tile has one record, so a reload of the same level can skip the
    metatile table), then the columns with records, in order: column (16 bits), n, n x (slot,
    pool tile, k, k x (metatile, quarter)); then 0xFFFF"""
    tiles = [t for rec in st.records for t, _ in rec]
    out = bytearray([len(st.pool), 1 if len(tiles) == len(set(tiles)) else 0])
    for c, rec in enumerate(st.records):
        if not rec:
            continue
        out += bytes([c & 0xFF, c >> 8, len(rec)])
        for t, s in rec:
            uses = st.uses[t]
            assert len(uses) < 256, "%s: tile %d in %d metatile quarters" % (st.name, t, len(uses))
            out += bytes([s, t, len(uses)])
            for m, q in uses:
                out += bytes([m, q])
    return bytes(out + bytes([255, 255]))


def c_array(name, data):
    lines = ["const uint8_t %s[] = {" % name]
    for i in range(0, max(len(data), 1), 16):
        lines.append("    " + ", ".join(str(b) for b in (data[i:i + 16] or b"\0")) + ",")
    return lines + ["};"]


def test_slots(slots):
    """BG_STREAM_SLOTS=n (environment): only the first n slots, so that every level streams while it
    plays (tests the streaming; the release build leaves it unset)"""
    import os
    n = os.environ.get("BG_STREAM_SLOTS")
    return slots[:int(n)] if n else slots


def write(pools):
    """pools: (level name, metatile columns, CGB slots, DMG slots) in game_levels order.
    Writes src/graphics/bg_stream_<level>.c (data, packed into free banks from the top) and
    src/graphics/bg_stream_tables.h."""
    import re
    art = Art()
    gfx = ROOT / "src" / "graphics"
    files = {}
    for name, cols, cgb_slots, dmg_slots, novel in pools:
        cols = [list(c) for c in cols]
        L = ["/* Generated by tools/bg_stream.py - do not edit */", "#pragma bank %d", "#include <gbdk/platform.h>",
             "#include <stdint.h>", "", "BANKREF(bs_%s)" % name, ""]
        size = 0
        for cgb, slots in ((1, cgb_slots), (0, dmg_slots)):
            st = Stream(art, name, cols, cgb, test_slots(slots), novel)
            st.simulate(cols)
            assert len(st.pool) < 256, "%s: %d tiles" % (name, len(st.pool))
            # DMG: all the tiles, then all of them mirrored (runs of tiles copy in one go)
            tiles = b"".join(tile_bytes(px) for px in st.pool)
            if not cgb:
                tiles += b"".join(tile_bytes(flipped(px)) for px in st.pool)
            recs = record_bytes(st)
            tag = "cgb" if cgb else "dmg"
            L += c_array("bs_%s_%s_tiles" % (name, tag), tiles) + c_array("bs_%s_%s_recs" % (name, tag), recs)
            size += max(len(tiles), 1) + len(recs)
        files[name] = (L, size)

    # banks: top down, skipping every bank other files use
    for old in gfx.glob("bg_stream_*.c"):
        if old.stem[len("bg_stream_"):] not in files:
            old.unlink()
    used = set()
    for f in (ROOT / "src").rglob("*.c"):
        if f.parent == gfx and f.name.startswith("bg_stream_"):
            continue
        m = re.search(r'^#pragma\s+bank\s+(\d+)', f.read_text(errors="ignore"), re.M)
        if m:
            used.add(int(m.group(1)))
    bank, room = None, 0
    bank_of = {}
    for name, (L, size) in files.items():
        if size > room:
            bank = max(b for b in range(2, 255) if b not in used)
            used.add(bank)
            room = 16384
        room -= size
        L[1] = L[1] % bank
        bank_of[name] = bank
        (gfx / ("bg_stream_%s.c" % name)).write_text("\n".join(L) + "\n", encoding="utf-8")

    H = ["/* Generated by tools/bg_stream.py - do not edit */", "#ifndef BG_STREAM_TABLES_H",
         "#define BG_STREAM_TABLES_H", "", "#include <gbdk/platform.h>", "",
         "#define BS_LEVEL_COUNT %d" % len(pools)]
    for name, _, _, _, _ in pools:
        H += ["BANKREF_EXTERN(bs_%s)" % name] + ["extern const uint8_t bs_%s_%s_%s[];" % (name, t, k)
                                                 for t in ("cgb", "dmg") for k in ("tiles", "recs")]
    H += ["// per level: the CGB data, then the DMG data (DMG: the tiles, then the same tiles mirrored)",
          "static const uint8_t * const bs_tiles[BS_LEVEL_COUNT * 2] = {"]
    H += ["    bs_%s_cgb_tiles, bs_%s_dmg_tiles," % (n, n) for n, _, _, _, _ in pools] + ["};"]
    H += ["static const uint8_t * const bs_recs[BS_LEVEL_COUNT * 2] = {"]
    H += ["    bs_%s_cgb_recs, bs_%s_dmg_recs," % (n, n) for n, _, _, _, _ in pools] + ["};"]
    H += ["static const uint8_t bs_banks[BS_LEVEL_COUNT] = {"]
    H += ["    %d,   // %s" % (bank_of[n], n) for n, _, _, _, _ in pools] + ["};", "", "#endif", ""]
    (gfx / "bg_stream_tables.h").write_text("\n".join(H), encoding="utf-8")


def main():
    sys.path.insert(0, str(ROOT / "tools"))
    import level_objects as LO
    art = Art()
    names = [n.lower() for n in sys.argv[1:]]
    for p in LO.level_paths(names):
        grid = LO.compile_tmx(p)
        cols = [[grid[y][x] for y in range(len(grid))] for x in range(len(grid[0]))]
        out = []
        for cgb, slots in ((1, [s for s in range(160) if s not in (12, 26)]),
                           (0, [s for s in range(144) if s not in (12, 26)])):
            st = Stream(art, p.stem.lower(), cols, cgb, test_slots(slots))
            st.simulate(cols)
            play = st.records[RING:]
            out.append("%s pool %3d peak %3d, in play: %4d uploads (max %2d/col)"
                       % ("CGB" if cgb else "DMG", len(st.pool), st.peak, sum(len(r) for r in play),
                          max([len(r) for r in play] or [0])))
        print("%-22s %s" % (p.stem, "   ".join(out)))


if __name__ == "__main__":
    main()
