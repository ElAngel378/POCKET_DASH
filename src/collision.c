#include <gb/gb.h>
#include "collision.h"
#include "famidash_metatiles.h"

#define BKG_MT_H 16

static uint8_t _prev_map_bank;
static uint8_t metatile_column_tiles[BKG_MT_H * 4];
static uint8_t metatile_column_attributes[BKG_MT_H * 4];

void col_at_begin(uint8_t map_bank) {
    if (_current_bank == map_bank) {
        _prev_map_bank = 0xFF;
        return;
    }
    _prev_map_bank = _current_bank;
    SWITCH_ROM(map_bank);
}

void col_at_end(void) {
    if (_prev_map_bank != 0xFF) {
        SWITCH_ROM(_prev_map_bank);
    }
}

uint8_t col_at_raw(
    uint16_t world_px,
    int16_t  world_py,
    const uint8_t *map,
    uint16_t map_w
) {
    if ((uint16_t)world_py >= 256u) {
        return (world_py < 0) ? COL_NONE : COL_ALL;
    }
    uint16_t mx = world_px >> 4;
    if (mx >= map_w) return COL_ALL;

    return col_at_raw_cached(&map[mx << 4], (uint16_t)world_py);
}

uint8_t col_at_raw_cached(const uint8_t *col_ptr, uint16_t world_py) {
    uint8_t py8 = (uint8_t)world_py;
    uint8_t col = famidash_metatile_collision[col_ptr[py8 >> 4]];
    uint8_t inner_y = py8 & 0x0F;

    if (col == COL_TOP) {
        if (inner_y >= 8) return COL_NONE;
    } else if (col == COL_BOTTOM) {
        if (inner_y < 8) return COL_NONE;
    } else if (col == COL_DEATH_TOP_HALF) {
        if (inner_y < 8) return COL_NONE;
        return COL_DEATH;
    } else if (col == COL_DEATH_BOTTOM_HALF) {
        if (inner_y >= 8) return COL_NONE;
        return COL_DEATH;
    }

    return col;
}

// Bank-safe collision check wrapper
uint8_t col_at(
    uint16_t world_px,
    int16_t  world_py,
    const uint8_t *map,
    uint16_t map_w,
    uint8_t  map_bank
) {
    uint8_t res;
    col_at_begin(map_bank);
    res = col_at_raw(world_px, world_py, map, map_w);
    col_at_end();
    return res;
}

// Upload tileset graphics to VRAM
void load_bkg_tileset(const uint8_t* tiles, uint16_t tile_count, uint8_t bank) {
  uint8_t _prev = _current_bank;
  SWITCH_ROM(bank);
  VBK_REG = VBK_TILES;
  if (tile_count == 256u) {
    set_bkg_data(0, 128, tiles);
    set_bkg_data(128, 32, tiles + (128u * 16u));
  } else {
    set_bkg_data(0, (uint8_t)tile_count, tiles);
  }
  SWITCH_ROM(_prev);
}

// Buffer current and adjacent map columns in WRAM to reduce bank switches
void load_collision_columns(uint16_t map_col, const uint8_t* map,
                            uint16_t map_w, uint8_t map_bank,
                            uint8_t* columns) {
  uint8_t _prev = _current_bank;
  uint8_t i;
  const uint8_t *left;
  const uint8_t *right;

  SWITCH_ROM(map_bank);
  left = &map[map_col << 4];
  right = (map_col + 1u < map_w) ? left + 16 : left;
  for (i = 0; i < 16; i++) {
    columns[i] = left[i];
    columns[i + 16] = right[i];
  }
  SWITCH_ROM(_prev);
}

void prepare_mt_column(uint16_t map_col, const uint8_t* map, uint8_t map_bank, uint8_t reversed) {
  uint8_t tl_x = 0;
  uint8_t tr_x = 1;
  if (_cpu == CGB_TYPE) {
      uint8_t vram_slot = (uint8_t)(map_col & 15u);
      if (reversed) vram_slot = (uint8_t)(-(int8_t)vram_slot & 15u);
      tl_x = (uint8_t)((vram_slot & 3u) << 1);
      tr_x = tl_x + 1u;
  }

  uint8_t _prev = _current_bank;
  SWITCH_ROM(map_bank);

  const uint8_t *map_ptr = &map[(uint16_t)map_col << 4];
  const uint8_t (*mt_table)[4] = reversed ? metatiles_rev : metatiles;
  uint8_t *dst = metatile_column_tiles;

  if (_cpu == CGB_TYPE) {
      static const uint8_t row_to_ty0[16] = { 0, 16, 32, 0, 16, 32, 0, 16, 32, 0, 16, 32, 0, 16, 32, 0 };
      static const uint8_t row_to_ty1[16] = { 8, 24, 40, 8, 24, 40, 8, 24, 40, 8, 24, 40, 8, 24, 40, 8 };
      uint8_t *dst_attr = metatile_column_attributes;
      for (uint8_t r = 0; r < BKG_MT_H; r++) {
          uint8_t metatile_id = *map_ptr++;
          if (metatile_id == 0) {
              uint8_t r0 = row_to_ty0[r];
              uint8_t r1 = row_to_ty1[r];
              *dst++ = r0 + tl_x;
              *dst++ = r0 + tr_x;
              *dst++ = r1 + tl_x;
              *dst++ = r1 + tr_x;
              *dst_attr++ = 0x0B; // Bit 3 (0x08 = VRAM Bank 1) + Palette 3 (0x03)
              *dst_attr++ = 0x0B;
              *dst_attr++ = 0x0B;
              *dst_attr++ = 0x0B;
          } else {
              const uint8_t *tiles = mt_table[metatile_id];
              *dst++ = tiles[0];
              *dst++ = tiles[1];
              *dst++ = tiles[2];
              *dst++ = tiles[3];

              uint8_t palette = famidash_metatile_palettes[metatile_id];
              *dst_attr++ = palette;
              *dst_attr++ = palette;
              *dst_attr++ = palette;
              *dst_attr++ = palette;
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
  SWITCH_ROM(_prev);
}

void flush_mt_column(uint8_t ring_col) {
  uint8_t bx = ring_col << 1;
  VBK_REG = VBK_TILES;
  set_bkg_tiles(bx, 0, 2, BKG_MT_H << 1, metatile_column_tiles);
  if (_cpu == CGB_TYPE) {
    VBK_REG = VBK_ATTRIBUTES;
    set_bkg_tiles(bx, 0, 2, BKG_MT_H << 1, metatile_column_attributes);
    VBK_REG = VBK_TILES;
  }
}

void fill_scroll_bg(const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed) {
  uint16_t cols = (map_w < 16) ? map_w : 16;
  for (uint16_t c = 0; c < cols; c++) {
    prepare_mt_column(c, map, map_bank, reversed);
    flush_mt_column((uint8_t)(c % 16));
  }
}

#include "hUGEDriver.h"
extern uint8_t music_ready;
extern uint8_t current_song_bank;
extern volatile uint8_t current_music_divider;

void init_music_banked(const hUGESong_t * song, uint8_t bank, uint8_t divider) {
    uint8_t _prev = _current_bank;
    music_ready = 0;
    current_song_bank = bank;
    current_music_divider = divider;
    SWITCH_ROM(bank);
    disable_interrupts();
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    hUGE_init(song);
    TMA_REG = divider;
    TIMA_REG = divider;
    enable_interrupts();
    SWITCH_ROM(_prev);
    music_ready = 1;
}
