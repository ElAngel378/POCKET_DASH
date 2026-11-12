#ifndef SETTINGS_BG_H
#define SETTINGS_BG_H

#include <stdint.h>
#include <gb/gb.h>
#include <gb/cgb.h>

#define SETTINGS_BG_TILE_COUNT 31
#define SETTINGS_BG_WIDTH 20
#define SETTINGS_BG_HEIGHT 18
#define SETTINGS_BOARD_BLANK_TILE 0

extern const palette_color_t settings_bg_palettes[32];
extern const uint8_t settings_bg_tiles[496];
extern const unsigned char settings_bg_map[360];
extern const unsigned char settings_bg_attributes[360];

#endif
