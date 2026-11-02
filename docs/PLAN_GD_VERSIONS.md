# Plan: GD's levels, version by version

Goal: GD's levels running in Pocket Dash the way GD does it, starting with 1.6 (Electroman
Adventures and Clubstep). Any object can be placed in any level, like GD's editor. We follow GD's
update order (1.0-1.4 gaps, 1.5, 1.6, then 1.7, 1.8 ...), not Famidash's: Famidash only had part of
1.5, and its per-level tilesets and one-object-per-cell workarounds are not carried over.

Reference: `docs/GD_OBJECT_INVENTORY.md`, every object the 14 official levels use, with Famidash's
equivalent and a status (have / approximated / missing / your art exists / engine feature).

## Why the last attempt failed (do not repeat)

- **The tileset grew out of control:** 880 catalog objects, GDA pieces imported automatically, a copy
  per 8 px offset and flip, combined pieces, duplicates.
- **Automatic conversion from GDA's maps:** misaligned layouts and mixed 1.5 / 1.6 pieces, fixed one
  by one without end. GDA is a port, not the source.
- **The tile limit** (159 CGB / 143 DMG tiles per level) forced one workaround after another.
- **Big systems went in without being asked for or tested step by step.**

## Rules

1. Small steps. Each step ends with a ROM the user tests; the next step starts after their OK.
2. No new system, tool or large rework without the user's go-ahead first.
3. GD is the reference: object IDs, art, positions and behaviour come from GD (the user's GD
   install), not GDA and not Famidash.
4. One object = one GD object ID. No offset or flip copies, no combined pieces: stacking and
   offsets are the engine's job.
5. The object list is shared by every level. No per-level tilesets.

## Step 0: Engine foundations

Everything after this needs it. Nothing here changes how existing levels look; they must play and
look exactly as before (checked with `tools/visual_regression.py`).

1. **Object list.** One file listing our objects: GD ID, name, art (16 x 16 cells cut from the
   user's art), collision kind, palette. Replaces Famidash's fixed 256-metatile set as the source.
   The existing Famidash metatiles map onto it (the inventory's "have" column).
2. **Column streaming (CGB).** Tiles are loaded into VRAM column by column as the level scrolls, so a
   level is limited by what one screen shows, not by its total number of tiles. The renderer draws
   a "look" map; collision stays a separate map. (Proven on the old branch: look_stream, see
   memory notes; rebuilt cleanly here, verified with VRAM dumps.)
3. **Stacking.** A cell can show several objects drawn over each other (block + outline + glow,
   decoration behind a block). The build composites them into one cell picture.
4. **8 px offsets and flips.** Objects can sit half a cell over and be flipped / rotated by 90°, as
   in GD 1.x. Also done at build time, into the cell pictures.
5. **DMG.** DMG keeps a fixed tile set per level, so it gets a reduced look (the objects that fit,
   decorations dropped first). Decide per step what DMG shows; it must stay playable and readable.

Done when: an existing level is drawn through the new path and looks identical, and a test level
with stacked and offset objects shows them correctly on CGB.

## Step 1: 1.0-1.4 gaps

Missing or approximated (see the inventory):
- cogwheel decorations (85-87, 97): Clutterfunk, Theory of Everything, Electroman, Clubstep
- pulsing ring (51) and heart (52)
- clouds (48, 49) as GD has them
- Clutterfunk's light lines (76-82) and dark square (80), which Famidash patches
- enter effects (22-28, 56-59) and ghost trail (32, 33): engine features, decide whether to do them

Test levels: Clutterfunk, Cycles, xStep.

## Step 2: 1.5

Missing: stepped ground hills (113-115), thorns (123-128), gear rings (137-139), the other clouds
(129-131), chains with bases (106, 107, 110). Have (now objects of their own): Theory of
Everything's brick set (116-122) and bush spikes (135).

Test level: Theory of Everything, with all of its decorations.

## Step 3: 1.6

Objects (IDs 142-199):
- **Your art exists:** the 1.6 block set (170-176, 192-194, 197), outlines and glow lines (160-169),
  wave hills (157-159), saws (186-188), breakable brick (143), fading objects (144-147: the normal
  pieces, drawn fading), pulsing square / triangle / X (148-150).
- **Art needed:** ice spikes (177-179), black saws (183-185), spiked wheels (154-156), cart wheels
  (180-182), spike art (151-153), chain art (190), small block / slab (195, 196).
- **Have:** secret coin (142), fake spikes (191, 198, 199).

Mechanics: breakable bricks (Electroman), fading objects (Clubstep), new hitboxes (ice spikes,
saws, small block and slab).

Test levels: Electroman Adventures, Clubstep.

## Later

1.7 (speed portals, IDs 200+), 1.8 (slopes), 1.9, 2.0 ... in the same way: inventory first, then
objects and mechanics, then the levels.

## Open decisions

1. **How levels are made.** (a) Keep editing our TMX maps in Tiled, now with stacking layers and the
   new objects. (b) Build each level from GD's own level data (the user's GD install has the
   official levels: object IDs, positions, rotation, flips), mapped object by object, with Tiled
   for corrections. (b) gives exact layouts and decorations without hand work; it is a converter,
   so it must be checked level by level. GD's positions are free (3,468 of Electroman's 5,643
   objects are off the 30-unit grid): they round to the nearest 8 px.
2. **Collision.** From our objects' hitboxes (as now), or GD's hitboxes per object. Matters most for
   off-grid hazards.
3. **What DMG shows** in levels with many objects.
4. **Art style:** whose art for the missing objects (the user's, or a placeholder cut from GD's
   textures until then).
