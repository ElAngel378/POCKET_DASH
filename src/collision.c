#include <gb/gb.h>
#include "music.h"
#include "collision.h"
#include "famidash_metatiles.h"
#include <string.h>
#include <gb/cgb.h>

#define BKG_MT_H 16

// A level map is MAP_ROWS metatiles tall, stored column by column (MAP_ROWS bytes each), and
// split into MAP_BANK_COLS-column chunks (16 KB), one per ROM bank, in consecutive banks
// starting at map_bank. Every chunk is the only thing in its bank, so each one starts at the
// same address as the first: column c is in bank map_bank + (c >> 8), at
// map + ((c & 255) << 6).
#define MAP_BANK(map_bank, c) ((uint8_t)((map_bank) + (uint8_t)((c) >> MAP_BANK_COLS_SHIFT)))
#define MAP_COL(map, c) ((map) + (((uint16_t)(c) & (MAP_BANK_COLS - 1u)) << MAP_ROWS_SHIFT))

static uint8_t _prev_map_bank;

uint8_t level_mt_col[256];   // collision.h

// Broken breakable blocks (collision.h): the last BROKEN_MAX, a ring. A broken block has left the
// screen long before that many more are broken.
#define BROKEN_MAX 4
static uint16_t broken_col[BROKEN_MAX];
static uint8_t broken_row[BROKEN_MAX];
static uint8_t broken_n, broken_next;
uint16_t broken_redraw = 0xFFFF;

void break_block(uint16_t col, uint8_t row) {
  broken_col[broken_next] = col;
  broken_row[broken_next] = row;
  broken_next = (broken_next + 1u) & (BROKEN_MAX - 1u);
  if (broken_n < BROKEN_MAX) broken_n++;
  broken_redraw = col;
}

void broken_reset(void) {
  broken_n = broken_next = 0;
  broken_redraw = 0xFFFF;
}

// Empties the broken cells of map column `col` in dest, which holds its rows row0 .. row0+n-1
static void patch_broken(uint16_t col, uint8_t *dest, uint8_t row0, uint8_t n) {
  for (uint8_t i = 0; i < broken_n; i++) {
    uint8_t r = (uint8_t)(broken_row[i] - row0);
    if (broken_col[i] == col && r < n) dest[r] = 0;
  }
}

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

    return col_at_raw_cached(MAP_COL(map, mx), (uint16_t)world_py);
}

uint8_t col_at_raw_cached(const uint8_t *col_ptr, uint16_t world_py) {
    uint8_t py8 = (uint8_t)world_py;
    uint8_t col = level_mt_col[col_ptr[py8 >> 4]];
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
    } else if (col >= COL_DEATH_LEFT_BOTTOMQ && col <= COL_DEATH_RIGHT_TOPQ) {
        if (col & 4) {
            if (inner_y >= 8) return COL_NONE;
        } else {
            if (inner_y < 8) return COL_NONE;
        }
        return (col & 1) ? COL_DEATH_RIGHT : COL_DEATH_LEFT;
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
    col_at_begin(MAP_BANK(map_bank, world_px >> 4));
    res = col_at_raw(world_px, world_py, map, map_w);
    col_at_end();
    return res;
}

// Display-off VRAM copy of n bytes (a multiple of 16) to a 16-byte aligned dst: general purpose
// DMA on CGB when src is 16-byte aligned too (the DMA ignores the low 4 bits of the source; 16
// bytes per 8 cycles, the CPU halts), an unrolled loop otherwise. The ROM bank holding src must
// already be switched in.
static const uint8_t *vc_src;
static uint8_t *vc_dst;
static uint16_t vc_blocks;   // 16-byte blocks left
static void vram_copy_dmg(void) __naked {
    __asm
        ld      hl, #_vc_dst
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)                 ; de = dst
        ld      hl, #_vc_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a                    ; hl = src
        ld      bc, #_vc_blocks
        ld      a, (bc)
        ld      c, a
        ld      a, (_vc_blocks + 1)
        ld      b, a                    ; bc = blocks (never 0)
    1$:
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     de                      ; e wrapped to the next block: carry into d
        dec     bc
        ld      a, b
        or      a, c
        jr      NZ, 1$
        ret
    __endasm;
}

void vram_copy(uint8_t *dst, const uint8_t *src, uint16_t n) {
    if (!n) return;
    if (_cpu == CGB_TYPE && !((uint8_t)src & 15u)) {
        while (n) {
            uint8_t b = (n >= 2048u) ? 128u : (uint8_t)(n >> 4);
            HDMA1_REG = (uint8_t)((uint16_t)src >> 8);
            HDMA2_REG = (uint8_t)src;
            HDMA3_REG = (uint8_t)((uint16_t)dst >> 8);
            HDMA4_REG = (uint8_t)dst;
            HDMA5_REG = (uint8_t)(b - 1u);   // bit 7 clear: general purpose DMA
            src += (uint16_t)b << 4;
            dst += (uint16_t)b << 4;
            n -= (uint16_t)b << 4;
        }
        return;
    }
    vc_src = src;
    vc_dst = dst;
    vc_blocks = n >> 4;
    vram_copy_dmg();
}

// Upload tileset graphics to VRAM
void load_bkg_tileset(const uint8_t* tiles, uint16_t tile_count, uint8_t bank) {
  uint8_t _prev = _current_bank;
  uint8_t lcd_on = LCDC_REG & LCDCF_ON;   // read first: SDCC miscompiled the test inside the if
  SWITCH_ROM(bank);
  VBK_REG = VBK_TILES;
  // BG tiles 0..127 and 128.. are separate VRAM blocks (0x9000 / 0x8800)
  if (!lcd_on) {
    // display off: VRAM is always accessible, plain copies (set_bkg_data waits on STAT per byte)
    uint16_t lo = (tile_count > 128u) ? 128u : tile_count;
    vram_copy((uint8_t *)0x9000, tiles, lo * 16u);
    if (tile_count > 128u) vram_copy((uint8_t *)0x8800, tiles + 128u * 16u, (tile_count - 128u) * 16u);
  } else if (tile_count > 128u) {
    set_bkg_data(0, 128, tiles);
    set_bkg_data(128, (uint8_t)(tile_count - 128u), tiles + (128u * 16u));
  } else {
    set_bkg_data(0, (uint8_t)tile_count, tiles);
  }
  SWITCH_ROM(_prev);
}

// Buffer current and adjacent map columns in WRAM to reduce bank switches

// The player's collision window: map rows row0 .. row0+15 of columns map_col and map_col+1
void load_collision_columns(uint16_t map_col, const uint8_t* map,
                            uint16_t map_w, uint8_t map_bank,
                            uint8_t* columns, uint8_t row0) {
  uint8_t _prev = _current_bank;
  uint16_t right_col = (map_col + 1u < map_w) ? map_col + 1u : map_col;

  SWITCH_ROM(MAP_BANK(map_bank, map_col));
  memcpy(columns, MAP_COL(map, map_col) + row0, 16);
  // the right column is in the next bank when map_col is the last column of a chunk
  SWITCH_ROM(MAP_BANK(map_bank, right_col));
  memcpy(columns + 16, MAP_COL(map, right_col) + row0, 16);
  SWITCH_ROM(_prev);
  if (broken_n) {
    patch_broken(map_col, columns, row0, 16);
    patch_broken(right_col, columns + 16, row0, 16);
  }
}

// The 16 map rows the VRAM ring holds for band `band`: dest[k] = map row band + ((k - band) & 15),
// the row shown in VRAM metatile row k. Rows below the map (the ground row) read as 0.
void get_map_column(uint16_t map_col, const uint8_t *map, uint8_t map_bank, uint8_t *dest, uint8_t band) {
  uint8_t _prev = _current_bank;
  SWITCH_ROM(MAP_BANK(map_bank, map_col));
  // VRAM rows b..15 show map rows base+b..base+15, VRAM rows 0..b-1 rows base+16..base+16+b-1
  const uint8_t *src = MAP_COL(map, map_col) + (band & ~15u);
  uint8_t b = band & 15u;
  memcpy(dest + b, src + b, 16u - b);
  if (b) {
    if ((band & ~15u) + 16u < MAP_ROWS) memcpy(dest, src + 16, b);
    else memset(dest, 0, b);   // below the map: the ground row
  }
  SWITCH_ROM(_prev);
  // dest[k] is map row band + ((k - band) & 15): a broken row r of the band is at r & 15
  for (uint8_t i = 0; i < broken_n; i++) {
    if (broken_col[i] == map_col && (uint8_t)(broken_row[i] - band) < 16u) dest[broken_row[i] & 15u] = 0;
  }
}

// Metatiles of map row `row` at ring positions first .. first+n-1 (the columns loaded_r maps them
// to), with as few bank switches as possible. Rows below the map read as 0.
// Reads rr_n map bytes from rr_src (ROM bank rr_bank) to rr_dst, rr_src stepping by rr_step
// (+32: next column, -32: previous one).
static const uint8_t *rr_src;
static uint8_t *rr_dst;
static uint8_t rr_bank, rr_n;
static int16_t rr_step;
static void map_row_read(void) __naked {
    __asm
        ldh     a, (__current_bank + 0)
        push    af
        ld      a, (_rr_bank)
        ldh     (__current_bank + 0), a
        ld      (#_rROMB0), a
        ld      hl, #_rr_step
        ld      a, (hl+)
        ld      c, a
        ld      b, (hl)                 ; bc = step
        ld      hl, #_rr_dst
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)                 ; de = destination
        ld      hl, #_rr_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a                    ; hl = source
        ld      a, (_rr_n)
    1$:
        push    af
        ld      a, (hl)
        ld      (de), a
        inc     de
        add     hl, bc
        pop     af
        dec     a
        jr      NZ, 1$
        ld      a, e
        ld      (_rr_dst), a
        ld      a, d
        ld      (_rr_dst + 1), a
        pop     af
        ldh     (__current_bank + 0), a
        ld      (#_rROMB0), a
        ret
    __endasm;
}

void get_map_row_slots(uint8_t first, uint8_t n, uint8_t row, uint16_t loaded_r, uint8_t reversed,
                       const uint8_t *map, uint16_t map_w, uint8_t map_bank, uint8_t *out) {
  if (row >= MAP_ROWS) {
    memset(out, 0, n);
    return;
  }
  uint8_t n0 = n;
  // Consecutive ring positions are consecutive columns (backwards in mirror mode), except where
  // the ring wraps: read runs of columns (cut at the wrap and at ROM bank boundaries) with
  // map_row_read. Column by column in C this took ~0.8k dots per position, ~1/10 of a DMG
  // frame per row job, and a long climb has a row job almost every frame.
  uint8_t slot = reversed ? (uint8_t)(-(int8_t)first & 15u) : first;
  uint16_t c = loaded_r - (uint8_t)((uint8_t)((uint8_t)loaded_r - slot) & 15u);
  uint16_t lo = loaded_r - 15u;     // the ring holds columns lo .. loaded_r
  rr_dst = out;
  rr_step = reversed ? -(int16_t)MAP_ROWS : (int16_t)MAP_ROWS;
  while (n) {
    // run: up to the ring's end and the bank's end in this direction
    uint16_t in_bank = (uint16_t)c & (MAP_BANK_COLS - 1u);
    uint16_t run = reversed ? (uint16_t)(c - lo + 1u) : (uint16_t)(loaded_r - c + 1u);
    uint16_t to_bank = reversed ? in_bank + 1u : MAP_BANK_COLS - in_bank;
    if (to_bank < run) run = to_bank;
    if (run > n) run = n;
    if (c < map_w) {
      rr_src = MAP_COL(map, c) + row;
      rr_bank = MAP_BANK(map_bank, c);
      rr_n = (uint8_t)run;
      map_row_read();
    } else {
      memset(rr_dst, 0, (uint8_t)run);
      rr_dst += (uint8_t)run;
    }
    n -= (uint8_t)run;
    if (!reversed) c = (c + run > loaded_r) ? lo : c + run;
    else c = (c - run < lo || c < run) ? loaded_r : c - run;
  }
  // broken cells of this row in the ring: column c is at ring position c & 15 (mirrored: -c & 15)
  for (uint8_t i = 0; i < broken_n; i++) {
    uint16_t bc = broken_col[i];
    if (broken_row[i] != row || bc > loaded_r || bc < lo) continue;
    uint8_t pos = reversed ? (uint8_t)(-(int8_t)bc & 15u) : (uint8_t)(bc & 15u);
    uint8_t k = (uint8_t)((uint8_t)(pos - first) & 15u);
    if (k < n0) out[k] = 0;
  }
}

uint8_t get_map_tile(uint16_t col, uint8_t row, const uint8_t *map, uint8_t map_bank) {
  if (row >= MAP_ROWS) return 0;
  uint8_t _prev = _current_bank;
  SWITCH_ROM(MAP_BANK(map_bank, col));
  uint8_t id = MAP_COL(map, col)[row];
  SWITCH_ROM(_prev);
  return id;
}


#include "hUGEDriver.h"
#include "music_beats.h"

extern uint8_t music_ready;
extern uint8_t current_song_bank;
extern volatile uint8_t current_music_divider;
extern volatile uint8_t sample_playing;
extern volatile uint8_t sample_keeps_music;

#include "settings.h"

void init_music_banked(const hUGESong_t * song, uint8_t bank, uint8_t divider) {
    uint8_t _prev = _current_bank;
    disable_interrupts();
    music_ready = 0;
    sample_playing = 0;
    sample_keeps_music = 0;
    current_song_bank = bank;
    current_music_divider = divider;
    music_driver = MUSIC_HUGE;
    SWITCH_ROM(bank);
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    hUGE_init(song);
    music_ticks = 0;
    music_beats_start(song);
    fur_beats_start(FUR_NONE);
    if (setting_music_enabled) {
        hUGE_mute_channel(HT_CH1, HT_CH_PLAY);
        hUGE_mute_channel(HT_CH2, HT_CH_PLAY);
        hUGE_mute_channel(HT_CH3, HT_CH_PLAY);
        hUGE_mute_channel(HT_CH4, HT_CH_PLAY);
    } else {
        hUGE_mute_channel(HT_CH1, HT_CH_MUTE);
        hUGE_mute_channel(HT_CH2, HT_CH_MUTE);
        hUGE_mute_channel(HT_CH3, HT_CH_MUTE);
        hUGE_mute_channel(HT_CH4, HT_CH_MUTE);
        NR12_REG = 0; NR14_REG = 0x80;
        NR22_REG = 0; NR24_REG = 0x80;
        NR30_REG = 0;
        NR42_REG = 0; NR44_REG = 0x80;
    }
    hUGE_reset_wave();
    TMA_REG = divider;
    TIMA_REG = divider;
    IF_REG &= ~TIM_IFLAG;
    TAC_REG = 0x04;
    enable_interrupts();
    SWITCH_ROM(_prev);
    music_ready = setting_music_enabled ? 1 : 0;
}
