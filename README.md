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
- Music Covers
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

AI tools (LLMs) are used in this project to help with things like boilerplate code, optimizing parts of the code, and brainstorming solutions. However, the project is not vibecoded.

Developing for the original 1989 Game Boy with GBDK 2020 requires a solid understanding of things like memory management, bank switching, hardware registers, and low-level C/Assembly optimization. AI is used as a tool and pair-programmer to speed things up and explore ideas, but the development, decisions, testing, and debugging are still done by us.
