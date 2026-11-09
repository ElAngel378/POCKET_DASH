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
    uint8_t mixed;          // 1 = the level has cells with a palette per quarter
} BgLevelInfo;

extern const BgLevelInfo bg_level_info[];

extern uint8_t bm_info[256], rt_ids[16], mt_cgb_flip, row_attrs[2][16];
extern const uint8_t (*mt_tab)[4], (*mt_tab_rev)[4];
void quarter_pals_column(uint8_t *attrs, const uint8_t *ids, uint8_t reversed, uint8_t r_start, uint8_t r_end, uint8_t skip) BANKED;
void quarter_pals_row(uint8_t n, uint8_t reversed) BANKED;

#endif
