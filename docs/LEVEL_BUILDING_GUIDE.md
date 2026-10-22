# Level building guide

How to make a Pocket Dash level in Tiled, from an empty map to playing it on a Game Boy.

## What you need

- [Tiled](https://www.mapeditor.org/) 1.10 or newer
- Python 3 with Pillow (`pip install pillow`), for the build tools
- The game's normal build setup (GBDK, `make`)
- Optional: [Furnace](https://tildearrow.org/furnace/) for the level's song

## Where things are

| path | what |
|---|---|
| `levels/chr_data/tmx/<level>.tmx` | the levels, one Tiled map each (edit these) |
| `levels/chr_data/tmx/template.tmx` | an empty level to start from |
| `levels/objects/objects.tsx` | every block, spike and decoration (the object catalog, see its `README.md`) |
| `levels/objects/sprites.tsx` | portals, orbs, pads, coins, triggers (the sprite layer) |
| `levels/metadata/*.json` | per level start colours, max fall speed, sprite position fixes |
| `music/Furnace/*.fur` | the songs |
| `tools/build_levels.py` | turns all of the above into game data |

Everything under `src/levels/`, `src/sprites/`, `src/graphics/bg_*` and `levels/level_data/` is
generated: don't edit it, it is overwritten on every build.

## 1. Make the level

1. Copy `levels/chr_data/tmx/template.tmx` to `levels/chr_data/tmx/<name>.tmx`. Use a short
   lowercase name without spaces (`mylevel.tmx`): it is the level's id everywhere.
2. Give it a place in the game: add a line to `KNOWN_LEVELS` at the top of
   `tools/build_levels.py`:

   ```python
   "mylevel": {"title": "MY LEVEL", "divider": 183, "order": 24, "short": "ml", "save": 20, "extra": True},
   ```

   - `title`: the name on the level select (upper case, keep it short)
   - `order`: its position in the level list
   - `save`: its save slot, one no other level uses (slot 20 is free; the game has 24,
     `include/save_manager.h`). A level without a slot keeps no progress.
   - `extra: True` puts it with the extra levels (the wrench in the main menu); without it, it
     is a main level
   - `song` (optional): play another level's song, e.g. `"song": "dryout"`
   - `divider`: a setting of the old hUGETracker songs; leave it at 183

   A level that is not in `KNOWN_LEVELS` is still built, but no menu shows it.
3. Open the `.tmx` in Tiled.

## 2. The map

- **Size**: Map > Resize Map. Up to 4095 columns wide and 64 rows tall; most levels are 27 rows.
  The map sits on the ground: the floor is just below the bottom row, and rows are counted from
  the bottom (a 27-row level is the bottom 27 rows of the game's 64).
- **Layers**: the first tile layer is the level (blocks, spikes, decorations), the layer named
  `SP` holds the sprites. Keep exactly these two; the image layers (parallax, ground) are only a
  backdrop in Tiled.
- One object per cell. To put decoration on a block, use the object that has both (or draw a new
  one).
- Don't flip or rotate tiles in Tiled (X / Y / Z keys): the game ignores it. Every orientation is
  its own object (`SPIKE_UP`, `SPIKE_TOP`, ...).

## 3. Blocks, spikes and decorations

Open the Tilesets panel, `objects` tab. The tileset shows the CGB colours, with markers:

- magenta corner: **fake** (looks like a spike or block, does nothing)
- red dashed outline: **invisible** (collides, but has no art)
- yellow edge: solid from that side only (slabs, platforms)

**Painting blocks**: use the Terrain Sets panel (Checkerboard, Checkerboard set B, X blocks,
Black blocks, 1.6 blocks). Pick a style and paint: Tiled places the right edge and corner pieces.
A single block with an outline on all 4 sides, and the 1.6 shading pieces one cell further in,
go by hand.

**Special objects**:

- **Saws**: big (3x3), medium (2x2), small (1x1 and the two half-cell small saws). Place all their
  parts. A level uses either the Famidash saws or the 1.6 saws (`1.6_SAW_*`), not both; on DMG the
  1.6 saws show as Famidash saws.
- **Breakable blocks** (`BREAKABLE_BRICK`): solid from above and below, they break when the player
  runs into them, and come back on a respawn. At most 4 broken ones are remembered, so don't put
  more than 4 within one screen.
- **Slabs** (`1.6_SLAB_*`, `OUTLINE_SLAB_*`): half and smaller blocks with GDA's hitboxes. The
  player lands on the slab's own top edge.
- **Theory of Everything objects**: `TOE_BRICK_*` (solid bricks, named by their outlined sides),
  `BG_SQUARES_*` (background squares, place a group's pieces side by side on the floor),
  `BUSH_SPIKE_*` (deadly in their bottom half; `_TOP` on ceilings) and `BUSH_DECO`.
- **Ground looks** (`GROUND_*`, `BG5`, `BG6`): pieces drawn in the ground's colours, for floors
  and ceilings made of ground.

## 4. Sprites (the SP layer)

Select the `SP` layer and the `sprites` tileset. Sprites the game doesn't support are dimmed and
crossed out: placing them does nothing. The ones that work:

| sprites | notes |
|---|---|
| cube, ship, ball, UFO portals | the game mode from there on |
| mini / growth portals | |
| gravity portals (normal / upside down, also the horizontal ones) | |
| mirror portal (`MIRROR_PORTAL`) / exit (`MIRROR_EXIT`) | flips the screen left-right |
| yellow, blue, pink orbs | |
| yellow, blue, pink, red pads, upright and upside down; invisible blue pads | |
| coins 1, 2, 3 | 3 per level at most, one of each |
| `LEVEL_END_TRIGGER` | **required**: the level is finished when the player reaches it |
| pulsing ball, music note, lights, chains and the other decorations (ids 42..63) | |
| `PULSE_*` (circle, ring, heart, square, triangle, X, !, arrow, ?) | our pulsing shapes, big on the music's hits (CGB only, like all decorations) |
| `BG_COLOR_*`, `GROUND_COLOR_*` | change the background / ground colour from there on |
| `OBJ_COLOR_*` | change the block outline colour (CGB only) |

Put a sprite in the cell where it should be. If one needs to sit a few pixels off its cell (like
Famidash levels sometimes do), add an `objectOffsets` entry for the level in
`levels/metadata/*.json` (same format as the existing ones).

Speed portals are not supported: every level runs at one speed.

## 5. Colours and physics

`levels/metadata/lvlset_*.json` holds an entry per level (`"level": "<name>"`):

- `startingBackgroundColor`, `startingGroundColor`: the colours the level starts with (the
  same numbers as the colour triggers)
- `maxFallSpeed`: 7 for the faster fall some Famidash levels use

A level with no entry starts with colour 17 for both and the normal fall speed.

## 6. Music

Put the song in `music/Furnace/` with the level's name as the file name (letters and digits are
compared, case ignored: `My Level.fur` matches `mylevel`). It must be a Game Boy module. Or reuse
another level's song with `"song"` in `KNOWN_LEVELS`. Without a song the level is silent.

## 7. Build and play

```bash
python tools/build_levels.py --no-music
```

```bash
make
```

`--no-music` skips the songs (converting a new or changed `.fur` takes minutes; the levels take
seconds). Run it without `--no-music` once when you add or change a song. The ROM is
`bin/POCKETDASH.gb`.

The build prints every level's tile use, for example:

```
mylevel              CGB  57/159 tiles   DMG  60/143 tiles + 16 saw tiles (0 in sprite tiles)
```

## 8. Limits

Per level:

- **256 kinds of object** (the map stores one byte per cell). Every orientation and variant
  counts.
- **CGB: 159 distinct 8x8 tiles**, **DMG: 143** plus the saws. Objects share tiles, and on CGB
  flipped copies are free, so it is the variety of art that counts, not the number of cells.

When a level doesn't fit, the build stops with its name and what is over, e.g.
`mylevel (DMG): more than 143 distinct tiles`. Use fewer different objects (a block style or two
instead of five), or for DMG, give heavy art a simpler `dmg_art` (see `levels/objects/README.md`).

## 9. New objects

To add a block, spike or decoration that doesn't exist yet, see `levels/objects/README.md`
(draw it in `objects.png`, give it properties in Tiled, place it).

## 10. Converting a GDA level

`tools/convert_gda_level.py` turns a Geometry Dash Advance level (`../geometry_dash_advance`) into
our format:

```bash
python tools/convert_gda_level.py clubstep levels/chr_data/tmx/myclubstep.tmx
```

It converts the map only (GDA's two layers merged, the top one wins); add the sprites by hand.
Objects we don't have are left empty and listed. `docs/GDA_MISSING_OBJECTS.md` lists them for all
GDA levels.

## Troubleshooting

| problem | fix |
|---|---|
| the level isn't in the menus | add it to `KNOWN_LEVELS` (step 1) |
| `objects.tsx tiles [...] are placed in a level but have no properties` | a tile of `objects.png` without properties is in the map: give it properties, or use another tile |
| `a level uses more than 256 kinds of object` | use fewer different objects |
| `two objects of a level have engine id ...` | two kinds of the same special part, e.g. Famidash and 1.6 saws in one level: use one kind |
| `... (CGB) / (DMG): more than N distinct tiles` | see Limits |
| `(DMG): no room for its N saw tiles` | fewer kinds of saw in the level |
| the level never ends | place a `LEVEL_END_TRIGGER` sprite |
| a sprite does nothing | it is a crossed-out (unsupported) sprite |
| you renamed or deleted a level | also delete its `src/sprites/<name>_sprites.c` and `levels/level_data/<name>.bin` (they still take a ROM bank), then build again |
| Tiled shows a broken image | open the `.tmx` from `levels/chr_data/tmx/`: the tilesets are found relative to it |
