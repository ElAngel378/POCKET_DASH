#ifndef LEVEL_COLOURS_H
#define LEVEL_COLOURS_H

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>

// One colour of a level's colour table (src/level_colours.c, made by tools/level_colours.py): the
// exact colour, and the DMG theme (bg_pals index) its sky takes
typedef struct {
    palette_color_t color;
    uint8_t band;
} level_colour_t;

// Each level's table, game_levels order (bank 15: read it from src/famidash_bg.c). A level's start
// colours (Level bg_color, g_color) and its BG / ground triggers (ids 128 + n, 192 + n) are
// positions in it
extern const level_colour_t * const level_colours[];

#endif
