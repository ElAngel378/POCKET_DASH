#ifndef BG_LEVEL_INFO_H
#define BG_LEVEL_INFO_H

#include <stdint.h>

// One bank with the block tables and what the renderer needs per level: it is read with
// fur_copy_banked when a level loads, so more levels don't cost space in the renderer's full bank.
// src/famidash_metatiles.c, src/level_collision.c (tools/build_bg_tiles.py).
#define LEVEL_TABLES_BANK 16

typedef struct {
    const uint8_t *col;     // collision table: famidash_metatile_collision, or the level's own
    const uint8_t *pal;     // CGB palette table: famidash_metatile_palettes, or the level's own
    const uint8_t *sheet;   // DMG baked sheet (levels with mirror portals)
    uint8_t sheet_bank;     // 0 = none (the base sheet, then the load list)
    uint8_t saws;           // 1 = the level has saws (animate them)
    uint8_t big_saws;       // 1 = big saws, so metatile SAW_CENTER_MT is their centre (CGB)
    uint8_t rest;           // index in bg_sheet_rests: the baked sheet's other extra tiles
} BgLevelInfo;

extern const BgLevelInfo bg_level_info[];

#endif
