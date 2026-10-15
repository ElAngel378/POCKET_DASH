"""Builds the gameplay background tile data from ONE image: levels/chr_data/bg_tiles.png.

    python tools/build_bg_tiles.py

bg_tiles.png is 16 tiles (128 px) wide; the colour of a pixel is its Game Boy colour index
(white = 0, light grey = 1, dark grey = 2, black = 3). Edit the art there and rerun this.

Sections (first tile row, tile count). Tile rows are 8 px:
    row  0  BASE          160  the level tile sheet every level loads (VRAM tiles 0..159);
                               include/famidash_metatiles_dmg.c indexes into it
    row 10  BLACK_FILL      1  CGB: replaces tile 26 (Famidash BLACK_* fill: black is
                               colour 2 on CGB, colour 3 on DMG)
    row 11  SAWS_0         52  sawblades, animation frame 0 (rows 11..14; rows 15, 16 are unused)
                               CGB: VRAM bank 1 tiles 64..115 (all levels); DMG: loaded into free
                               slots of levels with saws. Colours: 2 = black, 1 = ring, 3 = hub
                               (the DMG copy gets a colour 3 outline, see dmg_saw_outline)
    row 22  SAWS_1         52  sawblades, animation frame 1 (rotated anticlockwise)
    row 26  SAWS_2         52  sawblades, animation frame 2
    row 17  BLOCKS_B_CGB   20  Famidash block set B (CGB), replaces BLOCKS_B_SLOTS
    row 19  BLOCKS_B_DMG   20  Famidash block set B (DMG), replaces BLOCKS_B_SLOTS
    row 21  SPIKES_B_CGB    8  Famidash spike set B background spikes (CGB and DMG: the
                               gameplay BGP keeps the CGB colour order), replaces SPIKES_B_SLOTS
    row 30  SPIKES_C        4  Famidash spike set C pit spikes (CGB and DMG, tools/make_spikes_c.py),
                               replaces SPIKES_C_SLOTS
    row 31  BLOCKS_B2_CGB  20  the rest of block set B: plain blocks as bricks (CGB,
                               tools/make_blocks_b2.py), replaces BLOCKS_B2_SLOTS
    row 33  BLOCKS_B2_DMG  20  the same for DMG

Which Famidash tile sets a level uses: LEVEL_SETS below.

Outputs:
    levels/chr_data/bg_base_tiles.bin          BASE, and the mirrored copy
    levels/chr_data/bg_base_tiles_flipped.bin  (mirror mode loads the flipped sheet)
    levels/chr_data/bg_extra_tiles.bin         every other section, per tile: normal + mirrored
    levels/chr_data/saw_anim_tiles.bin         the 3 saw frames, contiguous (CGB streams them)
    src/graphics/bg_level_tables.h             saw metatile tables + per level load lists
    include/bg_tiles.h                         BG_BASE_TILE_COUNT

Per level and platform the load list says: extra tiles to load (VRAM bank, slot, count,
first extra tile), sheet tiles to move (DMG), and metatile tiles to override. On DMG, BG
tiles 144..159 share VRAM with the coin sprite tiles, so tiles a level uses there move to
slots it leaves free, and its saw tiles go to free slots too.
"""
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "bg_tiles.png"

SAW_FRAMES = 3
SAW_TILES = 52   # saw tiles per animation frame: 0x00..0x2F (see SAW_METATILES) + 4 centre tiles

SECTIONS = [  # name, first row, tile count
    ("BASE", 0, 160),
    ("BLACK_FILL", 10, 1),
    ("SAWS_0", 11, SAW_TILES),
    ("BLOCKS_B_CGB", 17, 20),
    ("BLOCKS_B_DMG", 19, 20),
    ("SPIKES_B_CGB", 21, 8),
    ("SAWS_1", 22, SAW_TILES),
    ("SAWS_2", 26, SAW_TILES),
    ("SPIKES_C", 30, 4),
    ("BLOCKS_B2_CGB", 31, 20),
    ("BLOCKS_B2_DMG", 33, 20),
]
BLOCKS_B_SLOTS = [82, 83, 84, 85, 86, 87, 98, 99, 100, 101, 102, 103, 110, 111, 112, 113, 122, 123, 124, 125]
# plain blocks (NES $40..$49, $50..$59): Theory of Everything's bricks
BLOCKS_B2_SLOTS = list(range(72, 82)) + list(range(88, 98))
SPIKES_B_SLOTS = [0, 1, 23, 24, 25, 37, 38, 39]
# Metatiles 12 / 13 (background spikes) have the transparent tile 12 where spike set B has
# art: on CGB those quarters use tiles 0 / 1 of SPIKES_B_SLOTS.
SPIKES_B_OVERRIDES = [(12, 0, 0), (13, 2, 1)]
SPIKES_C_SLOTS = [19, 20, 33, 34]   # pit spikes left / right (NES $08, $09, $18, $19)
BLACK_FILL_SLOT = 26

# Famidash tile sets per level (by level map name), defaults: spikes A, blocks A
LEVEL_SETS = {
    "xstep": {"spikes": "B"},
    "clutterfunk": {"blocks": "B"},
    "theoryofeverything": {"spikes": "C", "blocks": "B"},
}

SAW_CENTER_MT = 120
BIG_SAW_PARTS = {116, 117, 118, 119, 121, 122, 123, 124}
SAW_VRAM_BASE = 64           # CGB VRAM bank 1
DMG_BG_SLOTS = 144           # DMG BG tiles 0..143 (144..159 = coin sprite tiles)
DMG_RESERVED_TILES = [12, 26] + list(range(48, 54))   # blank, black fill, ground (used by code)
# DMG BG tiles 128..255 are sprite tiles 128..255. Some of those sprites are never drawn on DMG, so
# a level short of slots (Theory of Everything: block set B, spike set C and every saw) puts saw
# tiles there: 224..237 (CGB-only mirror exit art; 238, 239 are the UFO badge) and, in levels
# without mirror portals, 208..223 (the mirror portal art). Only saw tiles (reloaded from ROM after
# the sprite tiles by reapply_bg_shared_slots), never moved sheet tiles (copied from VRAM).
DMG_SHARED_SLOTS = list(range(224, 238))
DMG_MIRROR_SLOTS = list(range(208, 224))

# Sawblade metatiles: metatile id -> its 4 saw tiles (0..47, None = keep the sheet tile)
SAW_METATILES = {
    4: (0x1A, 0x1F, None, None),    # SMALL_SAW_BOTTOM_HALF
    8: (0x20, 0x21, 0x24, 0x25),    # MED_SAW_TOP_LEFT
    9: (0x22, 0x23, 0x26, 0x27),    # MED_SAW_TOP_RIGHT
    10: (0x28, 0x29, 0x2C, 0x2D),   # MED_SAW_BOTTOM_LEFT
    11: (0x2A, 0x2B, 0x2E, 0x2F),   # MED_SAW_BOTTOM_RIGHT
    116: (None, 0x01, 0x10, 0x11),  # BIG_SAW_TOP_LEFT
    117: (0x02, 0x03, 0x12, 0x13),  # BIG_SAW_TOP_MIDDLE
    118: (0x04, None, 0x14, 0x15),  # BIG_SAW_TOP_RIGHT
    119: (0x06, 0x07, 0x16, 0x17),  # BIG_SAW_MIDDLE_LEFT
    121: (0x08, 0x09, 0x18, 0x19),  # BIG_SAW_MIDDLE_RIGHT
    122: (0x0A, 0x0B, None, 0x1B),  # BIG_SAW_BOTTOM_LEFT
    123: (0x0C, 0x0D, 0x1C, 0x1D),  # BIG_SAW_BOTTOM_MIDDLE
    124: (0x0E, 0x0F, 0x1E, None),  # BIG_SAW_BOTTOM_RIGHT
    120: (0x30, 0x31, 0x32, 0x33),  # BIG_SAW_CENTER
    125: (0x00, 0x05, 0x1A, 0x1F),  # SMALL_SAW
    127: (None, None, 0x00, 0x05),  # SMALL_SAW_TOP_HALF
}


def read_png():
    img = Image.open(PNG).convert("L")
    w, h = img.size
    assert w == 128 and h % 8 == 0, "bg_tiles.png must be 128 px wide"
    px = img.load()
    # nearest of the 4 greys: 255 -> 0, 170 -> 1, 85 -> 2, 0 -> 3
    idx = lambda v: min(range(4), key=lambda i: abs(v - (255 - 85 * i)))
    tiles = []
    for ty in range(h // 8):
        for tx in range(16):
            rows = [[idx(px[tx * 8 + x, ty * 8 + r]) for x in range(8)] for r in range(8)]
            tiles.append(rows)
    return tiles


def dmg_saw_outline(rows):
    """DMG saws: the art only uses colours 1 and 2, which the dark DMG background palettes
    map to black like the dark sky. Saw pixels next to a sky pixel (colour 0) in the same
    tile become colour 3, which those palettes map to white (and the normal one to black)."""
    out = [r[:] for r in rows]
    for y in range(8):
        for x in range(8):
            if rows[y][x] == 0:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < 8 and 0 <= ny < 8 and rows[ny][nx] == 0:
                    out[y][x] = 3
                    break
    return out


def gb_bytes(rows):
    out = bytearray()
    for row in rows:
        lo = hi = 0
        for v in row:
            lo = (lo << 1) | (v & 1)
            hi = (hi << 1) | (v >> 1)
        out += bytes([lo, hi])
    return bytes(out)


def mirrored(rows):
    return [row[::-1] for row in rows]


def runs(slots, first_src, only=None):
    """[(dst, count, src)] for runs of consecutive slots (only: the slots to load, default all)"""
    out = []
    for i, s in enumerate(slots):
        if only is not None and s not in only:
            continue
        if out and out[-1][0] + out[-1][1] == s and out[-1][2] + out[-1][1] == first_src + i:
            out[-1] = (out[-1][0], out[-1][1] + 1, out[-1][2])
        else:
            out.append((s, 1, first_src + i))
    return out


def main():
    all_tiles = read_png()
    section = {}
    for name, row, count in SECTIONS:
        section[name] = all_tiles[row * 16: row * 16 + count]
        assert len(section[name]) == count, name

    # The saws in bg_extra_tiles are only shown on DMG (CGB streams saw_anim_tiles.bin):
    # give them an outline there, see dmg_saw_outline.
    cgb_saws = [section["SAWS_%d" % f] for f in range(SAW_FRAMES)]
    for f in range(SAW_FRAMES):
        section["SAWS_%d" % f] = [dmg_saw_outline(t) for t in cgb_saws[f]]

    out_dir = ROOT / "levels" / "chr_data"
    (out_dir / "bg_base_tiles.bin").write_bytes(b"".join(gb_bytes(t) for t in section["BASE"]))
    (out_dir / "bg_base_tiles_flipped.bin").write_bytes(b"".join(gb_bytes(mirrored(t)) for t in section["BASE"]))

    # extra tiles: every section but BASE, in SECTIONS order; first index of each section
    extra = bytearray()
    first = {}
    k = 0
    for name, _, count in SECTIONS[1:]:
        first[name] = k
        for t in section[name]:
            extra += gb_bytes(t) + gb_bytes(mirrored(t))
        k += count
    assert k < 256
    (out_dir / "bg_extra_tiles.bin").write_bytes(extra)
    # saw animation frames for the CGB VBlank handler: contiguous 16 byte tiles, 3 frames
    # (the art as drawn: bg_extra_tiles has the DMG version, see dmg_saw_outline)
    (out_dir / "saw_anim_tiles.bin").write_bytes(b"".join(
        gb_bytes(t) for f in range(SAW_FRAMES) for t in cgb_saws[f]))

    # levels in game_levels order
    assets = (ROOT / "src" / "assets.c").read_text()
    order = re.findall(r'&(level_\w+)', assets.split("game_levels[] = {")[1].split("};")[0])
    ident = dict(re.findall(r'const Level (level_\w+) = \{.*?\n\s*(\w+)_map,', assets, re.S))
    src = (ROOT / "include" / "famidash_metatiles_dmg.c").read_text()
    a = src.index("const uint8_t metatiles[FAMIDASH_NUM_METATILES][4]")
    sheet_mt = [list(map(int, m)) for m in re.findall(r'\{ (\d+), (\d+), (\d+), (\d+) \}', src[a:src.index("};", a)])]

    def stream(loads, moves, overrides):
        data = [len(loads)] + [v for l in loads for v in l]
        data += [len(moves)] + [v for m in moves for v in m]
        data += [len(overrides)] + [v for o in overrides for v in o]
        return data

    streams = []   # (level var, cgb data, dmg data)
    for lv in order:
        name = ident[lv]
        sets = LEVEL_SETS.get(name, {})
        level_mts = set((ROOT / "levels" / "level_data" / ("%s.bin" % name)).read_bytes())
        # metatile 120 is also Xstep's diamond: it is the big saw's centre only where big saws are
        big_saw = bool(level_mts & BIG_SAW_PARTS)
        saw_mts = {m: t for m, t in SAW_METATILES.items() if m != SAW_CENTER_MT or big_saw}
        # BLOCKS_B2: only the slots the level's metatiles show (DMG slots are scarce)
        b2 = {t for m in level_mts for t in sheet_mt[m]} & set(BLOCKS_B2_SLOTS)

        # CGB
        # bank bit 7: never load mirrored (CGB mirrors saws with the X flip attribute)
        loads = [(0x81, SAW_VRAM_BASE, SAW_TILES, first["SAWS_0"]),
                 (0, BLACK_FILL_SLOT, 1, first["BLACK_FILL"])]
        overrides = []
        if sets.get("blocks") == "B":
            loads += [(0, d, n, s) for d, n, s in runs(BLOCKS_B_SLOTS, first["BLOCKS_B_CGB"])]
            loads += [(0, d, n, s) for d, n, s in runs(BLOCKS_B2_SLOTS, first["BLOCKS_B2_CGB"], b2)]
        if sets.get("spikes") == "B":
            loads += [(0, d, n, s) for d, n, s in runs(SPIKES_B_SLOTS, first["SPIKES_B_CGB"])]
            overrides += SPIKES_B_OVERRIDES
        if sets.get("spikes") == "C":
            loads += [(0, d, n, s) for d, n, s in runs(SPIKES_C_SLOTS, first["SPIKES_C"])]
        cgb = stream(loads, [], overrides)

        # DMG
        loads = []
        if sets.get("blocks") == "B":
            loads += [(0, d, n, s) for d, n, s in runs(BLOCKS_B_SLOTS, first["BLOCKS_B_DMG"])]
            loads += [(0, d, n, s) for d, n, s in runs(BLOCKS_B2_SLOTS, first["BLOCKS_B2_DMG"], b2)]
        overrides = []
        if sets.get("spikes") == "B":
            loads += [(0, d, n, s) for d, n, s in runs(SPIKES_B_SLOTS, first["SPIKES_B_CGB"])]
            overrides += SPIKES_B_OVERRIDES
        if sets.get("spikes") == "C":
            loads += [(0, d, n, s) for d, n, s in runs(SPIKES_C_SLOTS, first["SPIKES_C"])]
        used_t = set(DMG_RESERVED_TILES)
        # slots that hold a loaded tile set B must not be handed out as free slots
        if sets.get("blocks") == "B":
            used_t.update(BLOCKS_B_SLOTS)
            used_t.update(b2)
        if sets.get("spikes") == "B":
            used_t.update(SPIKES_B_SLOTS)
        if sets.get("spikes") == "C":
            used_t.update(SPIKES_C_SLOTS)
        saw_k = set()
        for m in level_mts:
            for q in range(4):
                st = saw_mts[m][q] if m in saw_mts else None
                if st is not None:
                    saw_k.add(st)
                else:
                    used_t.add(sheet_mt[m][q])
        high = sorted(t for t in used_t if t >= DMG_BG_SLOTS)
        free = [s for s in range(DMG_BG_SLOTS) if s not in used_t]
        if len(high) > len(free):
            raise SystemExit("%s: needs %d free DMG BG slots, has %d" % (name, len(high), len(free)))
        moves = list(zip(free, high))
        saw_free = free[len(high):]
        if len(saw_k) > len(saw_free):
            sp = (ROOT / "src" / "sprites" / ("%s_sprites.c" % name)).read_text()
            mirror = re.search(r"\{\s*\d+,\s*\d+,\s*(121|126)\s*\}", sp) is not None
            saw_free += DMG_SHARED_SLOTS + ([] if mirror else DMG_MIRROR_SLOTS)
        if len(saw_k) > len(saw_free):
            raise SystemExit("%s: needs %d free DMG BG slots, has %d" % (name, len(high) + len(saw_k), len(free) + len(saw_free) - len(free[len(high):])))
        saw_slot = dict(zip(sorted(saw_k), saw_free))
        for k_ in sorted(saw_slot):
            loads.append((0, saw_slot[k_], 1, first["SAWS_0"] + k_))
        overrides += [(m, q, saw_slot[saw_mts[m][q]]) for m in sorted(level_mts) if m in saw_mts
                      for q in range(4) if saw_mts[m][q] is not None]
        dmg = stream(loads, moves, overrides)
        streams.append((lv, name, cgb, dmg, big_saw, any(m in saw_mts for m in level_mts)))
        if moves or saw_slot:
            shared = sum(1 for s in saw_slot.values() if s >= DMG_BG_SLOTS)
            print("DMG %-20s %d tiles moved above slot %d, %d saw tiles (%d in sprite tiles), %d free slots left"
                  % (name, len(moves), DMG_BG_SLOTS, len(saw_slot), shared, len(saw_free) - len(saw_k)))

    # header
    idx = [0] * 256
    saw_rows = []
    for i, (m, tl) in enumerate(sorted(SAW_METATILES.items())):
        idx[m] = i + 1
        saw_rows.append("    { %s }, /* metatile %d */" % (", ".join(
            "0xFF" if t is None else "0x%02X" % (SAW_VRAM_BASE + t) for t in tl), m))
    L = ["/* Generated by tools/build_bg_tiles.py from levels/chr_data/bg_tiles.png - do not edit */",
         "#ifndef BG_LEVEL_TABLES_H", "#define BG_LEVEL_TABLES_H", "",
         "#define SAW_VRAM_BASE %d" % SAW_VRAM_BASE,
         "#define SAW_MT_COUNT %d" % len(SAW_METATILES), "",
         "// CGB saw metatiles: saw_mt_index[metatile] = row in saw_mt_tiles + 1 (0 = not a saw);",
         "// the 4 tiles are in VRAM bank 1 (0xFF = keep the sheet tile)",
         "static const uint8_t saw_mt_index[256] = {"]
    for r in range(0, 256, 16):
        L.append("    " + ", ".join(str(v) for v in idx[r:r + 16]) + ",")
    L += ["};", "static const uint8_t saw_mt_tiles[SAW_MT_COUNT][4] = {"] + saw_rows + ["};", "",
          "// Per level load lists (see tools/build_bg_tiles.py):",
          "//   n, n x (VRAM bank (bit 7: never mirrored), slot, count, first bg_extra_tiles tile)",
          "//   n, n x (slot, sheet tile)   sheet tiles moved to another slot (DMG)",
          "//   n, n x (metatile, quarter, tile)"]
    for lv, name, cgb, dmg, _, _ in streams:
        L.append("static const uint8_t bg_cgb_%s[] = { %s };" % (name, ", ".join(map(str, cgb))))
        L.append("static const uint8_t bg_dmg_%s[] = { %s };" % (name, ", ".join(map(str, dmg))))
    L += ["#define BG_LEVEL_COUNT %d" % len(streams),
          "#define SAW_CENTER_MT %d" % SAW_CENTER_MT,
          "// Saw animation: frame f of the saw tiles is bg_extra_tiles tile saw_frame_first[f] + 0..SAW_TILES-1",
          "// (CGB streams the 3 frames from saw_anim_tiles.bin: SAW_TILES tiles of 16 bytes per frame)",
          "#define SAW_ANIM_FRAMES %d" % SAW_FRAMES,
          "#define SAW_ANIM_TILES %d" % SAW_TILES,
          "static const uint8_t saw_frame_first[SAW_ANIM_FRAMES] = { %s };" % ", ".join(
              str(first["SAWS_%d" % f]) for f in range(SAW_FRAMES)),
          "// 1 = the level has saws (animate them)",
          "static const uint8_t bg_level_saws[BG_LEVEL_COUNT] = { %s };" % ", ".join(str(int(s[5])) for s in streams),
          "// 1 = the level has big saws, so metatile SAW_CENTER_MT is their centre (CGB)",
          "static const uint8_t bg_level_big_saws[BG_LEVEL_COUNT] = { %s };" % ", ".join(str(int(s[4])) for s in streams),
          "static const uint8_t * const bg_level_cgb[BG_LEVEL_COUNT] = { %s };" % ", ".join("bg_cgb_" + s[1] for s in streams),
          "static const uint8_t * const bg_level_dmg[BG_LEVEL_COUNT] = { %s };" % ", ".join("bg_dmg_" + s[1] for s in streams),
          "", "#endif", ""]
    (ROOT / "src" / "graphics" / "bg_level_tables.h").write_text("\n".join(L), encoding="utf-8")
    (ROOT / "include" / "bg_tiles.h").write_text("\n".join([
        "/* Generated by tools/build_bg_tiles.py - do not edit */",
        "#ifndef BG_TILES_H", "#define BG_TILES_H", "",
        "// Tiles in levels/chr_data/bg_base_tiles.bin (VRAM tiles 0..BG_BASE_TILE_COUNT-1)",
        "#define BG_BASE_TILE_COUNT %d" % len(section["BASE"]), "", "#endif", ""]), encoding="utf-8")
    print("bg_tiles.png: %d base tiles, %d extra tiles, %d levels" % (len(section["BASE"]), k, len(streams)))


if __name__ == "__main__":
    main()
