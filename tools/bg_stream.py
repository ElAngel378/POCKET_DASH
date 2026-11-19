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
SPIN_BASE = 28         # the spinning objects' tiles: BG slots from here (CGB, bg_stream.c)
SPIN_MAX = 52          # ... at most: what VBlank can carry (13 tiles a VBlank, a step every 4+)
SPIN_FRAMES = 3
MAIN_PART = 8192       # a level with more records than a bank: each platform's first bytes in the shared bank
PER_COLUMN = {1: 4, 0: 4}     # records a column may take in play: CGB, DMG (bg_stream_feed: 4 and 2 tiles a
                              # frame; a column takes 3 frames at the top speed. DMG 2 left runs of
                              # heavy columns at their own column: 25 tiles at once, the game waited)
BLACK_FILL_SLOT = 26
CGB_GROUND_LOOK = {1, 2, 5, 6, 136, 137}   # mt_renderer.c gnd_look_tab
# CGB VRAM bank 1 tiles 128..159 (no sprite or fixed tile uses them in play): more slots for a part
# of a level that needs more tiles than bank 0 has. Internally 256 + tile (unique slot ids)
BANK1_SLOTS = [256 + t for t in range(128, 160)]
# ...and the saws' tiles 64..115 in a level without saws (build_bg_tiles.py loads none there)
BANK1_SAW_SLOTS = [256 + t for t in range(64, 116)]
BANK1_PAL = 0x08       # the attribute's VRAM bank bit, in the palette byte of a look there


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


def quarters(art, name, m, cgb, big_saw, sets=None):
    """The 4 quarters of metatile m in this level: None (not streamed: transparent, saw, ground
    look) or the tile's pixels. sets: the Famidash variant art (blocks, spikes)"""
    blocks, spikes = sets if sets is not None else (None, None)
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
        if spikes != "B" and any(o[:2] == (m, q) for o in BG.SPIKES_A_OVERRIDES):
            out.append(art.sec["SPIKES_A_EXTRA"][[o for o in BG.SPIKES_A_OVERRIDES if o[:2] == (m, q)][0][2]])
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
            # DMG fake spikes (CGB: palette 5): hollow, a checkered outline of colours 3 and 2: dark on the
            # normal theme, on a black background white and black dots, weaker than the real spikes' white
            px = tuple((2 if (i % 8 + i // 8) & 1 else 3) if v == 3 else 0 for i, v in enumerate(px))
        out.append(px)
    return out


class Stream:
    """One level on one platform: pool, slots, records"""
    def __init__(self, art, name, grid, cgb, slots, novel=None, spin_max=SPIN_MAX, bank1=BANK1_SLOTS):
        """novel: the level's own metatiles (tools/compose.py): id -> {"cgb": 4 quarters, "dmg": ...}"""
        self.name, self.cgb = name, cgb
        self.b1slots = list(bank1)
        self.own = sorted(novel or {})
        big_saw = any(m in BG.BIG_SAW_PARTS and m not in (novel or {}) for col in grid for m in col)
        novel = novel or {}
        # An id with "dmg_at" [[first column, quarters], ...] (tools/compose.py: the cells with the
        # DMG's painted objects) looks different from each of those columns on. Here it is one
        # metatile (id, n) per look; its quarters are pointed at their tiles where a column shows it
        # (self.repoint), not by the tiles' records.
        # CGB: "cgb_at" the same way (a level with more cells than ids: they share ids, compose.py "wide")
        tag = "cgb_at" if cgb else "dmg_at"
        self.at = {m: novel[m][tag] for m in novel if novel[m].get(tag)}

        def shown(m, x):
            if m not in self.at:
                return m
            return (m, max(i for i, (first, _) in enumerate(self.at[m]) if first <= x or not i))
        real = grid
        grid = self.grid = [[shown(m, x) for m in col] for x, col in enumerate(real)]
        used = sorted({m for col in grid for m in col}, key=lambda v: v if isinstance(v, tuple) else (v, -1))
        def quarter_of(q):
            if not q:
                return None
            return tuple(tuple(f) for f in q) if len(q) == SPIN_FRAMES else tuple(q)
        self.mq = {m: [quarter_of(q) for q in self.at[m[0]][m[1]][1]] if isinstance(m, tuple)
                   else [quarter_of(q) for q in novel[m]["cgb" if cgb else "dmg"]] if m in novel
                   else quarters(art, name, m, cgb, big_saw) for m in used}
        # The tiles that change as objects spin keep a slot for the whole level. An object spins
        # whole or not at all (a wheel half turning looks broken): they are taken from the left of
        # the level while they fit, the others stand still
        moving, tiles_of = [], {}
        for m in dict.fromkeys(m for col in real for m in col):
            if m in novel and cgb:
                # (an id with looks by column: the tiles of all of them)
                looks = [v for v in used if isinstance(v, tuple) and v[0] == m] if m in self.at else [m]
                for key in novel[m].get("spin", ()):
                    for px in (px for v in looks for px in self.mq[v]):
                        if px is not None and len(px) == SPIN_FRAMES:
                            tiles_of.setdefault(key, {})[px] = 1
        for key, tiles in tiles_of.items():
            new = [px for px in tiles if px not in moving]
            if len(moving) + len(new) <= spin_max:
                moving += new
            else:
                print("  %s: no room for %s to spin (%d tiles, %d of %d used)" % (name, key, len(new), len(moving), spin_max))
        for m in used:
            self.mq[m] = [px[0] if px is not None and len(px) == SPIN_FRAMES and px not in moving else px
                          for px in self.mq[m]]
        pinned = list(range(SPIN_BASE, SPIN_BASE + len(moving)))
        self.slots = [s for s in slots if s not in pinned]
        # CGB: when a part of the level needs more tiles than the bank 0 slots, some looks go to
        # VRAM bank 1 (BANK1_SLOTS), whole: the bank is the cell's attribute, from its palette byte
        # (+ BANK1_PAL; mt_renderer.c bm_info). Only plain cells of the level's own: no palette per
        # quarter, no saws, spinning tiles, fading or ground looks
        # A part whose bank 0 slots free just in time brings more tiles in a column than the game
        # uploads in a frame (PER_COLUMN, item 6 of 2026-11-18): then more looks go to bank 1 (fewer
        # bank 0 tiles there, so they load earlier): bank 0 counted 4, 8, ... slots smaller
        best = None
        for margin in range(0, 41, 4):
            self.b1 = set()
            if cgb and novel:
                self.b1 = self.bank1_looks(grid, used, novel, moving, margin)
            if best is not None and self.b1 == best[0]:
                continue
            try:
                self.build_pool(used, moving)
            except SystemExit:
                if best is None:
                    raise
                continue
            if best is None or len(self.over) < len(best[1]):
                best = (self.b1, self.over)
            if not self.over or not (cgb and novel):
                break
        if self.b1 != best[0]:
            self.b1 = best[0]
            self.build_pool(used, moving)
        if self.b1:
            print("  %s (CGB): %d looks in VRAM bank 1" % (name, len(self.b1)))
        for x in self.over:
            print("  %s (%s): column %d brings more than %d tiles" % (name, "CGB" if cgb else "DMG", x, PER_COLUMN[cgb]))
        index, key = self.index, self.key
        self.records[0][:0] = list(zip(self.spin, pinned))
        # repoint[c]: [(slot, id, quarter)] after column c's records: the quarters of the looks column
        # c shows that are not at their tiles yet (another look of the id was shown, or the tile
        # came back in another slot)
        # CGB: an id with "pal_at" [[first column, palette], ...] changes palette (bm_info) where
        # a column shows another of its looks: pal_rec[c] = [(id, palette)]
        self.pal_rec = [[] for _ in grid]
        pal_at = {m: novel[m]["pal_at"] for m in novel if cgb and novel[m].get("pal_at")}
        b1_ids = {v[0] for v in self.b1 if isinstance(v, tuple)}
        changing = set(pal_at) | b1_ids

        def pal_of(m, c):
            v = novel[m]["pal"]
            if m in pal_at:
                v = pal_at[m][max(i for i, (first, _) in enumerate(pal_at[m]) if first <= c or not i)][1]
            return v | (BANK1_PAL if shown(m, c) in self.b1 else 0)
        # the ids' palettes at the start (the level's palette table, build_bg_tiles.py)
        self.pal_start = {m: novel[m]["pal"] | (BANK1_PAL if shown(m, 0) in self.b1 else 0)
                          for m in novel} if cgb else {}
        pal_now = {m: self.pal_start[m] for m in changing}
        for c, col in enumerate(real if changing else []):
            for m in sorted(set(col) & changing):
                v = pal_of(m, c)
                if pal_now[m] != v:
                    pal_now[m] = v
                    self.pal_rec[c].append((m, v))
        self.repoint = [[] for _ in grid]
        slot_of, at = {}, {}
        for c, col in enumerate(grid if self.at else []):
            for t, sl in self.records[c]:
                slot_of[t] = sl
            for v in sorted(v for v in set(col) if isinstance(v, tuple)):
                for q, px in enumerate(self.mq[v]):
                    want = TRANSPARENT if px is None else slot_of[index[key(v, px)]]
                    if at.get((v[0], q)) != want:
                        at[(v[0], q)] = want
                        self.repoint[c].append((want, v[0], q))

    def build_pool(self, used, moving):
        """The pool of tiles (a look in bank 1: tiles of their own), the columns' tiles, the records"""
        b1 = self.b1

        def key(m, px):
            return (px, 1) if m in b1 else px
        pool, index, self.pbank = [], {}, []
        self.uses = {}          # pool tile -> [(metatile, quarter)]
        for m in used:
            for q, px in enumerate(self.mq[m]):
                if px is None:
                    continue
                k = key(m, px)
                if k not in index:
                    index[k] = len(pool)
                    pool.append(px)
                    self.pbank.append(1 if m in b1 else 0)
                self.uses.setdefault(index[k], [])
                if not isinstance(m, tuple):
                    self.uses[index[k]].append((m, q))
        self.pool, self.index, self.key = pool, index, key
        self.spin = [index[px] for px in moving]
        self.col_tiles = [sorted({index[key(m, px)] for m in set(col) for px in self.mq[m]
                                  if px is not None and len(px) != SPIN_FRAMES}) for col in self.grid]
        self.allocate()

    def bank1_looks(self, grid, used, novel, moving, margin=0):
        """The looks to put in VRAM bank 1: none while the bank 0 slots hold every part of the
        level; else, the part needing the most first, the look there that frees the most bank 0
        tiles, until every part fits (or nothing more can go)"""
        S0, S1 = len(self.slots) - margin, len(self.b1slots)
        hides = {m: novel[m]["hide"] for m in novel if novel[m].get("hide") is not None}
        fades = set(hides) | set(hides.values())

        def ok(v):
            m = v[0] if isinstance(v, tuple) else v
            if m not in novel or m in fades or m in CGB_GROUND_LOOK or m in BG.SAW_METATILES or m == BG.SAW_CENTER_MT:
                return False
            if novel[m]["pal"] >= 8 or any(p >= 8 for _, p in novel[m].get("pal_at") or []):
                return False
            pxs = [px for px in self.mq[v] if px is not None]
            return bool(pxs) and not any(len(px) == SPIN_FRAMES or px in moving for px in pxs)
        good = {v for v in used if ok(v)}
        b1 = set()
        tiles = {v: {px for px in self.mq[v] if px is not None and len(px) != SPIN_FRAMES} for v in used}

        def demand(bank):
            out, count = [], {}
            for c, col in enumerate(grid):
                if c >= RING:
                    for k in self._keys(grid[c - RING], tiles, b1, bank):
                        count[k] -= 1
                        if not count[k]:
                            del count[k]
                for k in self._keys(col, tiles, b1, bank):
                    count[k] = count.get(k, 0) + 1
                out.append(len(count))
            return out
        for _ in range(2000):
            d0 = demand(0)
            worst = max(range(len(d0)), key=d0.__getitem__)
            if d0[worst] <= S0:
                break
            window = {v for k in range(max(0, worst - RING + 1), worst + 1) for v in grid[k]}
            users = {}
            for v in window - b1:
                for px in tiles[v]:
                    users[px] = users.get(px, 0) + 1
            best = max(((window & good) - b1), default=None,
                       key=lambda v: (sum(1 for px in tiles[v] if users[px] == 1), -len(tiles[v]), str(v)))
            if best is None or not any(users[px] == 1 for px in tiles[best]):
                break
            b1.add(best)
            if max(demand(1)) > S1:
                b1.discard(best)
                good.discard(best)
        return b1

    @staticmethod
    def _keys(col, tiles, b1, bank):
        return {px for v in set(col) for px in tiles[v] if (v in b1) == bool(bank)}

    def allocate(self):
        """records[c]: [(pool tile, slot)] to apply before column c is drawn. A tile that left the
        ring stays in its slot until the slot is needed (the oldest one goes first): if it comes
        back before that, it is still there and needs no record."""
        # not in use by the ring: empty ones first, then oldest freed; per VRAM bank
        free = {0: list(self.slots), 1: list(self.b1slots)}
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
                        free[self.pbank[t]].append(slot_of[t])
            rec = []
            for t in tiles:
                if t not in count:
                    b = self.pbank[t]
                    if t in slot_of:
                        free[b].remove(slot_of[t])       # still there
                    else:
                        if not free[b]:
                            raise SystemExit("%s (%s): more than %d tiles in %d columns at column %d%s"
                                             % (self.name, "CGB" if self.cgb else "DMG",
                                                len(self.slots) if not b else len(self.b1slots), RING, c,
                                                " (VRAM bank 1)" if b else ""))
                        s = free[b].pop(0)
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
        """Moves records earlier, as far as their slots allow: a slot can take its next tile once
        its previous tile's last column left the ring. A level whose tiles all fit gets them all at
        column 0, loaded before it starts. In play a column takes at most PER_COLUMN records (the
        upload queue must never fill: the game would stop and wait for it), the ones needed soonest
        first; only a record at its own column goes over that (warned about)."""
        uses = {}                       # tile -> columns using it
        for c, tiles in enumerate(self.col_tiles):
            for t in tiles:
                uses.setdefault(t, []).append(c)
        recs = []                       # [column needed, tile, slot, previous record of the slot,
        slot_prev, tile_prev = {}, {}   #  of the tile, earliest column]
        for c, rec in enumerate(self.records):
            for t, s in rec:
                ps, pt = slot_prev.get(s), tile_prev.get(t)
                e = 0
                if ps is not None:
                    pc, pk = recs[ps][0], recs[ps][1]
                    e = max([u for u in uses[pk] if pc <= u < c], default=pc) + RING
                slot_prev[s] = tile_prev[t] = len(recs)
                recs.append((c, t, s, ps, pt, min(e, c)))
        moved = [[] for _ in self.records]
        self.over = []                  # columns bringing more than the budget
        placed, todo = {}, list(range(len(recs)))
        budget = PER_COLUMN[self.cgb]
        for x in range(len(moved)):
            ready = []
            for i in todo:
                c, t, s, ps, pt, e = recs[i]
                if recs[i][0] < x:
                    raise SystemExit("%s: record of column %d left behind" % (self.name, c))
                if e <= x and (ps is None or ps in placed) and (pt is None or pt in placed):
                    ready.append(i)
            # each record's predecessors go first (same slot, same tile: their columns are earlier)
            for n, i in enumerate(sorted(ready, key=lambda i: recs[i][0])):
                if x and n >= budget and recs[i][0] > x:
                    break
                if x and n >= budget and x not in self.over:
                    self.over.append(x)
                moved[x].append((recs[i][1], recs[i][2]))
                placed[i] = x
            todo = [i for i in todo if i not in placed]
        self.records = moved

    def simulate(self, grid=None):
        """Applies the records like the game and checks every metatile of the ring each column"""
        vram, mt = {TRANSPARENT: None}, {}
        grid = self.grid
        for c, col in enumerate(grid):
            for t, s in self.records[c]:
                vram[s] = self.pool[t]
                for m, q in self.uses[t]:
                    mt[(m, q)] = s
            for s, m, q in self.repoint[c]:
                mt[(m, q)] = s
            for k in range(max(0, c - RING + 1), c + 1):
                for v in set(grid[k]):
                    m = v[0] if isinstance(v, tuple) else v
                    for q, px in enumerate(self.mq[v]):
                        if (px is not None or isinstance(v, tuple)) and vram.get(mt.get((m, q)), 0) != px:
                            raise SystemExit("%s: column %d (ring at %d), metatile %s quarter %d shows the wrong tile"
                                             % (self.name, k, c, v, q))


def tile_bytes(px):
    out = bytearray()
    for y in range(8):
        lo = hi = 0
        for v in px[y * 8:y * 8 + 8]:
            lo = (lo << 1) | (v & 1)
            hi = (hi << 1) | (v >> 1)
        out += bytes([lo, hi])
    return bytes(out)


def record_bytes(st, starts=None, start_cols=None):
    """Pool size (16 bits), flags (1: every tile has one record, so a reload of the same level can
    skip the metatile table), then the columns with records, in order: column (16 bits), n, n x
    (slot, pool tile (16 bits; 0xFFFF: no tile to load), k, k x (metatile, quarter)); then 0xFFFF.
    A record with tile 0xFEFF sets palettes (CGB): k x (metatile, bm_info value).
    starts: a list to get where each column's records begin"""
    tiles = [t for rec in st.records for t, _ in rec]
    n = len(st.pool)
    assert n < 0x7E00, "%s: %d tiles" % (st.name, n)
    out = bytearray([n & 0xFF, n >> 8, 1 if len(tiles) == len(set(tiles)) and not any(st.repoint)
                     and not any(st.pal_rec) else 0])
    # the empty quarters of the level's own metatiles show the transparent tile: records of no tile
    empty = [(m, q) for m in st.own if m in st.mq for q, px in enumerate(st.mq[m]) if px is None]
    groups = [empty[i:i + 255] for i in range(0, len(empty), 255)]
    for c, rec in enumerate(st.records):
        points = {}
        for sl, m, q in st.repoint[c]:
            points.setdefault(sl, []).append((m, q))
        pals = st.pal_rec[c]
        if not rec and not points and not pals and not (c == 0 and groups):
            continue
        n = len(rec) + len(points) + (len(groups) if c == 0 else 0) + (1 if pals else 0)
        assert n < 256, "%s: %d records in column %d" % (st.name, n, c)
        if starts is not None:
            starts.append(len(out))
        if start_cols is not None:
            start_cols.append(c)
        out += bytes([c & 0xFF, c >> 8, n])
        if c == 0:
            for g in groups:
                out += bytes([TRANSPARENT, 0xFF, 0xFF, len(g)]) + bytes(v for mq in g for v in mq)
        for t, s in rec:
            uses = st.uses[t]
            assert len(uses) < 256, "%s: tile %d in %d metatile quarters" % (st.name, t, len(uses))
            # (a VRAM bank 1 slot: the tile's top bit, bs_apply / bg_stream_to)
            out += bytes([s & 0xFF, t & 0xFF, (t >> 8) | (0x80 if st.pbank[t] else 0), len(uses)])
            for m, q in uses:
                out += bytes([m, q])
        for sl, g in points.items():
            out += bytes([sl & 0xFF, 0xFF, 0xFF, len(g)]) + bytes(v for mq in g for v in mq)
        if pals:
            out += bytes([0, 0xFF, 0xFE, len(pals)]) + bytes(v for mv in pals for v in mv)
    return bytes(out + bytes([255, 255]))


SKIP_EVERY = 128       # columns between the stream states saved for a reload (snapshot_bytes)
# a saved state (bg_stream.c): its column, the next column with records and where they are (bank),
# the state's records (bank)
BS_SKIP_TYPE = ("typedef struct { uint16_t k, col; const uint8_t *rp, *sp; uint8_t rbank, sbank; } BsSkip;")


def snapshot_bytes(st, every=SKIP_EVERY):
    """[(column K, bytes)]: the stream's state after the records of columns 0..K (every K a
    multiple of `every`), as records: per slot, its tile and every metatile quarter pointing at it
    (more than 255: more records of the slot, no tile), the transparent tile's quarters, the
    palettes of the ids whose palette changes. Blocks of at most 255 records (n, records), then 0.
    A reload with the display off (a practice checkpoint: bg_stream.c bs_skip) applies the state
    nearest before the column it needs instead of every record from column 0."""
    vram, mt, pal = {}, {}, {}
    changing = {m for rec in st.pal_rec for m, _ in rec}
    empty = [(m, q) for m in st.own if m in st.mq for q, px in enumerate(st.mq[m]) if px is None]
    out = []
    for c, rec in enumerate(st.records):
        if c == 0:
            for mq in empty:
                mt[mq] = TRANSPARENT
        for t, sl in rec:
            vram[sl] = t
            for mq in st.uses[t]:
                mt[mq] = sl
        for sl, m, q in st.repoint[c]:
            mt[(m, q)] = sl
        for m, v in st.pal_rec[c]:
            pal[m] = v
        if c and c % every == 0 and c < len(st.records) - RING:
            by_slot = {}
            for mq, sl in mt.items():
                by_slot.setdefault(sl, []).append(mq)
            recs = []
            for sl in sorted(set(vram) | set(by_slot)):
                mqs = sorted(by_slot.get(sl, []))
                t = vram.get(sl)
                for i in range(0, max(len(mqs), 1), 255):
                    g = mqs[i:i + 255]
                    if i == 0 and t is not None:
                        hi = (t >> 8) | (0x80 if st.pbank[t] else 0)
                        b = bytes([sl & 0xFF, t & 0xFF, hi, len(g)])
                    else:
                        if not g:
                            continue
                        b = bytes([sl & 0xFF, 0xFF, 0xFF, len(g)])
                    recs.append(b + bytes(v for mq in g for v in mq))
            if changing:
                vals = sorted((m, pal.get(m, st.pal_start[m])) for m in changing)
                for i in range(0, len(vals), 255):
                    g = vals[i:i + 255]
                    recs.append(bytes([0, 0xFF, 0xFE, len(g)]) + bytes(v for mv in g for v in mv))
            data = bytearray()
            for i in range(0, len(recs), 255):
                blk = recs[i:i + 255]
                data += bytes([len(blk)]) + b"".join(blk)
            out.append((c, bytes(data + bytes([0]))))
    return out


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
    Writes src/graphics/bg_stream_<level>.c (records, packed into free banks from the top),
    bg_stream_tiles_<n>.c (every level's tiles, one space over consecutive banks: a level can have
    any number of them) and bg_stream_tables.h. Returns {level: {id: palette byte}} of the ids that
    start in VRAM bank 1 (their palette table entries, build_bg_tiles.py)."""
    import re
    art = Art()
    gfx = ROOT / "src" / "graphics"
    files = {}
    space = bytearray()    # the tile space
    first = {}             # (level, platform) -> its first tile in the space
    spin = {}              # level -> (its spinning tiles' first tile in the space, their number)
    bank1 = {}             # level -> {id: its palette byte at the start} of the ids in VRAM bank 1 there
    snaps = {}             # (level, platform) -> (snapshot_bytes, record starts, their columns)
    piece_at = {}          # (level, platform) -> each record piece's first byte in the records
    for name, cols, cgb_slots, dmg_slots, novel, bank1_slots in pools:
        parts, arrays = {}, []
        # (a cell's hidden look is shown in the column of the cell itself: its tiles are needed there)
        cols = [list(c) + [novel[m]["hide"] for m in sorted(set(c))
                           if m in novel and novel[m].get("hide") is not None] for c in cols]
        L = ["/* Generated by tools/bg_stream.py - do not edit */", "#pragma bank %d", "#include <gbdk/platform.h>",
             "#include <stdint.h>", "", "BANKREF(bs_%s)" % name, "", BS_SKIP_TYPE, ""]
        size = 0
        for cgb, slots in ((1, cgb_slots), (0, dmg_slots)):
            # the spinning tiles' slots can leave too few for the columns: fewer of them then
            for limit in range(SPIN_MAX, -1, -13):
                try:
                    st = Stream(art, name, cols, cgb, test_slots(slots), novel, limit, bank1_slots)
                    break
                except SystemExit:
                    if not limit:
                        raise
            st.simulate(cols)
            if cgb:
                bank1[name] = {m: v for m, v in st.pal_start.items() if v & BANK1_PAL}
            # DMG: all the tiles, then all of them mirrored (runs of tiles copy in one go)
            tiles = b"".join(tile_bytes(px[0] if len(px) == SPIN_FRAMES else px) for px in st.pool)
            if not cgb:
                tiles += b"".join(tile_bytes(flipped(px)) for px in st.pool)
            starts, start_cols = [], []
            recs = record_bytes(st, starts, start_cols)
            snaps[(name, "cgb" if cgb else "dmg")] = (snapshot_bytes(st), starts, start_cols)
            tag = "cgb" if cgb else "dmg"
            first[(name, tag)] = len(space) // 16
            space += tiles
            if st.spin:
                # the frames, one after the other, in one bank (one DMA reads them)
                data = b"".join(tile_bytes(st.pool[t][f]) for f in range(SPIN_FRAMES) for t in st.spin)
                if len(space) // 16384 != (len(space) + len(data) - 1) // 16384:
                    space += bytes(16384 - len(space) % 16384)
                spin[name] = (len(space) // 16, len(st.spin))
                space += data
            parts[tag] = (recs, starts)
        # A level with more records than a bank: each platform's first MAIN_PART bytes here, the
        # rest in banks of their own, each piece starting its bank (0x4000). A piece ends in the
        # column 0xFFFE and the next piece's bank (bg_stream.c bg_stream_to).
        big = sum(len(r) for r, _ in parts.values()) > 16384
        for tag, (recs, starts) in parts.items():
            # (the first piece shares its bank with the saved states' table: bs_skip)
            pieces, at, limit = [], 0, MAIN_PART - 16 - 10 * len(snaps[(name, tag)][0])
            cuts = starts + [len(recs) - 2]
            while big and len(recs) - at > limit:
                cut = max(x for x in cuts if x <= at + limit - 3)
                assert cut > at, "%s: one column's records are more than a bank" % name
                pieces.append(recs[at:cut])
                at, limit = cut, 16384
            pieces.append(recs[at:])
            arrays.append(("bs_%s_%s_recs" % (name, tag), pieces))
            piece_at[(name, tag)] = [sum(len(x) for x in pieces[:i]) for i in range(len(pieces))]
            size += len(pieces[0]) + (3 if len(pieces) > 1 else 0) + 2 + 10 * len(snaps[(name, tag)][0])
        files[name] = (L, size, arrays)

    # banks: top down, skipping every bank other files use
    for old in gfx.glob("bg_stream_*.c"):
        if old.stem[len("bg_stream_"):] not in files and not old.stem.startswith("bg_stream_more_"):
            old.unlink()
    used = set()
    for f in (ROOT / "src").rglob("*.c"):
        if f.parent == gfx and f.name.startswith("bg_stream_"):
            continue
        m = re.search(r'^#pragma\s+bank\s+(\d+)', f.read_text(errors="ignore"), re.M)
        if m:
            used.add(int(m.group(1)))
    # the tile space: the highest consecutive free banks
    pages = [bytes(space[i:i + 16384]) for i in range(0, len(space), 16384)]
    top = max(b for b in range(2, 255) if all(b - i not in used and b - i >= 2 for i in range(len(pages))))
    tile_bank = top - len(pages) + 1
    for i, data in enumerate(pages):
        used.add(tile_bank + i)
        T = ["/* Generated by tools/bg_stream.py - do not edit */", "#pragma bank %d" % (tile_bank + i),
             "#include <stdint.h>", ""] + c_array("bs_tile_space_%d" % i, data)
        (gfx / ("bg_stream_tiles_%d.c" % i)).write_text("\n".join(T) + "\n", encoding="utf-8")
    # the saved stream states (snapshot_bytes): packed into free banks, a state never split
    for old in gfx.glob("bg_stream_snap_*.c"):
        old.unlink()
    snap_at = {}           # (level, platform, column) -> (array name, bank)
    snap_files, room = [], 0
    for (name, tag), (states, _, _) in snaps.items():
        for k, data in states:
            if len(data) > room:
                b = max(b for b in range(2, 255) if b not in used)
                used.add(b)
                snap_files.append((b, ["/* Generated by tools/bg_stream.py - do not edit */", "#pragma bank %d" % b,
                                       "#include <stdint.h>", ""]))
                room = 16384
            room -= len(data)
            arr = "bs_snap_%s_%s_%d" % (name, tag, k)
            snap_files[-1][1].extend(c_array(arr, data))
            snap_at[(name, tag, k)] = (arr, snap_files[-1][0])
    for i, (b, T) in enumerate(snap_files):
        (gfx / ("bg_stream_snap_%d.c" % i)).write_text("\n".join(T) + "\n", encoding="utf-8")
    skip_n = {}

    bank, room = None, 0
    bank_of = {}
    more = 0
    for name, (L, size, arrays) in files.items():
        if size > room:
            bank = max(b for b in range(2, 255) if b not in used)
            used.add(bank)
            room = 16384
        room -= size
        L[1] = L[1] % bank
        bank_of[name] = bank
        for array, pieces in arrays:
            tag = array.rsplit("_", 2)[-2]
            pbank = [bank] + [None] * (len(pieces) - 1)
            # the pieces past the first: a bank each, from the last one (each ends in the next's bank)
            nxt = None
            for i in range(len(pieces) - 1, 0, -1):
                b = max(b for b in range(2, 255) if b not in used)
                used.add(b)
                T = ["/* Generated by tools/bg_stream.py - do not edit */", "#pragma bank %d" % b,
                     "#include <stdint.h>", "", "// %s, piece %d: at the start of the bank" % (array, i), ""]
                T += c_array("%s_%d" % (array, i), pieces[i] + (bytes([0xFE, 0xFF, nxt]) if nxt else b""))
                (gfx / ("bg_stream_more_%d.c" % more)).write_text("\n".join(T) + "\n", encoding="utf-8")
                more += 1
                nxt = b
                pbank[i] = b
            L += c_array(array, pieces[0] + (bytes([0xFE, 0xFF, nxt]) if nxt else b""))
            # where a reload goes on after each saved state (bg_stream.c bs_skip): the first column
            # with records after it, its records' place (the piece's bank)
            states, starts, start_cols = snaps[(name, tag)]
            rows = []
            for k, _ in states:
                arr, sb = snap_at[(name, tag, k)]
                L.append("extern const uint8_t %s[];" % arr)
                nxt_i = [i for i, c in enumerate(start_cols) if c > k]
                if not nxt_i:
                    rows.append("{ %d, 0xFFFF, %s, %s, %d, %d }" % (k, array, arr, bank, sb))
                    continue
                off = starts[nxt_i[0]]
                pi = max(i for i, a in enumerate(piece_at[(name, tag)]) if a <= off)
                rp = ("%s + %d" % (array, off + 2)) if pi == 0 else                      "(const uint8_t *)0x%04X" % (0x4000 + off - piece_at[(name, tag)][pi] + 2)
                rows.append("{ %d, %d, %s, %s, %d, %d }" % (k, start_cols[nxt_i[0]], rp, arr, pbank[pi], sb))
            skip_n[(name, tag)] = len(rows)
            L += ["const BsSkip %s_skips[] = {" % array[:-len("_recs")]]
            L += ["    %s," % row for row in rows] or ["    { 0xFFFF, 0xFFFF, 0, 0, 0, 0 },"]
            L.append("};")
        (gfx / ("bg_stream_%s.c" % name)).write_text("\n".join(L) + "\n", encoding="utf-8")
    for old in gfx.glob("bg_stream_more_*.c"):
        if int(old.stem[len("bg_stream_more_"):]) >= more:
            old.unlink()

    H = ["/* Generated by tools/bg_stream.py - do not edit */", "#ifndef BG_STREAM_TABLES_H",
         "#define BG_STREAM_TABLES_H", "", "#include <gbdk/platform.h>", "",
         "#define BS_LEVEL_COUNT %d" % len(pools)]
    for name, *_ in pools:
        H += ["BANKREF_EXTERN(bs_%s)" % name] + ["extern const uint8_t bs_%s_%s_%s[];" % (name, t, k)
                                                 for t in ("cgb", "dmg") for k in ("recs",)]
    H += ["#define BS_TILE_BANK %d" % tile_bank, "#define BS_SPIN_BASE %d" % SPIN_BASE,
          "#define BS_SPIN_FRAMES %d" % SPIN_FRAMES]
    H += ["// per level: its spinning tiles (CGB): first tile in the space, number",
          "static const uint16_t bs_spin_first[BS_LEVEL_COUNT] = { %s };"
          % ", ".join(str(spin.get(n, (0, 0))[0]) for n, *_ in pools),
          "static const uint8_t bs_spin_count[BS_LEVEL_COUNT] = { %s };"
          % ", ".join(str(spin.get(n, (0, 0))[1]) for n, *_ in pools)]
    H += ["// per level: the CGB data, then the DMG data (DMG: the tiles, then the same tiles mirrored)",
          "static const uint16_t bs_tile_first[BS_LEVEL_COUNT * 2] = {"]
    H += ["    %d, %d," % (first[(n, "cgb")], first[(n, "dmg")]) for n, *_ in pools] + ["};"]
    H += ["// per level: the CGB, then the DMG saved stream states (tools/bg_stream.py snapshot_bytes)", BS_SKIP_TYPE]
    H += ["extern const BsSkip bs_%s_%s_skips[];" % (n, t) for n, *_ in pools for t in ("cgb", "dmg")]
    H += ["static const BsSkip * const bs_skips[BS_LEVEL_COUNT * 2] = {"]
    H += ["    bs_%s_cgb_skips, bs_%s_dmg_skips," % (n, n) for n, *_ in pools] + ["};"]
    H += ["static const uint8_t bs_skip_n[BS_LEVEL_COUNT * 2] = { %s };"
          % ", ".join("%d, %d" % (skip_n[(n, "cgb")], skip_n[(n, "dmg")]) for n, *_ in pools)]
    H += ["static const uint8_t * const bs_recs[BS_LEVEL_COUNT * 2] = {"]
    H += ["    bs_%s_cgb_recs, bs_%s_dmg_recs," % (n, n) for n, *_ in pools] + ["};"]
    def fade_of(novel):
        hides = sorted(m for m in novel if novel[m].get("hide") is not None)
        return (hides[0], len(hides)) if hides else (0, 0)
    H += ["// per level: its fading cells (tools/compose.py): first metatile, how many; each one's hidden look is that many further",
          "static const uint8_t bs_fade[BS_LEVEL_COUNT * 2] = { %s };"
          % ", ".join("%d, %d" % fade_of(pool[4]) for pool in pools)]
    H += ["static const uint8_t bs_banks[BS_LEVEL_COUNT] = {"]
    H += ["    %d,   // %s" % (bank_of[n], n) for n, *_ in pools] + ["};", "", "#endif", ""]
    (gfx / "bg_stream_tables.h").write_text("\n".join(H), encoding="utf-8")
    return bank1


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
