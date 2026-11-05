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
// Once a frame: the next queued tiles to the VBlank handler
void bg_stream_feed(void) BANKED;

#endif
