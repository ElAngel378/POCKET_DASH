# The object catalog

Every block, spike and decoration a level can use is one 16x16 tile of `objects.tsx` (a Tiled
tileset over `objects.png`). Levels (`levels/chr_data/tmx/*.tmx`) place them in their map layer;
the build gives every level its own tile sheet with only the tiles its objects need
(`tools/build_bg_tiles.py`), so a level can use any object, as long as it fits:

- up to 256 kinds of object per level (the map stores one byte per cell)
- CGB: up to 159 distinct 8x8 tiles per level; DMG: 143 (plus the saws' tiles)

The build prints each level's count, and fails with the level's name when one does not fit.

## objects.png

16 objects (256 px) per row. The 4 greys are colour indices: white = 0, light grey = 1,
dark grey = 2, black = 3 (use exactly these 4 greys). A fully white 8x8 quarter is transparent:
on CGB the parallax background shows through it.

## Object properties (in Tiled: select the tile, Properties panel)

| property | type | meaning |
|---|---|---|
| `name` | string | unique name |
| `collision` | int | collision type, `COL_*` in `include/collision.h` (0 = none, 7 = solid block, ...) |
| `palette` | int | CGB background palette: 0 blocks / spikes (sky, darker sky, black, white), 1 like 0 with colour 2 = sky, 2 background decorations (colour 3 = the decoration accent), 5 fake spikes (sky, darker sky, black, black) |
| `famidash_id` | int | the Famidash metatile it came from; **-1 for new objects** |
| `dmg_art` | int | tile id of its DMG-only art (a tile with `dmg_only` = true further down the image), -1 = the same art on DMG |

Objects with a `famidash_id` keep it as their id in the level: the engine knows some of them
(saw parts, ground looks, pit spikes). New objects get a free id in each level by themselves.

## Adding an object

1. Draw it in a free 16x16 cell of `objects.png` (rows can be added at the bottom).
2. In Tiled, open `objects.tsx` (it picks up the larger image), select the new tile and add the
   properties above (copy them from a similar object and change `name`, `famidash_id` = -1).
3. Place it in a level in Tiled and save.
4. `python tools/build_levels.py` (maps, sprite lists and the tile build), then `make`.

The catalog was made once from the Famidash data by `tools/make_object_catalog.py`; see
`docs/PLAN_POST_1.5_OBJECTS.md` for the plan.
