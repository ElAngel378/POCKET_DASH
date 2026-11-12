#ifndef BG_STREAM_H
#define BG_STREAM_H

#include <gb/gb.h>
#include <stdint.h>

// Background tile streaming (src/graphics/bg_stream.c, tools/bg_stream.py)

// the level's metatile tables (mt_renderer.c): normal, mirrored (left / right swapped)
extern uint8_t mt_ram[2][256][4];

// Tiles ready for the VBlank handler: bs_up_slot[i] gets bs_up_buf[16 i ..] for i in bs_up_i .. bs_up_n - 1
#define BS_UP_MAX 4
extern volatile uint8_t bs_up_i, bs_up_n;
extern uint8_t bs_up_slot[BS_UP_MAX];
extern uint8_t bs_up_buf[BS_UP_MAX * 16];

// Spinning objects share the saws' VBlank DMA (only while the saws are idle: saw_anim_request)
extern uint8_t bs_spin_n;    // the level's spinning tiles (CGB; 0: none)
void bg_stream_spin(void) BANKED;

// Breakable bricks (bg_stream.c): broken ones read as empty until broken_reset (level start, respawn)
extern uint8_t broken_n;
void broken_reset(void) BANKED;
void brick_break(uint16_t col, uint8_t row) BANKED;
void broken_fix_ring(uint16_t col, uint8_t *ids) BANKED;
void player_columns_load(uint16_t col) BANKED;

// Fading objects (bg_stream.c)
extern uint8_t fade_n;
void fade_hide(uint16_t col, uint8_t *ids, uint8_t n) BANKED;
void fade_row(uint8_t first, uint8_t n, uint8_t row, uint16_t loaded_r, uint8_t reversed, uint8_t *ids) BANKED;
uint8_t col_job_next(void) BANKED;
void fade_reset_view(void) BANKED;
void fade_update(uint8_t scx) BANKED;
void fade_cells_copy(uint8_t ly_end);
BANKREF_EXTERN(fade_cells_copy)
extern volatile uint8_t bg_fj_n;
extern uint8_t mj_col[16], mj_out[128];
void fade_build(uint16_t col, uint8_t reversed, uint8_t r_start, uint8_t r_end) BANKED;

// The calls below are banked: callers check these first, so a frame with nothing to stream pays
// nothing (DMG frames are full)
extern uint8_t bs_q_n;       // tiles queued for bg_stream_feed
extern uint16_t bs_col;      // the next column with records to apply (bg_stream_to)
#define BG_STREAM_TO(col) do { if (bs_col <= (col)) bg_stream_to(col); } while (0)

// A level's tiles are (re)loaded (apply_level_bg_tiles): the stream starts again from column 0.
// tables_kept: mt_ram was not rebuilt (the same level again)
void bg_stream_reset(uint8_t level, uint8_t reversed, uint8_t tables_kept) BANKED;
// Applies the records of the columns up to `col` (display off: uploads them at once)
void bg_stream_to(uint16_t col) BANKED;
// Display off, after bg_stream_reset: back to the column the stream had reached (DMG mirror
// portal: the tiles in the other orientation)
void bg_stream_resume(void) BANKED;
// (src/bg_stream_home.c)
#define BSU_MAX 8
extern uint8_t bsu_n, bsu_bank, bsu_skip, bsu_m, bsu_copy, bsu_tbank;
extern uint16_t bsu_base;
extern const uint8_t *bsu_src;
extern uint8_t bsu_out[BSU_MAX * 3];
void bs_apply(void) NONBANKED;
extern uint8_t *bsf_dst;
extern uint8_t bsf_n;
void bs_flip_tiles(void) NONBANKED;
// DMG mirror portal (display off): the background tiles in VRAM mirrored in place, and what the
// stream uploads from now on in the new orientation
void bg_stream_mirror(uint8_t reversed) BANKED;
// Once a frame: the next queued tiles to the VBlank handler
void bg_stream_feed(void) BANKED;

#endif
