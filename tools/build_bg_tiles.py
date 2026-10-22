"""Builds the gameplay background tile data: every level's own tile sheets and tables, from the
object catalog (levels/objects/objects.tsx + objects.png) and the objects its TMX uses, plus the
saw animation tiles from levels/chr_data/bg_tiles.png.

    python tools/build_bg_tiles.py

PER LEVEL (levels in game_levels order, src/assets.c; the objects: levels/chr_data/tmx/<level>.tmx,
the map bytes: levels/level_data/<level>.bin, written by tools/build_levels.py): a map byte is the
level's id of an object (tools/objects_catalog.py assign_level_ids: its famidash_id when it has
one, so the engine keeps knowing the saw parts, ground looks and pit spikes). For both platforms:
    - the object's 4 quarters (CGB art / DMG art from the catalog) are cut into 8x8 tiles; a blank
      quarter is the reserved transparent tile 12 (CGB: the parallax background shows through it),
      identical tiles are merged
    - they get sheet slots: CGB 0..159 (160..255 are sprite tiles), DMG 0..143 (144..159 are the
      coin sprite tiles)
    - its sheet: those tiles at their slots, BG_BASE_TILE_COUNT tiles (DMG: then the mirrored copy,
      loaded by a mirror portal; CGB mirrors with the X flip attribute)
    - its metatile table (256 x 4 slots), collision table and CGB palette table
SAWS animate: a saw part's quarters (SAW_METATILES) are drawn from the saw tiles. CGB: VRAM bank 1
tiles SAW_VRAM_BASE.. (all levels, streamed from saw_anim_tiles.bin). DMG: the level's saw tiles
go to free sheet slots, then to sprite tiles DMG never draws (DMG_SHARED_SLOTS, and
DMG_MIRROR_SLOTS in levels without mirror portals), loaded and animated from bg_extra_tiles.bin.

bg_tiles.png is 16 tiles (128 px) wide; the colour of a pixel is its Game Boy colour index
(white = 0, light grey = 1, dark grey = 2, black = 3). Sections (first tile row, tile count):
    row  0  BASE          160  the Famidash tile sheet the catalog was made from (tools/make_object_
                               catalog.py); its bins are still the levels' `tiles` in src/assets.c
    row 11  SAWS_0         52  sawblades, animation frame 0 (rows 11..14). Colours: 2 = black,
                               1 = ring, 3 = hub (the DMG copy gets a colour 3 outline, see
                               dmg_saw_outline)
    row 22  SAWS_1         52  sawblades, animation frame 1 (rotated anticlockwise)
    row 26  SAWS_2         52  sawblades, animation frame 2
    (rows 10, 17..21, 30..34: the Famidash tile set sections the catalog was made from)

Outputs:
    levels/chr_data/bg_base_tiles.bin, bg_base_tiles_flipped.bin   BASE (normal / mirrored)
    levels/chr_data/bg_extra_tiles.bin    the other sections, per tile: normal + mirrored
    levels/chr_data/saw_anim_tiles.bin    the 3 saw frames as drawn (CGB streams them)
    levels/chr_data/bg_sheet_<level>_cgb.bin, _dmg.bin, src/graphics/bg_sheet_<level>.c   sheets
    src/graphics/bg_level_mt_*.c          per level tables (deduplicated)
    src/graphics/bg_level_lists.c         per level load lists and table / sheet pointers
    src/graphics/bg_level_tables.h        saw tables, per level flags, banks
    include/bg_tiles.h                    BG_BASE_TILE_COUNT
"""
import re
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from objects_catalog import load_catalog, tmx_level_objects, assign_level_ids, write_preview, write_sprite_preview  # noqa: E402

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
    # the 1.6 saws (tools/make_saws_16.py): CGB only, so not in bg_extra_tiles (DMG draws the
    # Famidash saws in those levels too)
    ("SAWS16_0", 35, SAW_TILES),
    ("SAWS16_1", 39, SAW_TILES),
    ("SAWS16_2", 43, SAW_TILES),
]

SHEET_TILES = 160            # BG_BASE_TILE_COUNT: a sheet fills VRAM tiles 0..159
BLANK_TILE = 12              # transparent (CGB: the parallax shows through); the level complete
                             # screen's board too
CGB_SLOTS = [s for s in range(SHEET_TILES) if s != BLANK_TILE]
DMG_BG_SLOTS = 144           # DMG BG tiles 0..143 (144..159 = coin sprite tiles)
DMG_SLOTS = [s for s in range(DMG_BG_SLOTS) if s != BLANK_TILE]
# DMG BG tiles 128..255 are sprite tiles 128..255. Some of those sprites are never drawn on DMG, so
# a level short of slots puts saw tiles there: 224..237 (CGB-only mirror exit art; 238, 239 are the
# UFO badge) and, in levels without mirror portals, 208..223 (the mirror portal art). Only saw tiles
# (reloaded from ROM after the sprite tiles by reapply_bg_shared_slots).
DMG_SHARED_SLOTS = list(range(224, 238))
DMG_MIRROR_SLOTS = list(range(208, 224))

SAW_CENTER_MT = 120
BIG_SAW_PARTS = {116, 117, 118, 119, 121, 122, 123, 124}
SAW_VRAM_BASE = 64           # CGB VRAM bank 1

# Sawblade metatiles: metatile id -> its 4 saw tiles (0..47, None = the object's own art)
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
    return [list(row[::-1]) for row in rows]


def mirrored_bytes(data):
    """2bpp tile data mirrored left / right"""
    rev = lambda b: int("{:08b}".format(b)[::-1], 2)
    return bytes(rev(b) for b in data)


def vflipped_bytes(data):
    """2bpp tile data mirrored top / bottom"""
    return b"".join(data[r * 2:r * 2 + 2] for r in range(7, -1, -1))


# CGB attribute flip bits, as the flip table stores them per quarter (2 bits: X, Y)
FLIP_X, FLIP_Y = 1, 2


def canonical(data):
    """(the tile data a CGB sheet holds for data, its flips: data is that tile shown with them):
    one tile for all 4 flipped copies"""
    h = mirrored_bytes(data)
    v = vflipped_bytes(data)
    hv = vflipped_bytes(h)
    best = min(data, h, v, hv)
    return best, (0 if best == data else FLIP_X if best == h else FLIP_Y if best == v else FLIP_X | FLIP_Y)


# Ground look metatiles: the engine draws them from the ground strip, not from their tiles (no flips)
GROUND_LOOK_MTS = {1, 2, 5, 6, 136, 137}


def level_objects(name, cat):
    """level id -> catalog object of the level (its TMX's map layer, tools/objects_catalog.py)"""
    tids = tmx_level_objects(ROOT / "levels" / "chr_data" / "tmx" / ("%s.tmx" % name))
    assert tids is not None, "%s.tmx does not use the object catalog (tools/convert_tmx_to_catalog.py)" % name
    return {i: tid for tid, i in assign_level_ids(cat, tids).items()}


def allocate(datas, slots, what):
    """slot per distinct tile (first come first served); BLANK_TILE for a blank one"""
    slot, it = {}, iter(slots)
    for d in datas:
        if d in slot:
            continue
        if d == bytes(16):
            slot[d] = BLANK_TILE
            continue
        s = next(it, None)
        if s is None:
            raise SystemExit("%s: more than %d distinct tiles" % (what, len(slots)))
        slot[d] = s
    return slot


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
    gfx = ROOT / "src" / "graphics"
    (out_dir / "bg_base_tiles.bin").write_bytes(b"".join(gb_bytes(t) for t in section["BASE"]))
    (out_dir / "bg_base_tiles_flipped.bin").write_bytes(b"".join(gb_bytes(mirrored(t)) for t in section["BASE"]))

    # extra tiles: every section but BASE, in SECTIONS order; first index of each section
    extra = bytearray()
    first = {}
    k = 0
    for name, _, count in SECTIONS[1:]:
        if name.startswith("SAWS16"):
            continue
        first[name] = k
        for t in section[name]:
            extra += gb_bytes(t) + gb_bytes(mirrored(t))
        k += count
    assert k < 256
    (out_dir / "bg_extra_tiles.bin").write_bytes(extra)
    n_extra = k
    # saw animation frames for the CGB VBlank handler: contiguous 16 byte tiles, 3 frames
    # (the art as drawn: bg_extra_tiles has the DMG version, see dmg_saw_outline)
    # then the 1.6 saw set's 3 frames (saw set 1)
    (out_dir / "saw_anim_tiles.bin").write_bytes(b"".join(
        gb_bytes(t) for f in range(SAW_FRAMES) for t in cgb_saws[f]) + b"".join(
        gb_bytes(t) for f in range(SAW_FRAMES) for t in section["SAWS16_%d" % f]))
    saw_extra = lambda s, rev: bytes(extra[(first["SAWS_0"] + s) * 32 + (16 if rev else 0):][:16])

    # levels in game_levels order
    assets = (ROOT / "src" / "assets.c").read_text()
    order = re.findall(r'&(level_\w+)', assets.split("game_levels[] = {")[1].split("};")[0])
    ident = dict(re.findall(r'const Level (level_\w+) = \{.*?\n\s*(\w+)_map,', assets, re.S))
    write_preview()                       # Tiled's views of objects.png and sprites.png
    write_sprite_preview()
    cat = load_catalog()

    def has_mirror_portal(name):
        sp = (ROOT / "src" / "sprites" / ("%s_sprites.c" % name)).read_text()
        return re.search(r"\{\s*\d+,\s*\d+,\s*(121|126)\s*\}", sp) is not None

    def stream(loads):
        return [len(loads)] + [v for l in loads for v in l]

    levels = []
    for lv in order:
        name = ident[lv]
        objs = level_objects(name, cat)
        level_mts = set((ROOT / "levels" / "level_data" / ("%s.bin" % name)).read_bytes())
        missing = level_mts - set(objs) - {0}
        assert not missing, "%s: map ids without an object: %s" % (name, sorted(missing))
        objs.setdefault(0, 0)
        # metatile 120 is also Xstep's diamond: it is the big saw's centre only where big saws are
        big_saw = bool(level_mts & BIG_SAW_PARTS)
        saw_mts = {m: t for m, t in SAW_METATILES.items() if m != SAW_CENTER_MT or big_saw}
        mts = sorted(level_mts | {0})
        mirror = has_mirror_portal(name)

        # quarters: (metatile, quarter) -> tile data, or ("saw", saw tile)
        def quarters(dmg):
            q = {}
            for m in mts:
                art = cat.dmg_art(objs[m]) if dmg else cat.art(objs[m])
                for i in range(4):
                    saw = saw_mts.get(m, (None,) * 4)[i]
                    q[(m, i)] = ("saw", saw) if saw is not None else gb_bytes(art[i])
            return q

        # CGB: saw quarters are drawn from VRAM bank 1 (the table entry is not used). A tile and its
        # flipped copies are one sheet tile: the quarter's flips (attributes) are in the level's
        # flip table, 2 bits per quarter (bits 2q, 2q + 1: X, Y), 1 byte per metatile
        cq = quarters(False)
        cflip = bytearray(256)
        for (m, i), d in list(cq.items()):
            if d[0] != "saw" and m not in GROUND_LOOK_MTS:
                cq[(m, i)], f = canonical(d)
                cflip[m] |= f << (2 * i)
        cslot = allocate([d for d in cq.values() if d[0] != "saw"], CGB_SLOTS, "%s (CGB)" % name)
        cgb_sheet = [bytes(16)] * SHEET_TILES
        for d, s in cslot.items():
            cgb_sheet[s] = d
        cgb_mt = bytearray([BLANK_TILE] * 1024)
        for (m, i), d in cq.items():
            cgb_mt[m * 4 + i] = BLANK_TILE if d[0] == "saw" else cslot[d]
        cgb_loads = [(0x81, SAW_VRAM_BASE, SAW_TILES, first["SAWS_0"])]

        # DMG: saw tiles after the level's own tiles, then in the shared sprite slots
        dq = quarters(True)
        dslot = allocate([d for d in dq.values() if d[0] != "saw"], DMG_SLOTS, "%s (DMG)" % name)
        used = set(dslot.values())
        saw_k = sorted({d[1] for d in dq.values() if d[0] == "saw"})
        saw_free = [s for s in DMG_SLOTS if s not in used] + DMG_SHARED_SLOTS + ([] if mirror else DMG_MIRROR_SLOTS)
        if len(saw_k) > len(saw_free):
            raise SystemExit("%s (DMG): no room for its %d saw tiles" % (name, len(saw_k)))
        saw_slot = dict(zip(saw_k, saw_free))
        dmg_sheet = [bytes(16)] * SHEET_TILES
        for d, s in dslot.items():
            dmg_sheet[s] = d
        dmg_rev = [mirrored_bytes(d) for d in dmg_sheet]
        for s, slot in saw_slot.items():
            if slot < SHEET_TILES:
                dmg_sheet[slot], dmg_rev[slot] = saw_extra(s, False), saw_extra(s, True)
        dmg_mt = bytearray([BLANK_TILE] * 1024)
        for (m, i), d in dq.items():
            dmg_mt[m * 4 + i] = saw_slot[d[1]] if d[0] == "saw" else dslot[d]
        dmg_loads = [(0, saw_slot[s], 1, first["SAWS_0"] + s) for s in saw_k]
        rest = [v for _, d, n, f in dmg_loads if d >= SHEET_TILES for v in (d, n, f)]

        col = bytearray(256)
        pal = bytearray(256)
        for m in mts:
            col[m] = cat.objects[objs[m]]["collision"]
            pal[m] = cat.objects[objs[m]]["palette"]
        levels.append(dict(lv=lv, name=name, cgb=stream(cgb_loads), dmg=stream(dmg_loads),
                           big_saw=big_saw, saws=any(m in saw_mts for m in level_mts),
                           saw_set=int(any(cat.objects[objs[m]]["name"].startswith("1.6_SAW") for m in mts)),
                           cgb_mt=bytes(cgb_mt), dmg_mt=bytes(dmg_mt), col=bytes(col), pal=bytes(pal), flip=bytes(cflip),
                           cgb_sheet=b"".join(cgb_sheet), dmg_sheet=b"".join(dmg_sheet + dmg_rev),
                           rest=rest))
        shared = sum(1 for s in saw_slot.values() if s >= SHEET_TILES)
        print("%-20s CGB %3d/%d tiles   DMG %3d/%d tiles + %d saw tiles (%d in sprite tiles)"
              % (name, len(set(cslot.values()) - {BLANK_TILE}), len(CGB_SLOTS),
                 len(used - {BLANK_TILE}), len(DMG_SLOTS), len(saw_k), shared))

    # sheets: one file per level (CGB, then DMG normal + mirrored), packed into free banks from the
    # top down (the #pragma bank numbers here are real banks; tools/build_levels.py allocates the
    # level maps from bank 30 up)
    for old in list(out_dir.glob("bg_sheet_*.bin")) + list(gfx.glob("bg_sheet_*.c")) + list(gfx.glob("bg_level_mt_*.c")):
        old.unlink()
    used_banks = set()
    for f in (ROOT / "src").rglob("*.c"):
        m = re.search(r'^#pragma\s+bank\s+(\d+)', f.read_text(errors="ignore"), re.M)
        if m:
            used_banks.add(int(m.group(1)))
    # ("#pragma bank 255" is not auto-banking here: those files all go to bank 255)
    space = {"bank": None, "room": 0}

    def bank_for(size):
        """a real bank with room for size bytes: the current one, else the highest free one"""
        if size > space["room"]:
            space["bank"] = max(b for b in range(2, 255) if b not in used_banks)
            used_banks.add(space["bank"])
            space["room"] = 16384
        space["room"] -= size
        return space["bank"]
    for L_ in levels:
        name = L_["name"]
        bank = bank_for(len(L_["cgb_sheet"]) + len(L_["dmg_sheet"]))
        (out_dir / ("bg_sheet_%s_cgb.bin" % name)).write_bytes(L_["cgb_sheet"])
        (out_dir / ("bg_sheet_%s_dmg.bin" % name)).write_bytes(L_["dmg_sheet"])
        (gfx / ("bg_sheet_%s.c" % name)).write_text("\n".join([
            "/* Generated by tools/build_bg_tiles.py - do not edit */",
            "#pragma bank %d" % bank, "#include <gbdk/platform.h>", "#include <gbdk/incbin.h>", "",
            "BANKREF(bg_sheet_%s)" % name,
            "INCBIN(bg_sheet_%s_cgb, \"levels/chr_data/bg_sheet_%s_cgb.bin\")" % (name, name),
            "INCBIN(bg_sheet_%s_dmg, \"levels/chr_data/bg_sheet_%s_dmg.bin\")" % (name, name), ""]),
            encoding="utf-8")
        L_["bank"] = bank

    # header
    idx = [0] * 256
    saw_rows = []
    for i, (m, tl) in enumerate(sorted(SAW_METATILES.items())):
        idx[m] = i + 1
        saw_rows.append("    { %s }, /* metatile %d */" % (", ".join(
            "0xFF" if t is None else "0x%02X" % (SAW_VRAM_BASE + t) for t in tl), m))
    H = ["/* Generated by tools/build_bg_tiles.py - do not edit */",
         "#ifndef BG_LEVEL_TABLES_H", "#define BG_LEVEL_TABLES_H", "",
         "#define SAW_VRAM_BASE %d" % SAW_VRAM_BASE,
         "#define SAW_MT_COUNT %d" % len(SAW_METATILES), "",
         "// CGB saw metatiles: saw_mt_index[metatile] = row in saw_mt_tiles + 1 (0 = not a saw);",
         "// the 4 tiles are in VRAM bank 1 (0xFF = keep the sheet tile)",
         "static const uint8_t saw_mt_index[256] = {"]
    for r in range(0, 256, 16):
        H.append("    " + ", ".join(str(v) for v in idx[r:r + 16]) + ",")
    H += ["};", "static const uint8_t saw_mt_tiles[SAW_MT_COUNT][4] = {"] + saw_rows + ["};", "",
          "// Per level load lists and the pointers to its tables and sheets: in bg_level_lists.c (their",
          "// own bank: only read when a level loads). Entry 2 * level is the CGB one, 2 * level + 1 the DMG one.",
          "#include <gbdk/platform.h>",
          "BANKREF_EXTERN(bg_level_lists)",
          "extern const uint8_t * const bg_level_lists[];",
          "extern const uint16_t bg_level_list_len[];",
          "extern const uint8_t * const bg_level_mt[];",
          "extern const uint8_t * const bg_level_col[];",
          "extern const uint8_t * const bg_level_pal[];",
          "extern const uint8_t * const bg_level_flip[];",
          "extern const uint8_t * const bg_level_sheet[];",
          "#define BG_LEVEL_COUNT %d" % len(levels),
          "#define SAW_CENTER_MT %d" % SAW_CENTER_MT,
          "// Saw animation: frame f of the saw tiles is bg_extra_tiles tile saw_frame_first[f] + 0..SAW_TILES-1",
          "// (CGB streams the 3 frames from saw_anim_tiles.bin: SAW_TILES tiles of 16 bytes per frame)",
          "#define SAW_ANIM_FRAMES %d" % SAW_FRAMES,
          "#define SAW_ANIM_TILES %d" % SAW_TILES,
          "static const uint8_t saw_frame_first[SAW_ANIM_FRAMES] = { %s };" % ", ".join(
              str(first["SAWS_%d" % f]) for f in range(SAW_FRAMES)),
          "// 1 = the level has saws (animate them)",
          "static const uint8_t bg_level_saws[BG_LEVEL_COUNT] = { %s };" % ", ".join(str(int(L_["saws"])) for L_ in levels),
          "// CGB saw art: 0 = Famidash, 1 = the 1.6 saws (saw_anim_tiles frames 3..5)",
          "static const uint8_t bg_level_saw_set[BG_LEVEL_COUNT] = { %s };" % ", ".join(str(L_["saw_set"]) for L_ in levels),
          "// 1 = the level has big saws, so metatile SAW_CENTER_MT is their centre (CGB)",
          "static const uint8_t bg_level_big_saws[BG_LEVEL_COUNT] = { %s };" % ", ".join(str(int(L_["big_saw"])) for L_ in levels)]
    for L_ in levels:
        r = L_["rest"]
        if r:
            H.append("static const uint8_t bg_sheet_rest_%s[] = { %s };" % (L_["name"], ", ".join(map(str, [len(r) // 3] + r))))
    H += ["static const uint8_t bg_sheet_rest_none[] = { 0 };",
          "// DMG: the level's tiles outside its sheet (saws in sprite tiles), loaded again after a mirror",
          "// portal: n, n x (slot, count, first bg_extra_tiles tile)",
          "static const uint8_t * const bg_dmg_sheet_rest[BG_LEVEL_COUNT] = { %s };" % ", ".join(
              ("bg_sheet_rest_%s" % L_["name"]) if L_["rest"] else "bg_sheet_rest_none" for L_ in levels)]

    C = ["/* Generated by tools/build_bg_tiles.py - do not edit */",
         "#pragma bank 255", "#include <gbdk/platform.h>", "#include <stdint.h>", "",
         "BANKREF(bg_level_lists)", "",
         "// Per level load lists:",
         "//   n, n x (VRAM bank (bit 7: never mirrored), slot, count, first bg_extra_tiles tile)"]
    for L_ in levels:
        C.append("static const uint8_t bg_cgb_%s[] = { %s };" % (L_["name"], ", ".join(map(str, L_["cgb"]))))
        C.append("static const uint8_t bg_dmg_%s[] = { %s };" % (L_["name"], ", ".join(map(str, L_["dmg"]))))
    C += ["", "const uint8_t * const bg_level_lists[] = {"]
    C += ["    bg_cgb_%s, bg_dmg_%s," % (L_["name"], L_["name"]) for L_ in levels]
    C += ["};", "const uint16_t bg_level_list_len[] = {"]
    C += ["    %d, %d," % (len(L_["cgb"]), len(L_["dmg"])) for L_ in levels]
    C += ["};", ""]

    # Per level tables (bg_level_mt_*.c, deduplicated): the metatile table (256 x 4 tiles) per
    # platform, the collision table and the CGB palette table. apply_level_bg_tiles copies them to
    # RAM: the engine has no global metatile tables.
    blobs, blob_of = [], {}

    def blob(data):
        if data not in blob_of:
            blob_of[data] = len(blobs)
            blobs.append(data)
        return blob_of[data]
    for L_ in levels:
        L_["k"] = (blob(L_["cgb_mt"]), blob(L_["dmg_mt"]), blob(L_["col"]), blob(L_["pal"]), blob(L_["flip"]))
    files, size = [[]], 0
    for b_, data in enumerate(blobs):
        if size + len(data) > 16000:
            files.append([])
            size = 0
        files[-1].append(b_)
        size += len(data)
    file_of = {b_: f for f, bs in enumerate(files) for b_ in bs}
    for f, bs in enumerate(files):
        F = ["/* Generated by tools/build_bg_tiles.py - do not edit */",
             "#pragma bank %d" % bank_for(16384), "#include <gbdk/platform.h>", "#include <stdint.h>", "",
             "BANKREF(bg_level_mt_%d)" % f, ""]
        for b_ in bs:
            F.append("const uint8_t bg_level_blob_%d[%d] = {" % (b_, len(blobs[b_])))
            for r in range(0, len(blobs[b_]), 32):
                F.append("    " + ", ".join(str(v) for v in blobs[b_][r:r + 32]) + ",")
            F.append("};")
        (gfx / ("bg_level_mt_%d.c" % f)).write_text("\n".join(F + [""]), encoding="utf-8")

    def ref(b_):
        return "bg_level_blob_%d" % b_, "BANK(bg_level_mt_%d)" % file_of[b_]
    C += ["// Per level tables (bg_level_mt_*.c): entry 2 * level is the CGB metatile table, 2 * level + 1",
          "// the DMG one; then per level its collision and CGB palette tables"]
    C += ["extern const uint8_t bg_level_blob_%d[];" % b_ for b_ in range(len(blobs))]
    C += ["const uint8_t * const bg_level_mt[] = { %s };" % ", ".join(ref(L_["k"][p])[0] for L_ in levels for p in (0, 1)),
          "const uint8_t * const bg_level_col[] = { %s };" % ", ".join(ref(L_["k"][2])[0] for L_ in levels),
          "const uint8_t * const bg_level_pal[] = { %s };" % ", ".join(ref(L_["k"][3])[0] for L_ in levels),
          "// CGB: per metatile the flips of its 4 quarters (2 bits each: X, Y), see build_mt_rows",
          "const uint8_t * const bg_level_flip[] = { %s };" % ", ".join(ref(L_["k"][4])[0] for L_ in levels), ""]
    C += ["// Per level sheets (bg_sheet_<level>.c): entry 2 * level is the CGB sheet, 2 * level + 1 the",
          "// DMG one (BG_BASE_TILE_COUNT tiles normal, then mirrored)"]
    C += ["extern const uint8_t bg_sheet_%s_cgb[], bg_sheet_%s_dmg[];" % (L_["name"], L_["name"]) for L_ in levels]
    C += ["const uint8_t * const bg_level_sheet[] = { %s };" % ", ".join(
        "bg_sheet_%s_cgb, bg_sheet_%s_dmg" % (L_["name"], L_["name"]) for L_ in levels), ""]
    # the banks: here in the header (SDCC only takes BANK() in an initializer whose first element
    # is a plain constant, and only in a static one: entry k at [k + 1])
    H += ["// banks of bg_level_mt / bg_level_col / bg_level_pal / bg_level_sheet (bg_level_lists.c), entry k",
          "// at [k + 1]: SDCC only takes BANK() in an initializer whose first element is a plain constant"]
    H += ["BANKREF_EXTERN(bg_level_mt_%d)" % f for f in range(len(files))]
    H += ["BANKREF_EXTERN(bg_sheet_%s)" % L_["name"] for L_ in levels]
    H += ["static const uint8_t bg_level_mt_bank[1 + 2 * BG_LEVEL_COUNT] = { 0, %s };" % ", ".join(
              ref(L_["k"][p])[1] for L_ in levels for p in (0, 1)),
          "static const uint8_t bg_level_col_bank[1 + BG_LEVEL_COUNT] = { 0, %s };" % ", ".join(ref(L_["k"][2])[1] for L_ in levels),
          "static const uint8_t bg_level_pal_bank[1 + BG_LEVEL_COUNT] = { 0, %s };" % ", ".join(ref(L_["k"][3])[1] for L_ in levels),
          "static const uint8_t bg_level_flip_bank[1 + BG_LEVEL_COUNT] = { 0, %s };" % ", ".join(ref(L_["k"][4])[1] for L_ in levels),
          "static const uint8_t bg_level_sheet_bank[1 + 2 * BG_LEVEL_COUNT] = { 0, %s };" % ", ".join(
              "BANK(bg_sheet_%s), BANK(bg_sheet_%s)" % (L_["name"], L_["name"]) for L_ in levels),
          "", "#endif", ""]
    (gfx / "bg_level_tables.h").write_text("\n".join(H), encoding="utf-8")
    (gfx / "bg_level_lists.c").write_text("\n".join(C), encoding="utf-8")
    (ROOT / "include" / "bg_tiles.h").write_text("\n".join([
        "/* Generated by tools/build_bg_tiles.py - do not edit */",
        "#ifndef BG_TILES_H", "#define BG_TILES_H", "",
        "// Tiles in a level's sheet (VRAM tiles 0..BG_BASE_TILE_COUNT-1)",
        "#define BG_BASE_TILE_COUNT %d" % SHEET_TILES, "", "#endif", ""]), encoding="utf-8")
    print("%d levels, %d table blobs, %d extra tiles" % (len(levels), len(blobs), n_extra))


if __name__ == "__main__":
    main()
