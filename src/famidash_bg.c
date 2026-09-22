#pragma bank 10

#include <gb/gb.h>

#include "famidash_bg.h"
#include "fade.h"
#include "bg_parallax.h"

/**
 * NES Master Palette mapped to GBC 15-bit RGB.
 * 64 colors (4 rows of 16).
 */
static const uint16_t nes_master_palette[64] = {
    // Row 0 (0x00 - 0x0F): Dark
    RGB(10, 10, 10), RGB(0, 0, 17), RGB(1, 2, 18), RGB(6, 0, 17),
    RGB(8, 0, 12), RGB(11, 0, 6), RGB(20, 0, 0), RGB(7, 3, 0),
    RGB(4, 5, 0), RGB(1, 7, 0), RGB(0, 8, 0), RGB(0, 7, 0),
    RGB(0, 6, 7), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0),

    // Row 1 (0x10 - 0x1F): Medium/Dark
    RGB(22, 22, 22), RGB(0, 7, 19), RGB(6, 6, 29), RGB(11, 3, 28),
    RGB(27, 0, 25), RGB(20, 2, 12), RGB(27, 0, 0), RGB(15, 7, 0),
    RGB(10, 11, 0), RGB(5, 14, 0), RGB(1, 15, 0), RGB(0, 14, 5),
    RGB(0, 12, 15), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0),

    // Row 2 (0x20 - 0x2F): Bright
    RGB(31, 31, 31), RGB(7, 23, 31), RGB(15, 15, 31), RGB(22, 12, 31),
    RGB(31, 7, 21), RGB(31, 11, 22), RGB(31, 13, 12), RGB(31, 15, 0),
    RGB(27, 20, 0), RGB(14, 24, 0), RGB(0, 28, 0), RGB(7, 25, 13),
    RGB(0, 29, 27), RGB(7, 7, 7), RGB(0, 0, 0), RGB(0, 0, 0),

    // Row 3 (0x30 - 0x3F): Pale
    RGB(31, 31, 31), RGB(21, 25, 31), RGB(23, 23, 31), RGB(26, 22, 31),
    RGB(31, 21, 31), RGB(31, 21, 26), RGB(31, 22, 22), RGB(31, 27, 15),
    RGB(25, 26, 15), RGB(22, 27, 15), RGB(21, 28, 18), RGB(19, 28, 22),
    RGB(20, 26, 28), RGB(20, 20, 20), RGB(0, 0, 0), RGB(0, 0, 0)
};

/**
 * Vibrant GBC Palettes moved local for maximum DMG performance
 */
static const uint16_t vibrant_palette_default[16] = {
    RGB(0, 7, 19), RGB(0, 0, 17), RGB(0, 0, 0), RGB(31, 31, 31), // palette 0
    RGB(0, 7, 19), RGB(0, 0, 17), RGB(0, 7, 19), RGB(31, 31, 31), // palette 1
    RGB(0, 7, 19), RGB(0, 0, 17), RGB(0, 0, 0), RGB(15, 31, 0), // palette 2: bg spikes accent (lime, matches cube)
    RGB(0, 7, 19), RGB(0, 0, 17), RGB(0, 0, 0), RGB(0, 0, 0)  // palette 3
};

static palette_color_t famidash_bg_palettes[16];

static palette_color_t famidash_darker(palette_color_t color) {
    return RGB(((color & 0x1Fu) * 4u / 5u),
               (((color >> 5) & 0x1Fu) * 4u / 5u),
               (((color >> 10) & 0x1Fu) * 4u / 5u));
}

static palette_color_t famidash_bg_border(palette_color_t color) {
    return RGB((((color & 0x1Fu) * 22u) >> 5),
               ((((color >> 5) & 0x1Fu) * 22u) >> 5),
               ((((color >> 10) & 0x1Fu) * 22u) >> 5));
}

static palette_color_t famidash_bg_body(palette_color_t color) {
    return RGB((((color & 0x1Fu) * 27u) >> 5),
               ((((color >> 5) & 0x1Fu) * 27u) >> 5),
               ((((color >> 10) & 0x1Fu) * 27u) >> 5));
}

static palette_color_t famidash_bg_shadow(palette_color_t color) {
    return RGB((((color & 0x1Fu) * 19u) >> 5),
               ((((color >> 5) & 0x1Fu) * 19u) >> 5),
               ((((color >> 10) & 0x1Fu) * 19u) >> 5));
}

static void famidash_update_parallax_palette(palette_color_t sky_color) {
    palette_color_t pal[4];
    pal[0] = sky_color;
    pal[1] = famidash_bg_border(sky_color);
    pal[2] = famidash_bg_body(sky_color);
    pal[3] = famidash_bg_shadow(sky_color);
    fade_buffer_bkg_palette(3, 1, pal);
}

static palette_color_t ground_palette[4];

static void update_ground_palette(palette_color_t bg_color, palette_color_t g_color) {
    if (_cpu != CGB_TYPE) return;
    ground_palette[0] = bg_color;
    ground_palette[1] = g_color;
    ground_palette[2] = RGB((((g_color & 0x1Fu) * 18u) >> 5),
                            ((((g_color >> 5) & 0x1Fu) * 18u) >> 5),
                            ((((g_color >> 10) & 0x1Fu) * 18u) >> 5));
    ground_palette[3] = RGB((((g_color & 0x1Fu) * 9u) >> 5),
                            ((((g_color >> 5) & 0x1Fu) * 9u) >> 5),
                            ((((g_color >> 10) & 0x1Fu) * 9u) >> 5));
    fade_buffer_bkg_palette(4, 1, ground_palette);
}

void famidash_apply_bg_trigger(uint8_t color_id) BANKED {
    palette_color_t color;

    if (color_id == 31u) color = RGB(0, 29, 27); /* FamiDash $9F: Use Aqua as default player color */
    else if (color_id == 46u) {                   /* FamiDash $AE: Ground Color 2 Trigger */
        color = RGB(0, 28, 0); /* Neon Green */
        famidash_bg_palettes[6] = color;
        famidash_bg_palettes[5] = famidash_darker(color);
        fade_buffer_bkg_palette(1, 1, &famidash_bg_palettes[4]);
        update_ground_palette(famidash_bg_palettes[0], color);
        return;
    } else {
        // Fast local lookup
        color = nes_master_palette[color_id & 0x3Fu];
    }

    famidash_bg_palettes[0] = color;
    famidash_bg_palettes[4] = color;
    famidash_bg_palettes[8] = color;
    famidash_bg_palettes[12] = color;
    color = famidash_darker(color);
    famidash_bg_palettes[1] = color;
    // famidash_bg_palettes[5] is preserved for ground darker color
    famidash_bg_palettes[9] = color;
    famidash_bg_palettes[13] = color;
    fade_buffer_bkg_palette(0, 3, famidash_bg_palettes);
    famidash_update_parallax_palette(famidash_bg_palettes[0]);
    update_ground_palette(famidash_bg_palettes[0], famidash_bg_palettes[6]);
}

void famidash_apply_g_trigger(uint8_t color_id) BANKED {
    palette_color_t color;

    if (color_id == 31u) color = RGB(0, 29, 27); /* FamiDash $9F: Aqua */
    else if (color_id == 46u) color = RGB(0, 28, 0); /* FamiDash $AE: Neon Green */
    else color = nes_master_palette[color_id & 0x3Fu];

    famidash_bg_palettes[6] = color;
    famidash_bg_palettes[5] = famidash_darker(color);
    fade_buffer_bkg_palette(1, 1, &famidash_bg_palettes[4]);
    update_ground_palette(famidash_bg_palettes[0], color);
}

static const uint8_t level_initial_bg_color[11] = {
    17, // Stereo Madness: Blue
    20, // Back On Track: Magenta
    42, // Polargeist: Green
    22, // Dry Out: Red
    17, // Base After Base: Blue
    20, // Cant Let Go: Magenta
    19, // Jumper: Purple
    42, // Time Machine: Green
    4,  // Cycles: Dark Violet
    28, // xStep: Cyan
    17  // Ultimate Destruction: Blue
};

static const uint8_t level_initial_g_color[11] = {
    46, // Stereo Madness: Neon Green
    20, // Back On Track: Magenta
    26, // Polargeist: Medium Green
    22, // Dry Out: Red
    17, // Base After Base: Blue
    4,  // Cant Let Go: Dark Violet
    19, // Jumper: Purple
    26, // Time Machine: Medium Green
    20, // Cycles: Magenta
    12, // xStep: Dark Cyan
    17  // Ultimate Destruction: Blue
};

void famidash_reset_bg_palettes(uint8_t idx) BANKED {
    uint8_t i;
    if (idx >= 11) idx = 0;
    for (i = 0; i != 16; i++) famidash_bg_palettes[i] = vibrant_palette_default[i];
    fade_set_bkg_palette(0, 3, famidash_bg_palettes);
    famidash_apply_bg_trigger(level_initial_bg_color[idx]);
    famidash_apply_g_trigger(level_initial_g_color[idx]);
    fade_apply_dirty_palettes();
}
