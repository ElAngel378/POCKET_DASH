#pragma bank 14

#include <gb/gb.h>
#include <stdint.h>
#include <string.h>
#include "collision.h"
#include "famidash_metatiles.h"

#define BKG_MT_H 16

uint8_t vram_row0_is_ground = 1;
static const uint8_t ground_top[8] = { 48, 49, 49, 49, 49, 49, 49, 50 };
static const uint8_t ground_bot[8] = { 51, 52, 52, 52, 52, 52, 52, 53 };

static uint8_t metatile_column_tiles[BKG_MT_H * 4];
static uint8_t metatile_column_attributes[BKG_MT_H * 4];
static uint8_t col_buf[16];

void prepare_mt_column(uint16_t map_col, const uint8_t* map, uint8_t map_bank, uint8_t reversed) BANKED {
    uint8_t tl_x = 0;
    uint8_t tr_x = 1;
    if (_cpu == CGB_TYPE) {
        uint8_t vram_slot = (uint8_t)(map_col & 15u);
        if (reversed) vram_slot = (uint8_t)(-(int8_t)vram_slot & 15u);
        tl_x = (uint8_t)((vram_slot & 3u) << 1);
        tr_x = tl_x + 1u;
    }

    get_map_column(map_col, map, map_bank, col_buf);

    const uint8_t *map_ptr = col_buf;
    const uint8_t (*mt_table)[4] = reversed ? metatiles_rev : metatiles;
    uint8_t *dst = metatile_column_tiles;

    if (_cpu == CGB_TYPE) {
        static const uint8_t row_to_ty0[16] = { 0, 16, 32, 0, 16, 32, 0, 16, 32, 0, 16, 32, 0, 16, 32, 0 };
        uint8_t *dst_attr = metatile_column_attributes;
        for (uint8_t r = 0; r < BKG_MT_H; r++) {
            uint8_t metatile_id = *map_ptr++;
            if (r == 0 && vram_row0_is_ground) {
                *dst++ = ground_top[tl_x];
                *dst++ = ground_top[tr_x];
                *dst++ = ground_bot[tl_x];
                *dst++ = ground_bot[tr_x];
                *dst_attr++ = 0x0C; // Bank 1 + Palette 4
                *dst_attr++ = 0x0C;
                *dst_attr++ = 0x0C;
                *dst_attr++ = 0x0C;
                continue;
            }
            const uint8_t *tiles = mt_table[metatile_id];
            uint8_t palette = famidash_metatile_palettes[metatile_id];
            uint8_t r0 = row_to_ty0[r];
            uint8_t r1 = r0 + 8u;
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t t = tiles[i];
                if (t == 12) {
                    *dst++ = (i < 2 ? r0 : r1) + ((i & 1) ? tr_x : tl_x);
                    *dst_attr++ = 0x0B;
                } else {
                    *dst++ = t;
                    *dst_attr++ = palette;
                }
            }
        }
    } else {
        for (uint8_t r = 0; r < BKG_MT_H; r++) {
            uint8_t metatile_id = *map_ptr++;
            const uint8_t *tiles = mt_table[metatile_id];

            *dst++ = tiles[0];
            *dst++ = tiles[1];
            *dst++ = tiles[2];
            *dst++ = tiles[3];
        }
    }
}

void flush_mt_column(uint8_t ring_col) BANKED {
    uint8_t bx = ring_col << 1;
    VBK_REG = VBK_TILES;
    set_bkg_tiles(bx, 0, 2, BKG_MT_H << 1, metatile_column_tiles);
    if (_cpu == CGB_TYPE) {
        VBK_REG = VBK_ATTRIBUTES;
        set_bkg_tiles(bx, 0, 2, BKG_MT_H << 1, metatile_column_attributes);
        VBK_REG = VBK_TILES;
    }
}

void fill_scroll_bg(const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed) BANKED {
    uint16_t cols = (map_w < 16) ? map_w : 16;
    for (uint16_t c = 0; c < cols; c++) {
        prepare_mt_column(c, map, map_bank, reversed);
        flush_mt_column((uint8_t)(c % 16));
    }
}

void update_vram_row0(uint8_t to_ground, uint16_t loaded_r, const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed) BANKED {
    if (_cpu != CGB_TYPE) return;
    vram_row0_is_ground = to_ground;

    if (to_ground) {
        VBK_REG = 0;
        for (uint8_t k = 0; k < 4; k++) {
            set_bkg_tiles((uint8_t)(k << 3), 0, 8, 1, ground_top);
            set_bkg_tiles((uint8_t)(k << 3), 1, 8, 1, ground_bot);
        }
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 2, 0x0C);
        VBK_REG = 0;
        return;
    }

    uint8_t row0_tiles[64];
    uint8_t row0_attrs[64];
    const uint8_t (*mt_table)[4] = reversed ? metatiles_rev : metatiles;

    for (uint8_t s = 0; s < 16; s++) {
        uint8_t slot = s;
        if (reversed) slot = (uint8_t)(-(int8_t)slot & 15u);
        uint8_t tl_x = (uint8_t)((slot & 3u) << 1);
        uint8_t tr_x = tl_x + 1u;
        uint16_t col = loaded_r - ((loaded_r - slot) & 15u);
        if (col < map_w) {
            static uint8_t col_buf0[16];
            get_map_column(col, map, map_bank, col_buf0);
            uint8_t mt_id = col_buf0[0];
            const uint8_t *tiles = mt_table[mt_id];
            uint8_t pal = famidash_metatile_palettes[mt_id];
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t t = tiles[i];
                uint8_t dst_idx = (i < 2 ? 0 : 32) + (s << 1) + (i & 1);
                if (t == 12) {
                    row0_tiles[dst_idx] = (i < 2 ? 0 : 8) + ((i & 1) ? tr_x : tl_x);
                    row0_attrs[dst_idx] = 0x0B;
                } else {
                    row0_tiles[dst_idx] = t;
                    row0_attrs[dst_idx] = pal;
                }
            }
        }
    }

    VBK_REG = 0;
    set_bkg_tiles(0, 0, 32, 2, row0_tiles);
    VBK_REG = 1;
    set_bkg_tiles(0, 0, 32, 2, row0_attrs);
    VBK_REG = 0;
}
