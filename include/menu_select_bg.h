#ifndef MENU_SELECT_BG_H
#define MENU_SELECT_BG_H

#include <stdint.h>
#include <gbdk/platform.h>

#define menu_select_bg_TILE_ORIGIN 0
#define menu_select_bg_TILE_W 8
#define menu_select_bg_TILE_H 8
#define menu_select_bg_WIDTH 160
#define menu_select_bg_HEIGHT 144
#define menu_select_bg_TILE_COUNT 65
#define menu_select_bg_PALETTE_COUNT 1
#define menu_select_bg_COLORS_PER_PALETTE 4
#define menu_select_bg_TOTAL_COLORS 4
#define menu_select_bg_MAP_ATTRIBUTES 0

extern const unsigned char menu_select_bg_map[360];
BANKREF_EXTERN(menu_select_bg)
extern const palette_color_t menu_select_bg_palettes[4];
extern const uint8_t menu_select_bg_tiles[1040];

#endif
