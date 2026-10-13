# Music in Pocket Dash

Pocket Dash plays Furnace songs through its own Game Boy driver, [fur2gb](https://github.com/Soteris24/fur2gb). The original hUGETracker songs still work through the **LEGACY** setting.

## Adding music

Run `convert_levels.bat` (or `python tools/build_levels.py`, then `make`) after adding songs:

- **Furnace** (the game's music): put the `.fur` in `music/Furnace/`. A song named like a level (`BaseAfterBase.fur`, `DryOut32.fur`; case, spaces and trailing digits don't matter) is that level's song; `MenuLoop.fur` is the menu theme; any other song is only in the music test. Songs must use a single Game Boy chip (DMG or CGB model); others are skipped with a warning.
- **hUGETracker** (the LEGACY setting): put the `.uge` in `music/Legacy/`, named like the level (`DryOut.uge` for `dryout.tmx`). A level without a Furnace song plays its hUGETracker one.

To move a hUGETracker song to Furnace, convert it with fur2gb's `tools/uge2fur.py` (`python ../fur2gb/tools/uge2fur.py music/Legacy/Song.uge -o music/Furnace/Song.fur`) and edit it in Furnace. `music/Furnace/Old/` keeps earlier hand-made versions (not built).

The Furnace songs are converted with [fur2gb](https://github.com/Soteris24/fur2gb), which plays each song in Furnace and checks the result write for write. It needs the fur2gb repo next to this one (or `FUR2GB_DIR`) and its Furnace build (see its README). Converted songs are cached in `.cache/furnace/`, so only new or changed songs take time. Without fur2gb the songs already in `src/furnace/` are kept.

## The driver: fur2gb

The game's music is written in [Furnace](https://github.com/tildearrow/furnace) and played by **[fur2gb](https://github.com/Soteris24/fur2gb)**, a Game Boy driver made for this project (also by Soteris24). It plays a Furnace song on the console exactly as Furnace plays it: the ROM makes the same register writes as Furnace's Game Boy chip, in the same order, tick by tick.

- **Converter:** `fur2gb.py` turns a `.fur` into C data. It runs the song in a patched Furnace, records every register write and checks that its own model of the player reproduces them, so a song that can't be played exactly is rejected or flagged instead of sounding slightly off.
- **Player:** a small Game Boy player (`furplayer`). The per-tick routine is in bank 0 and takes about 1.5-6k cycles per tick on average (up to about 12k on the busiest ticks), using about 260 bytes of RAM. Songs take 3-34 KB of ROM each.
- **Sound effects:** channels can be borrowed by sound effects and handed back, so the game's effects don't cut the song.
- **Verification:** `verify_rom.py` runs a test ROM headless in Mesen and compares every write with Furnace's, through the loop jumps.
- **hUGETracker songs:** `uge2fur.py` converts a hUGETracker module to a Furnace one that plays like hUGEDriver plays it. The original hUGEDriver songs are still in the game: turn on the **LEGACY** setting to hear them.

Pocket Dash copies the driver into `src/furnace/` with `tools/convert_furnace.py` (see *Adding music* above). The driver, its tests and a full description are in the [fur2gb repository](https://github.com/Soteris24/fur2gb).
