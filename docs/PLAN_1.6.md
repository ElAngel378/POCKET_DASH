# Plan: GD 1.6 levels on Game Boy

Branch base: `clean-1.5` (from `23667e2`, before the old object catalog). Replaces the old
`PLAN_POST_1.5_OBJECTS.md`, which is not on this branch.

## Goal

Electroman Adventures and Clubstep play as their GD 1.6 versions, not Famidash's 1.5 downgrades.
To get there the game drops Famidash's way of building levels: like GD, any object can be placed in
any level. No per-level tilesets (Famidash's block set B, spike sets B and C), no 256-kinds-per-level
or 210-tiles-per-level limit.

## What we know (the facts this plan stands on)

- **The object list is fixed and small.** GD 1.6 added IDs 142–199 (57 objects). The 14 official
  levels use 171 different IDs in all, none above 199. Source: GD's own data on this PC (IDs,
  textures, per-level counts). Full table: `docs/GD_OBJECT_INVENTORY.md`.
- **Famidash already covers most of it.** Of the 171: 81 it has, 12 approximated, 28 missing,
  36 have your art but no object yet, 14 are engine effects (enter effects, ghost trail).
- **Your art** (the 9 `.chr` files; Clutterfunk Blocks, Wavy Gspikes, y did i do this and
  sopeslopeslopeslope are NES format, the rest GB) covers the 1.6 blocks, outlines and rails, waves
  and saws, plus clouds 1–2, the pulsing ring, heart and star, and ToE's stepped ground hills.
  Still to draw: ice spikes, black saws, spiked and cart wheels, spike art, chain art (1.6), and
  1.5's thorns, gear rings, cogwheels, clouds 3–5, chains.
- **GD places objects anywhere**, rotated in 90° steps, flipped and stacked (Electroman: 5643
  objects, 3468 of them off the block grid). Famidash's half spikes, half orb rings, combined
  "spike on slab" pieces and quarter blocks are all workarounds for that.
- **The hardware can stream.** Electroman's streaming test showed: a screen needs at most ~190
  tiles, the whole level ~1300; uploading each column's new tiles during VBlank works on CGB.
  Traps already found: WRAM is full (use `scratch_buf`), coin tiles sit in bank-1 tiles 200–221,
  uploads must re-check the scanline before each DMA.

## Design

1. **One object table for the whole game, keyed by GD ID.** Each entry: name, GD ID, art (one or
   more 16×16 cells, at 0 or 8 px offsets), collision (GD's hitbox), palette. Famidash's metatiles
   become entries in it (its bricks and spike sets as objects of their own). Pieces that only
   existed as workarounds (half spikes, combined pieces) are not objects any more.
2. **Levels are objects placed on an 8 px grid, stacked.** What a 16×16 cell shows is built from
   the objects over it. Collision comes from the objects' hitboxes, kept apart from the look (like GD).
3. **Tiles stream per column**, for every level: a cell's look is built at build time, and the
   tiles a screen needs are loaded as the level scrolls. No per-level tile limit.
4. **Levels come from somewhere exact.** Converting Famidash's maps to the new format must give the
   same levels (checked screen by screen). 1.6 levels are built by hand, with GD as the reference.

## Phases

Each phase is its own branch. You test the ROM before the next one starts; nothing big starts
without your OK.

### Phase 0 — The object table, no visible change
- Object table with GD IDs, Famidash's art imported into it.
- Converter: Famidash maps to the new level format.
- Check: every level converts, and the converted level rebuilds to the same tiles and collision.

**Done (branch `phase0-objects`).** `tools/gd_objects.py` is the table (100 objects: 28 GD ones,
the rest Famidash's own pieces, keyed `fd_*`); `tools/level_objects.py` converts, compiles and makes
the Tiled tileset `levels/chr_data/objects/gd_objects.tsx`. Every level's TMX is converted; the
build compiles them back to byte-identical maps (the ROM's level data did not change).

How a level is built now, in Tiled:
- The map grid stays 16 px. Pick an object in the `gd_objects` tileset and click: one click per
  object, whatever its size (a big saw is one 48×48 tile).
- Rotate / flip with Tiled's buttons (Z, X, Y).
- Stacking: more tile layers. Half a block off the grid: a layer with Offset X or Y = 8 (Layer
  Properties). The converter named them `Objects +8x`, `Objects +8y`, ...; any names work, only the
  `SP` layer is special (sprites, as before).
- Objects bigger than a block are on `Layer 1` layers under the others: where a block and a saw
  overlap, the block shows (in game too).
- `template.tmx` has all these layers ready (big objects and objects, each on the grid and +8 px
  in x, y and both, a second objects layer, SP): a new level starts from it. Keep it that way when
  a phase adds a kind of layer.
- Until phase 1 a cell must still come out as one of Famidash's metatiles: the build stops with the
  cell's position if a combination has none.

### Phase 1 — Streaming renderer for all levels, no visible change

Design (2026-11-05):
- Measured with Famidash's art: a level uses at most 155 different tiles (Clubstep), a 24-column
  window at most 94. Nothing needs streaming yet; the 1.6 art will (Electroman's GDA test: ~1300).
- Each level gets its own tile pool: the tiles its metatiles show, per platform (CGB / DMG art,
  DMG fake spikes, block / spike set art: Famidash's tile sets end here). No shared base sheet.
- The renderer keeps drawing metatiles through the per-level RAM table (`mt_ram`); its hand-written
  loops do not change. The build gives each map column a list of records "tile T into VRAM slot S";
  applying one points the metatile quarters that show T at S (both orientations).
- A tile keeps its slot while any column in the 16-column VRAM ring uses it, so a slot is only
  reused for tiles of columns that left the ring (off screen, and row jobs read the ring only).
  The slots are assigned by the build, so the game only follows the lists.
- Uploads go through the VBlank handler: CGB by GDMA from ROM, DMG copied while VBlank lasts,
  like the saws. A column is drawn ~3 columns before it scrolls into view, so its tiles have time.
- Practice checkpoints and respawns: the records are replayed from the start (RAM only) to rebuild
  which tile is in each slot, then the live slots are uploaded with the display off.
- Kept as they are: saws (CGB VRAM bank 1, animated; DMG fixed slots), the ground strip, the
  parallax (tile 12 stays "transparent"), tile 26 (black fill).
- Slots: CGB bank 0 BG tiles 0..159, DMG 0..143 (144..159 are the coin sprites), minus 12, 26 and
  the DMG saw slots.

**Done (branch `phase1-streaming`).** `tools/bg_stream.py` (pools, slots, records, a simulator that
checks every column), `src/graphics/bg_stream.c` (runtime), the VBlank upload in
`bg_parallax_phases.c`. The build moves each record as early as its slot allows, so a level whose
tiles all fit loads them before it starts (every level so far, except one Clubstep DMG tile): no
streaming work while playing. `BG_STREAM_SLOTS=80` (environment, when building) caps the slots so
levels stream while they play: Xstep (~190 tiles loaded in play) matched the old ROM frame for
frame. Checked against the old ROM on all 22 levels, DMG and CGB (Mesen): same pictures; the saws'
spin, the pulsing decorations and a few dropped frames shift slightly because level loading takes a
different time now.

### Phase 1 tasks
- Per-column tile streaming (from the Electroman test) for every level; drop the per-level sheets
  and Famidash's tile sets.
- Check: all levels look and play the same as before, on CGB and DMG; no lag added.

### Phase 2 — Placement like GD

**Done (branch `phase2-placement`).** A cell no Famidash metatile shows (any stack, any 8 px offset)
becomes a metatile of the level's own (`tools/compose.py`): its look is composed from the objects'
art (solid objects and fills opaque, the others show what is behind them through colour 0), its
palette is the solid / deadly object's, its collision the Famidash metatile's when the solid /
deadly objects make one, else per 8x8 quarter (half blocks as slabs). It takes a metatile id the
level does not use, with that palette; the ids the engine treats specially are never reused. Each
level has its collision table (`src/level_collision.c` for levels with their own metatiles), copied
into `scratch_buf` while it plays. `test.tmx` shows examples. Not yet: saws and the ground strip
sharing a cell with other objects, orbs / pads sharing a cell with solid objects, two palettes in
one cell.
- 8 px offsets and stacking; the workaround pieces replaced by the real objects.
- Check: levels still look and play the same.

### Phase 3 — Fill the 1.5 gaps
- The missing 1.5 objects (ground hills, thorns, gear rings, clouds, cogwheels, chains), with your art.
- Theory of Everything gets them first.

**Done (branch `phase3-objects`).** 29 objects added, all `DRAWN` in `tools/gd_objects.py` (no
collision, the background decoration palette), their art in
`levels/chr_data/objects/art/gd<ID>.png` (GB colours 0–3, 0 = see-through): draw over a file and
rebuild, nothing else to change. `tools/import_gda_art.py` made the files.
- Your art: clouds 48, 49, ToE's stepped ground hills 113–115.
- Lines (nothing to draw): Clutterfunk's 76–78, 81, 82. Its dark fill 80 is Famidash's black fill.
- GDA's art for now (your call, 2026-11-05): cogwheels 85 (a quarter wheel, as in GD) / 86 / 87 /
  97, chains 106 / 107 / 110, thorns 123–128, clouds 129–131, gear rings 137 (half a ring, as in
  GD) / 138 / 139.

Each level with metatiles of its own now has its own CGB palette table too
(`src/level_collision.c`, copied into the renderer's RAM attribute table), so they can take any free
metatile id. `test.tmx` shows them all. Like every background object, a decoration hides the
parallax behind it within its 8x8 tiles (colour 0 is the palette's own colour).

Fixed on the way: the VBlank tile upload only checked LY < 152, so a long handler (heavy frames,
jumping) ran it after line 153 with VRAM locked and lost tiles. It now needs 144 <= LY < 152. Test
in-play streaming with the cube jumping: rows and the cube spin are what make frames heavy.

Left for phase 4: the pulsing objects 51, 52, 54 (your art), cogwheels and rings spinning.

### Phase 4 — 1.6 objects
- Import your 1.6 art; draw the 15 missing.
- Mechanics, one at a time: breakable bricks, fading objects, rotating decorations.

**In progress (branch `phase4-objects`).** 25 objects in from your art (`DRAWN`, `DRAWN_KIND` says
which collide): blocks 170, 171, 173–176, 192, 194, 197, fills 164, 193, breakable brick 143 (solid,
does not break yet), wave hills 157–159, outlines 161–163, 165–167, 169 (no collision yet), saws
186 (a quarter), 187, 188 (deadly by 8x8 quarter, not spinning yet). Levels can have any number of
tiles: tile numbers are 16 bits in the records and all levels' tiles are one space over consecutive
banks (`bg_stream_tiles_<n>.c`). Streaming never makes the game wait: the build limits what a column
brings in play (`PER_COLUMN`: 4 tiles CGB, 2 DMG, loading earlier when slots allow), the next column
waits a little while tiles back up, and backed-up tiles go before the DMG cube frame in VBlank. Test
level: no stall of 2 frames or more, CGB and DMG, running or jumping.

The 15 objects without art are in with GDA's art for now (your call, 2026-11-06; `import_gda_art.py`):
spiky art 151–153, spiked wheels 154 (a quarter), 155, cart wheels 180 (a quarter), 181, 182, chain
art 190 (decoration, all in the player colour: colour 3 of palette 2); ice spikes 177–179, black saws 183 (a quarter), 184, 185 (deadly). 178 is GDA's
spike squashed: GDA has no low wide one. Every object of the official levels now has art. GD's 1.0
deco spikes 18–21 stay sprites 46–49.

Spinning (CGB; DMG shows frame 0): `GO.SPIN` lists the objects, `level_objects.spin_frames` rotates
their art by a part of their rotational symmetry (3 frames), `compose.spin_look` composes every cell
in each frame, so overlaps spin too. Tiles that change get pinned BG slots from 28 for the whole level
(at most 52, from the left of the level; the rest stands still). `bg_stream_spin` uploads the next
frame in chunks of 13 tiles through the saws' VBlank DMA, while the saws are idle. It pauses during
palette fades (no VBlank time left). Quarter pieces must be rotated in Tiled, not flipped. An object spins whole or not at all; the build log says which got no room.

Colours as GDA's: most decorations player colour 1 (palette 2), rings, cart wheels and chains 106, 107,
190 player colour 2 (palette 6, `DRAWN_P2`), hazards object colour, white (palette 0). The empty
quarters of a level's own metatiles are records now (the old list's count was one byte).

Builds: `tools/keep_mtimes.py` gives unchanged generated files their old time back, so make only
compiles what changed.

Hitboxes: ice spikes collide like spikes (`HIT_LIKE`), saws by GDA's circles, as the 8x8 quarters
inside them (`HIT_RADIUS`, GDA's radii). No new engine code.

Fading objects 144–146 (`GO.FADE`, GDA's art, the block hollow): as GDA draws them, by the cell's
place on the screen: full at the edges, faded 43–127 px, full again behind the player. They stay
solid / deadly. A cell with one has a second metatile: on CGB the same tiles in palette 7 (palette 0
three quarters of the way to the sky), on DMG without the object. The cells take a block of ids with
the faded looks right after them, so the game needs no table. `fade_update` (`bg_stream.c`) finds one
ring slot a frame at the wrong look, and the VBlank handler rewrites its cells from a short list
(`fade_cells_copy`, attributes on CGB, tiles on DMG). Nothing depends on the path taken: checkpoints
and respawns just ask where a column is. One faded step, not GDA's three: palette 7 is the only one
free (palettes 5, 6: fake spikes, player colour 2). Demo: level `test2`. Mirror portals and the pause
box with faded cells on screen are not tested yet; on the level complete screen palette 7 is text.

Breakable bricks (gd143, collision COL_BREAKABLE): solid, broken by the wall probe that would kill
the player on a block (GDA breaks them with the centre hitbox); only on the grid, off it a plain
solid block. The last 8 broken cells are kept (bg_stream.c) and every map fetch reads them as
empty; the broken cell is redrawn through the fading cell list on the next VBlank. A cell a brick
shares breaks whole (build warning). They come back on a respawn. Demo: test2, after the fading
objects. The old port (9fbfc27) rewrote the whole column a few frames later and lost a brick when
two broke close together.

Pulsing: the ring and heart (your GDP_Pulsing_Obj.chr) as decoration sprites, engine ids 65, 66.

GDA comparison done 2026-11-07 (tools/gda_compare.py, test and test2 built in GDA): colours match
after moving clouds 1-2 and the pulsing shapes to player colour 2. Known differences, accepted:
the fade zone is scaled to our screen, not placed relative to the cube as GDA; colour triggers
fire a little earlier; 1.6 block pieces and light lines have no exact GDA object.

**Phase 4 done** (user approved 2026-11-07). Left over: black spots on some saws, untested
cases (fading / bricks with mirror portals, pause, level complete).

To do (found by the user after the push): palettes of the background pieces GDA draws in
background shades. 1.6 blocks and fills (170-197, 164, 193): GDA light blue / navy from the BG
colour, ours white / black. 1.6 outlines (161-169), light lines (76-82), wave hills (157-159):
GDA LBG (light BG), ours white or player colour 1. The GDA check compared palette numbers that
mean different things (GDA 2 = LBG, ours 2 = player colour). Idea: use the parallax palette (3),
which has lighter / darker BG shades, with the art remapped; test on CGB and DMG.

To do (user, 2026-11-08): the Tiled sprite sheet (levels/chr_data/graphics/sprites.png) as an
edited copy of Famidash's: a cross over every sprite our game does not have, and our own art in
the cells where Pocket Dash differs from Famidash or Famidash's is a placeholder (like the
pulsing ring and heart now, and the mirror portals), so Tiled shows what the game draws.

Order agreed 2026-11-07: spinning, hitboxes, fading, breakable bricks, pulsing last. Fading and
breakable bricks exist in the old code (main's history, gda-objects): review that it is good and worth
taking before porting it.

Check at the end of phase 4: the same test level built in GDA, compared with ours (palettes above
all, and that objects show up as they should). GDA's 1.6 objects are the same as GD's.

### Phase 5 — Electroman and Clubstep as 1.6
- Built by hand in Tiled with the full object set, GD's levels as the reference.

## Decisions (2026-11-02)

1. **DMG streams too.** Phase 1 has to fit it in DMG's VBlank (no DMA): fewer tiles per frame,
   so the column records may need more lookahead.
2. **Levels are built by hand** (you, maybe with a friend), in Tiled, with GD as the reference.
   No automatic conversion of 1.6 levels. Phase 5 is level building, not a converter.
3. **Enter effects** (objects sliding / fading in at the screen edges) and the ghost trail: their own
   work, later.
4. **Art:** the missing objects (`docs/missing_art.png`) get redrawn by you in 8 bit. GDA's art may
   be borrowed for these objects only; nothing else comes from GDA.
5. **Spinning deco** (GD's rotating objects up to ID 199: 85–89, 97, 98, 137–139, 154–156,
   180–188; 88, 89, 98 and 186–188 are saws): background tiles, not sprites, animated by swapping
   the tile graphics (3–4 pre-rotated frames, one tooth step apart), so every copy turns together
   at no extra cost. Same upload path as the column streaming. On DMG, slower updates or static if
   VBlank is tight. Sprites only for objects that actually move.

## After that

Make building levels easier (building, compiling, testing in game): many people in the community
have trouble with it.
