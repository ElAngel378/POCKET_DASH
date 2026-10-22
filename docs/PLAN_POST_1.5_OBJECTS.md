# Plan: objects past 1.5 (own object catalog, split from Famidash)

Status: planning only, nothing implemented yet (agreed in principle 2026-10-20).

## The problem

Famidash (and Pocket Dash, which converts its levels) draws every level with one of a few fixed
tile sets: blocks A-D, spikes A-C, a saw set and a slope set. Its 1.6+ levels (Electroman
Adventures, Clubstep, Electrodynamix, ...) have their 1.6 objects downgraded to 1.5 art. The goal
is to ADD the 1.6+ objects on top of the existing ones, not replace them: real GD has no tile
sets, so any object can appear next to any other.

Example art: a 1.6 blocks / saws / decorations CHR (512 slots, 132 tiles used) the user shared on
2026-10-20.

## Why we can't just add tiles today

- Metatile ids are one byte, global for the whole game: all 256 are defined, 209 are used.
- One background layer: a decoration on top of a block has to be its own metatile.
- Every level shares a small number of tile slots (DMG: Clubstep had 0 free after its fake
  spikes got their own tiles).

GDA (Geometry Dash Advance, GBA) gets around this with 16-bit block ids, 2+ background layers
and one ~1000 tile tileset that is always loaded. The GBA has the VRAM for that; the GB does not.

## The plan

1. Our own object catalog: every block / spike / decoration with its art, its collision and a
   DMG fallback object. A Tiled tileset (.tsx) is generated from it.
2. Our own TMX format using that tileset (no longer Famidash compatible). A one-time converter
   turns the existing Famidash TMXs into it (every Famidash metatile is a catalog object), so the
   current levels are kept. Cost: Famidash level updates no longer come over automatically.
3. A per-level build step: read the TMX, collect the objects used, give them level-local
   metatile ids, collect their 8x8 tiles (deduplicated, with CGB flips), allocate VRAM slots and
   write the level's metatile table, collision table and tile load list. Fail with a clear
   report when a level does not fit ("CGB 187/210 tiles, DMG over by 23").
4. Engine: per-level metatile table (mt_ram already is per level) and a per-level collision table
   (256 bytes) instead of the global famidash_metatile_collision.

First milestone: the new pipeline builds every existing level identically (no visual change).
Only then add the 1.6 art, Clubstep first (the most 1.6 objects), then Electroman Adventures.

## Work stages (branch object-catalog)

### Stage A: per-level tables in the engine (no visual change) - done 2026-10-22

- tools/build_bg_tiles.py bakes every level's final metatile table per platform (DMG tile moves
  and the saw / spike set / fake spike tile substitutions applied) into src/graphics/
  bg_level_mt_*.c, deduplicated, plus the collision and CGB palette tables. Pointers per level in
  bg_level_lists.c, banks in bg_level_tables.h.
- apply_level_bg_tiles copies the level's metatile table to mt_ram, its collision table to
  level_mt_col (RAM, replaces mt_xlat: no RAM growth) and builds bm_info from its palette table.
  The runtime metatile overrides and mt_xlat are gone; the global tables are no longer compiled
  (bank 0 freed ~1.5 KB). include/famidash_metatiles_dmg.c stays as the tool's input.
- Engine code still knows some metatile ids (saw parts 0x00..0x0B / 0x74..0x7F / 120, ground
  looks 1, 2, 5, 6, 136, 137, pit spikes 12, 24): stage B keeps those objects on those ids.

### Stage B: the object catalog (no visual change) - done 2026-10-22

Status: levels/objects/ holds the catalog (283 objects + 71 DMG-only art tiles, see its
README.md for editing it), every TMX uses it (tools/convert_tmx_to_catalog.py, maps and sprite
lists byte-identical), and tools/build_bg_tiles.py builds each level's own CGB / DMG tile sheet
from the art of the objects it uses (the base sheet, the Famidash tile sets, the DMG tile moves and
the runtime fake spike tiles are gone). Level ids: tools/objects_catalog.py assign_level_ids
(famidash_id when there is one, else a free id), so new objects (famidash_id = -1) work.
Checked: the visible background of every level, DMG and CGB, rendered from VRAM at 13 points
each, against the stage A build.

Design notes as planned:

- levels/objects/objects.tsx + objects.png: a Tiled tileset, one 16x16 tile per object, 4 grey
  levels = colour indices (like bg_tiles.png). Per-tile properties: name, collision, CGB palette,
  dmg_art (an extra DMG-only tile of the tileset, e.g. outlined saws, set B blocks), dmg_fallback
  (another object), fixed_id (the engine ids above), saw_part (animated saw objects).
- Generated once from today's data: every distinct (Famidash metatile, its art in a level) is
  an object; set B / C levels get their own objects for the same Famidash id.
- TMX: `<tileset firstgid="1" source="../../objects/objects.tsx"/>`, sprites tileset after the
  catalog. A one-time converter rewrites the existing TMXs (Famidash gid + the level's sets ->
  catalog gid).
- Per-level build: the level's objects get local ids (fixed_id ones first; keeping today's ids
  where possible keeps the maps byte-identical), their 8x8 tiles are cut from the art, a fully
  transparent quarter becomes the reserved blank tile 12, duplicates (and, on CGB, flipped
  copies) are merged, VRAM slots are allocated, and the level's tables + tile loads are written.
  This replaces the fixed 160-tile sheet + LEVEL_SETS loads.
- Check: tools/visual_regression.py, every level, DMG and CGB, identical frames.

## Limits per level (two separate ones)

- Object kinds (metatiles: 16x16 pieces; every orientation, edge and corner variant counts, and on
  CGB every recolour too). The current format stores one byte per map cell: 256 kinds per level.
  The new format uses TWO bytes per cell from the start (65536 kinds): maps double in size (fine,
  the ROM is 4 MB), the per-level object table grows (512 kinds x 8 bytes = 4 KB: CGB WRAM banks
  1..7; DMG keeps a smaller table, it uses fallbacks anyway), and drawing a screen gets a bit
  slower.
- Unique 8x8 tiles in VRAM. The background sees 256 tile slots per VRAM bank:
  - bank 0: 96 are sprite art (portals, orbs, chain, ...), 160 are the level sheet
  - bank 1: parallax background 48, ground 9, saws 52, sprite tiles and coins: ~50 free
  - so ~210 per level on CGB today. Objects share tiles, and CGB flips them (a symmetric block
    often needs 1-2 unique tiles instead of 4).
- CGB background palettes: 8, some already taken (ground, background, text).

### Raising the tile limit (CGB)

1. Move the sprite art out of bank 0: CGB sprites can use bank 1 too. Frees up to 96 bank 0
   slots: ~300 per level.
2. Tile streaming: load an object's tiles as it scrolls on screen and reuse the slots of tiles
   that scrolled off. CGB HDMA copies 100+ tiles per frame while the screen draws, so the limit
   becomes "unique tiles on one screen at a time" instead of "in the whole level". Biggest
   change; it removes the ceiling in practice.

DMG can do neither (one VRAM bank, a few tiles per frame by CPU copy: its saw animation is
already spread over frames), so DMG stays on fallback objects.

### Measured: GDA's Clubstep (2026-10-20)

geometry_dash_advance/levels/clubstep.json (899 x 54 cells, two tile layers l1 / l2), with GDA's
src/metatiles.c (8672 metatiles); sprites (SP layer) not counted:

- object kinds: 111 (l1 101, l2 35). On one layer every l1 + l2 combination in a cell is its own
  kind: 164. Under 256 either way.
- unique 8x8 tiles in the whole level: 106 with flips free (CGB), 249 when every flipped copy is
  its own tile (DMG).
- most unique tiles on one Game Boy screen (11 x 10 cells): 45 (CGB), 94 (DMG).

So a 1.6-accurate Clubstep fits the ~210 CGB budget even without streaming (106 + saws), and
streaming would leave lots of room (45 per screen). DMG would need 249 tiles for the full art:
over its ~144 slots, hence the fallbacks. Caveat: GDA's tiles are 16 colours (GBA 4bpp); redrawing
them in 4 colours may split some tiles, so the GB numbers will be somewhat higher.

## DMG

DMG has ~144 slots, no flips and no bank 1: 1.6-heavy levels will not fit with full art. Each
catalog object names a DMG stand-in (usually the 1.5-style object drawn today, or simplified art).
Collision always stays the real object's, so the level plays the same; only the art is
downgraded. The DMG build swaps in stand-ins until the level fits (saws and fake spikes already do
this by hand). Later option for very heavy levels: swap tile sets mid-level at a portal, like the
DMG mirror portals reload the sheet.
