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
| `levels/objects/objects.tsx` | every block, spike, slope, saw and decoration: the object catalog (`levels/objects/README.md`) |
| `levels/chr_data/graphics/sprites.png` | the sprite tileset's image (portals, orbs, pads, coins, triggers) |
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
   - `short`: a short unique id (used in generated file names)
   - `save`: its save slot, one no other level uses (the game has 24, `include/save_manager.h`;
     look at the other entries for a free one). Without it the slot is `order - 1`.
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
- **Layers**: the first tile layer is the level (blocks, spikes, slopes, saws, decorations), the
  layer named `SP` holds the sprites. Keep exactly these two; the image layers (parallax, ground)
  are only a backdrop in Tiled.
- One object per cell. To put a decoration on a block, use an object that has both (or draw a
  new one).
- Don't flip or rotate tiles in Tiled (X / Y / Z keys): the game ignores it. Every orientation is
  its own object (`SPIKE_UP`, `SPIKE_TOP`, ...).

## 3. Blocks, spikes and the rest (the `objects` tileset)

The tileset is laid out by kind, each kind from a new row, in this order: **basics** (empty,
ground pieces, invisible blocks), **blocks**, **slopes**, **spikes**, **saws**, **orbs and pads**
(outlines), **decorations**. It shows the CGB colours, with markers:

- magenta corner: **fake** (looks like a spike or block, does nothing)
- red dashed outline: **invisible** (collides, but has no art)
- yellow edge: solid from that side only (slabs, platforms)
- cyan corner: **fading** (see below)

Select a tile and look at the Properties panel for its `name`; the names below are what to look
for.

**Painting blocks**: use the Terrain Sets panel (Checkerboard, Checkerboard set B, X blocks,
Black blocks). Pick a style and paint: Tiled places the right edge and corner pieces. A single
block outlined on all 4 sides is the one piece the brush doesn't place: put it by hand. The other
block styles (1.6 blocks `1.6_*`, Clutterfunk `+_*`, basic `BASIC_BLOCK_*`, chipped, 3D) go by
hand: their pieces sit together in the tileset as they fit together.

**Multi-cell objects**: GDA's off-grid spikes, its saws, slabs and big decorations are blocks of
cells named `NAME_R<row>C<col>`, laid out in the tileset as they look. Select the whole block in
the tileset (drag over it) and stamp it in one go. Their hazards kill with GDA's own hitbox (a
rectangle or circle), not the whole cell.

**Special objects**:

- **Slopes**: `22DEG_*` (two cells wide: `_LEFT_HALF` + `_RIGHT_HALF`), `45DEG_*` (one cell),
  `66DEG_*` (two cells tall: `_TOP_HALF` + `_BOTTOM_HALF`), and GDA's brick / checkered ones
  (`BRICKS_66_*`, `CHKRED_22_*`). The player rolls on them with GDA's slope physics. Put a block
  (or a `SLOPE_SUPPORT_*` piece) under a slope so it doesn't hang in the air.
- **Saws**: Famidash's big (3x3, `BIG_SAW_*`), medium (2x2, `MED_SAW_*`) and small (`SMALL_SAW`,
  and the half-cell `SMALL_SAW_TOP_HALF` / `_BOTTOM_HALF`) spin; place all their parts. GDA's saws
  (outline, circle, spike saws...) are multi-cell objects with a round hitbox; they don't spin.
- **Fading objects** (`FADING_*`): fade in and out with their place on the screen, like GD 1.6.
- **Breakable blocks** (`BREAKABLE_BRICK`): solid, they break when the player runs into them and
  come back on a respawn. Only the last 4 broken ones are remembered, so don't put more than 4
  within one screen.
- **Slabs** (`1.6_SLAB_*`, `BASIC_SLAB_*`, Electroman rails `1.6_EA_*`): thinner than a block,
  with GDA's hitboxes. The player lands on the slab's own top edge.
- **Ground looks** (`GROUND_*`, `BG5`, `BG6`): pieces drawn in the ground's colours, for floors
  and ceilings made of ground.

## 4. Sprites (the SP layer)

Select the `SP` layer and the `sprites` tileset. Only these cells of it do something (counted
from 0, left to right, 16 per row):

| cells | sprites |
|---|---|
| 0, 1, 2, 3 | cube, ship, ball, UFO portals |
| 24, 25 | mini / growth portals |
| 8, 9 | gravity portals: normal / upside down |
| 16..19 | the horizontal gravity portals |
| 126, 121 | mirror portal / mirror exit (flips the screen left-right) |
| 11, 5, 6 | yellow, blue, pink orbs |
| 10, 12 / 13, 14 / 37, 38 / 82, 83 | yellow / blue / pink / red pads, upright and upside down |
| 253, 254 | invisible blue pads |
| 7, 26, 27 | coins 1, 2, 3 (one of each per level at most) |
| 15 | **level end, required**: the level is finished when the player reaches it |
| 39, 40, 41 | GDA's pulsing circle, heart and square (CGB only, they pulse with the music) |
| 42..63, 73, 74 | decorations: pulsing ball, music note, lights, chains... (CGB only) |
| 128..175 | background colour triggers |
| 176..191 | block outline colour triggers (CGB only) |
| 192..239 | ground colour triggers |

Put a sprite in the cell where it should be. If one needs to sit a few pixels off its cell (like
Famidash levels sometimes do), add an `objectOffsets` entry for the level in
`levels/metadata/*.json` (same format as the existing ones).

Not supported: speed portals (every level runs at one speed), dual portals, the robot.

## 5. Colours and physics

`levels/metadata/lvlset_*.json` holds an entry per level (`"level": "<name>"`):

- `startingBackgroundColor`, `startingGroundColor`: the colours the level starts with (the
  same numbers as the colour triggers)
- `maxFallSpeed`: 7 for the faster fall some Famidash levels use

A level with no entry starts with colour 17 for both and the normal fall speed.

## 6. Music

Put the song in `music/Furnace/` with the level's name as the file name (case, spaces,
punctuation and trailing digits are ignored: `My Level.fur` matches `mylevel`). It must be a Game
Boy module. Or reuse another level's song with `"song"` in `KNOWN_LEVELS`. Without a song the
level is silent. More in `docs/MUSIC.md`.

**Keep the song light on DMG.** The music runs inside the game's frame. A song that ticks faster
than 60 Hz, or that rewrites the wave channel and changes many registers every tick (lots of
macros and effects on all channels), can make the original Game Boy drop frames. Measure it:

```bash
python tools/profile.py --level <index> --dmg
```

(`<index>` is the level's place in the list, from 0.) "music (timer ISR)" should stay around
3k cycles on average; the other songs do.

## 7. Build and play

```bash
python tools/build_levels.py
```

```bash
make
```

The ROM is `bin/POCKETDASH.gb`. Converting a new or changed `.fur` takes a few minutes (it is
traced in Furnace); converted songs are cached, so later builds take seconds.

The build prints every level's tile use, for example:

```
mylevel              CGB  57/159 tiles   DMG  60/143 tiles + 16 saw tiles (0 in sprite tiles)
```

## 8. Limits

Per level:

- **256 kinds of object** (the map stores one byte per cell). Every orientation, variant and
  every cell of a multi-cell object counts.
- **CGB: 159 distinct 8x8 tiles**, **DMG: 143** plus the saws. Objects share tiles, so it is the
  variety of art that counts, not the number of cells. GDA's big decorations and saws use many
  tiles each: a few of them fill the DMG budget.

When a level doesn't fit, the build stops with its name and what is over, e.g.
`mylevel (DMG): more than 143 distinct tiles`. Use fewer different objects (a block style or two
instead of five), or for DMG, give heavy art a simpler `dmg_art` (see `levels/objects/README.md`).

## 9. New objects

To add a block, spike or decoration that doesn't exist yet, see `levels/objects/README.md`
(draw it in `objects.png`, give it properties in Tiled, place it).

## 10. GDA levels

Every object Geometry Dash Advance's official 1.0-1.6 levels use is in the catalog (matched in
`levels/objects/gda_ours.json` and `gda_sprites.json`), so those levels can be rebuilt by hand
in Tiled. There is no converter for the current catalog yet.

## Troubleshooting

| problem | fix |
|---|---|
| the level isn't in the menus | add it to `KNOWN_LEVELS` (step 1) |
| `objects.tsx tiles [...] are placed in a level but have no properties` | a tile of `objects.png` without properties is in the map: give it properties, or use another tile |
| `a level uses more than 256 kinds of object` | use fewer different objects |
| `two objects of a level have engine id ...` | two objects that are the same special part (saw parts, ground looks): use one of them |
| `... (CGB) / (DMG): more than N distinct tiles` | see Limits |
| `(DMG): no room for its N saw tiles` | fewer kinds of saw in the level |
| the level never ends | place a level end sprite (cell 15) |
| a sprite does nothing | its cell isn't in the table in step 4 |
| the original Game Boy drops frames in one level | profile it (step 6): often the song |
| you renamed or deleted a level | also delete its `src/sprites/<name>_sprites.c` and `levels/level_data/<name>.bin` (they still take a ROM bank), then build again |
| Tiled shows a broken image | open the `.tmx` from `levels/chr_data/tmx/`: the images are found relative to it |
