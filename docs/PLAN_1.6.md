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

### Phase 1 — Streaming renderer for all levels, no visible change
- Per-column tile streaming (from the Electroman test) for every level; drop the per-level sheets
  and Famidash's tile sets.
- Check: all levels look and play the same as before, on CGB and DMG; no lag added.

### Phase 2 — Placement like GD
- 8 px offsets and stacking; the workaround pieces replaced by the real objects.
- Check: levels still look and play the same.

### Phase 3 — Fill the 1.5 gaps
- The missing 1.5 objects (ground hills, thorns, gear rings, clouds, cogwheels, chains), with your art.
- Theory of Everything gets them first.

### Phase 4 — 1.6 objects
- Import your 1.6 art; draw the 15 missing.
- Mechanics, one at a time: breakable bricks, fading objects, rotating decorations.

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
