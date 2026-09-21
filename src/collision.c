#include <gb/gb.h>
#include "collision.h"
#include "famidash_metatiles.h"

#define BKG_MT_H 16

static uint8_t _prev_map_bank;

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
#include <string.h>

void load_collision_columns(uint16_t map_col, const uint8_t* map,
                            uint16_t map_w, uint8_t map_bank,
                            uint8_t* columns) {
  uint8_t _prev = _current_bank;
  const uint8_t *left;
  const uint8_t *right;

  SWITCH_ROM(map_bank);
  left = &map[map_col << 4];
  right = (map_col + 1u < map_w) ? left + 16 : left;
  memcpy(columns, left, 16);
  memcpy(columns + 16, right, 16);
  SWITCH_ROM(_prev);
}

void get_map_column(uint16_t map_col, const uint8_t *map, uint8_t map_bank, uint8_t *dest) {
  uint8_t _prev = _current_bank;
  SWITCH_ROM(map_bank);
  memcpy(dest, &map[(uint16_t)map_col << 4], 16);
  SWITCH_ROM(_prev);
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
