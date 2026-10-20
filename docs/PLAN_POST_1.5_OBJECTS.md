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

## Limits per level (two separate ones)

- Object kinds: up to 256 per level (one byte per map cell), but per level, so thousands in the
  game. Two-byte cells would lift it at the cost of double map size and a slower renderer;
  probably never needed.
- Unique 8x8 tiles: the real limit. CGB: bank 0 (~160 level tiles) plus what bank 1 has free
  after the background, ground, saws and sprite data (~50). Objects share tiles and CGB can flip
  them (a symmetric block often needs 1-2 unique tiles instead of 4).
- CGB background palettes: 8, some already taken (ground, background, text).

## DMG

DMG has ~144 slots, no flips and no bank 1: 1.6-heavy levels will not fit with full art. Each
catalog object names a DMG stand-in (usually the 1.5-style object drawn today, or simplified art).
Collision always stays the real object's, so the level plays the same; only the art is
downgraded. The DMG build swaps in stand-ins until the level fits (saws and fake spikes already do
this by hand). Later option for very heavy levels: swap tile sets mid-level at a portal, like the
DMG mirror portals reload the sheet.
