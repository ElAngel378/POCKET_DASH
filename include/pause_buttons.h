#ifndef PAUSE_BUTTONS_H
#define PAUSE_BUTTONS_H

#include <stdint.h>
#include <gb/gb.h>

#define PAUSE_BTN_TILE_BASE 36
#define PAUSE_BTN_TILE_COUNT 40

// Tile offsets within pause_button_tiles (in 8x8 tile units, step by 4 for 8x16 columns)
// Play: 8 sprites (16 tiles: 0..15)
// Menu: 6 sprites (12 tiles: 16..27)
// Restart: 6 sprites (12 tiles: 28..39)

#define BTN_PLAY_TILE_OFFSET 0
#define BTN_MENU_TILE_OFFSET 16
#define BTN_RESTART_TILE_OFFSET 28

extern const uint8_t pause_button_tiles[PAUSE_BTN_TILE_COUNT * 16];

void load_pause_button_tiles(void) BANKED;

#endif
