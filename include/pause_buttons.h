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

#define PAUSE_BTN_MENU 0
#define PAUSE_BTN_PLAY 1
#define PAUSE_BTN_RESTART 2

#define PAUSE_SPRITE_TILE_BASE 144
#define PAUSE_CURSOR_TILE_BASE 156

extern const uint8_t pause_button_tiles[PAUSE_BTN_TILE_COUNT * 16];

void load_pause_button_tiles(void) BANKED;
void init_pause_tiles(void) BANKED;
void draw_pause_menu_sprites(uint8_t selected_btn) BANKED;
void apply_pause_box_attributes(uint8_t apply) BANKED;

#endif
