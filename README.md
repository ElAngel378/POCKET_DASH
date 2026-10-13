<p align="center">
  <img src=".github/artwork/pocketdash_banner.png" width="500">
</p>

# Pocket Dash

### Pocket Dash (or Geometry Dash Pocket/GDP) is a demake of Geometry Dash for the DMG and GBC Game Boy!
Info: The project was renamed to Geometry Dash Pocket, and was formerly known as GB DASH

### Images:
<p align="center">
  <img src=".github/artwork/POCKETDASH_004.png?v=2" width="350">
  <img src=".github/artwork/POCKETDASH_007.png?v=2" width="350">
</p>

<p align="center">
  <img src=".github/artwork/POCKETDASH_001.png?v=2" width="350">
  <img src=".github/artwork/POCKETDASH_002.png?v=2" width="350">
</p>

<p align="center">
  <img src=".github/artwork/POCKETDASH_003.png?v=2" width="350">
  <img src=".github/artwork/POCKETDASH_006.png?v=2" width="350">
</p>

<p align="center">
  <img src=".github/artwork/pocketdash_dmg.png?v=2" width="350">
</p>

---
# About
Experience the ultimate rage-inducing rhythm platformer, scaled down to 4 shades of green (or gray) and a crisp 160x144 resolution! Built from scratch for original Game Boy hardware.

Unlike GB Dash by TiTi, this one features a Famidash-style gameplay with the actual original levels, and support for the original (DMG) Game Boy!

This game is being actively developed (coding and music) by Sotospro24 and ElAngel378

# Features (as of now)
- All levels up to Xstep
- Gameplay (Cube, Ship, Ball)
- Accurate Physics
- Collision (Death and block collision)
- Music Covers, played by a Furnace-accurate Game Boy driver ([fur2gb](https://github.com/Soteris24/fur2gb))
- All 1.0 - 1.2 mechanics including inverted gravity, mirror mode
- Level End screen
- Game Boy Original and Color support!

---

# Download 
### Download the latest Nightly build here: <a href="https://github.com/ElAngel378/GBDASH/releases/download/nightly/POCKETDASH.gb"><ins>DOWNLOAD</ins></a>
**WARNING!** These are constantly changing and might be broken.
### Alternatively, download the latest stable build in the releases tab.
---

# Building from source

Requires **GBDK-2020** (`gbdk-2020` v4.2.x or newer) and GNU Make.

- **Windows:** download the latest [GBDK releases](https://github.com/gbdk-2020/gbdk-2020/releases), unzip it, and point the build at it. The default search path is `C:/gbdk`; override it with the `GBDK` variable (e.g. `make GBDK=C:/dev/gbdk-2020`).
- **Linux/macOS** (and GitHub Actions): fetch and extract the `gbdk-linux64` release from the [GBDK releases](https://github.com/gbdk-2020/gbdk-2020/releases), then run `make` with `GBDK=/path/to/gbdk` exported.

Build the ROM:

```
make clean
make -j8
```

The output is `bin/POCKETDASH.gb`. Music playback uses [hUGEDriver](https://github.com/SuperDisk/hUGEDriver); its prebuilt `lib/hUGEDriver.lib` is included in this repository.

## Adding music

Run `convert_levels.bat` (or `python tools/build_levels.py`, then `make`) after adding songs:

- **Furnace** (the game's music): put the `.fur` in `music/Furnace/`. A song named like a level (`BaseAfterBase.fur`, `DryOut32.fur`; case, spaces and trailing digits don't matter) is that level's song; `MenuLoop.fur` is the menu theme; any other song is only in the music test. Songs must use a single Game Boy chip (DMG or CGB model); others are skipped with a warning.
- **hUGETracker** (the LEGACY setting): put the `.uge` in `music/Legacy/`, named like the level (`DryOut.uge` for `dryout.tmx`). A level without a Furnace song plays its hUGETracker one.

To move a hUGETracker song to Furnace, convert it with fur2gb's `tools/uge2fur.py` (`python ../fur2gb/tools/uge2fur.py music/Legacy/Song.uge -o music/Furnace/Song.fur`) and edit it in Furnace. `music/Furnace/Old/` keeps earlier hand-made versions (not built).

The Furnace songs are converted with [fur2gb](https://github.com/Soteris24/fur2gb), which plays each song in Furnace and checks the result write for write. It needs the fur2gb repo next to this one (or `FUR2GB_DIR`) and its Furnace build (see its README). Converted songs are cached in `.cache/furnace/`, so only new or changed songs take time. Without fur2gb the songs already in `src/furnace/` are kept.

# Music driver: fur2gb

The game's music is written in [Furnace](https://github.com/tildearrow/furnace) and played by **[fur2gb](https://github.com/Soteris24/fur2gb)**, a Game Boy driver made for this project (also by Soteris24). It plays a Furnace song on the console exactly as Furnace plays it: the ROM makes the same register writes as Furnace's Game Boy chip, in the same order, tick by tick.

- **Converter:** `fur2gb.py` turns a `.fur` into C data. It runs the song in a patched Furnace, records every register write and checks that its own model of the player reproduces them, so a song that can't be played exactly is rejected or flagged instead of sounding slightly off.
- **Player:** a small Game Boy player (`furplayer`). The per-tick routine is in bank 0 and takes about 1.5-6k cycles per tick on average (up to about 12k on the busiest ticks), using about 260 bytes of RAM. Songs take 3-34 KB of ROM each.
- **Sound effects:** channels can be borrowed by sound effects and handed back, so the game's effects don't cut the song.
- **Verification:** `verify_rom.py` runs a test ROM headless in Mesen and compares every write with Furnace's, through the loop jumps.
- **hUGETracker songs:** `uge2fur.py` converts a hUGETracker module to a Furnace one that plays like hUGEDriver plays it. The original hUGEDriver songs are still in the game: turn on the **LEGACY** setting to hear them.

Pocket Dash copies the driver into `src/furnace/` with `tools/convert_furnace.py` (see *Adding music* above). The driver, its tests and a full description are in the [fur2gb repository](https://github.com/Soteris24/fur2gb).

---

# Development Info

| Contributor | Contribution |
|---|---|
|[**Sotos_24**](https://github.com/Soteris24) **and** [**ElAngel378**](https://github.com/ElAngel378)|**Project creators and lead developers**|
|[Sotos_24](https://github.com/Soteris24) | Code (main code), Music, Graphics <sub>*when I feel like it*</sub> |
|[ElAngel378](https://github.com/ElAngel378) |Code (optimize), Music, Graphics|
|[Crafty Jumper](https://github.com/Crafty-Jumper) | Music, Graphics |
|[Ziqiangao](https://github.com/ziqiangao/)| Tools, Music, Graphics|
|PBaxx | Graphics |
|Finntendo | Graphics |
|Switchroot | Graphics |
|BobAJoeJoe | Graphics, Music |
|Ranedom | Graphics |
|NerdBoy628 | Music |


---

# TODO (for 1.0 release)
- Add all objects, mechanics, levels up to 1.5
- A proper menu and level select
- Decoration (supported only in GBC mode)
- Graphical improvements

---

# Contributing 

**Ways of Contributing**
1. Join the [Discord](https://discord.gg/cg8NRGxJTv)
2. Pull Requests
3. Contacting the main devs
4. Do NOT open issues for a discussion.
5. Any issues can be reported in the issues tab.

# Thank you
### To <a href="https://github.com/tfdsoft/famidash"><ins>TFDSoft</ins></a> , the Famidash team for assets (such as level files, graphics)
### And, of course, the game we all love: Geometry Dash by RobTop Games.
### AI Disclosure

AI tools (LLMs) are used in this project. Up to release 0.3 they were only used for things like boilerplate code, optimizing parts of the code and brainstorming solutions.

After release 0.3 there is some vibecoding. Development is much faster that way, and as I have school and other things going on, it lets me keep working on the game while focusing on my studies. It also generally makes the whole process easier, and in my experience it leads to fewer bugs and optimizes very well.

Developing for the original 1989 Game Boy with GBDK 2020 still requires a solid understanding of things like memory management, bank switching, hardware registers and low-level C/Assembly optimization, so the direction, decisions, testing and debugging are still done by us.
