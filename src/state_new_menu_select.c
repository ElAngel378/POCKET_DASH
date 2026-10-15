#pragma bank 22

#include "states.h"
#include "gameplay.h"
#include "assets.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "fade.h"
#include "hUGEDriver.h"
#include "menu_select_bg.h"
#include "save_manager.h"
#include "scratch.h"
#include <gb/gb.h>
#include <gb/cgb.h>

extern uint8_t selected;
extern uint8_t redraw;
extern uint8_t music_ready;
extern volatile uint8_t current_song_bank;
extern const hUGESong_t menuloop;

// Level background colors
#define NUM_LEVEL_STYLES 14
static const palette_color_t cgb_level_bg_colors[NUM_LEVEL_STYLES] = {
    RGB8(  0,   0, 255), // Stereo Madness
    RGB8(248,   0, 248), // Back On Track
    RGB8(248,   0, 122), // Polargeist
    RGB8(248,   0,   0), // Dry Out
    RGB8(248, 120,   0), // Base After Base
    RGB8(248, 248,   0), // Cant Let Go
    RGB8(  0, 248,   0), // Jumper
    RGB8(  0, 248, 248), // Time Machine
    RGB8(  0, 122, 248), // Cycles
    RGB8(  0,   0, 255), // xStep
    RGB8(230,  50,  80), // Clutterfunk
    RGB8(248,   0, 248), // Ultimate Destruction
    RGB8(176,   0,  64), // Theory of Everything (Famidash start colour $05)
    RGB8(  0,   0, 255), // Test
};

static const uint8_t level_difficulties[NUM_LEVEL_STYLES] = {
    1, // Stereo Madness
    1, // Back On Track
    2, // Polargeist
    2, // Dry Out
    3, // Base After Base
    3, // Cant Let Go
    4, // Jumper
    4, // Time Machine
    4, // Cycles
    5, // xStep
    5, // Clutterfunk
    6, // Ultimate Destruction
    5, // Theory of Everything
    1  // Test
};

// Difficulty faces (16x16, drawn as 4 sprites): Auto, Easy, Normal, Hard, Harder, Insane, Demon,
// Insane Demon, NA, Easy Demon
#include "graphics/difficulty_faces.h"

#define FACE_SPR_TILE   8     // sprite tiles 8..11 (TL, TR, BL, BR)
#define FACE_SPR_OAM    12    // OAM slots 12..15
#define FACE_SPR_X      28    // 4 px left of the old background tiles (column 4)
#define FACE_SPR_Y      48    // row 6
static inline palette_color_t get_box_tint(palette_color_t c) {
    uint8_t r = (uint8_t)(((c & 0x1F) * 7) / 31);
    uint8_t g = (uint8_t)((((c >> 5) & 0x1F) * 7) / 31);
    uint8_t b = (uint8_t)((((c >> 10) & 0x1F) * 7) / 31);
    return RGB(r, g, b);
}

static palette_color_t lerp_color(palette_color_t c1, palette_color_t c2, uint8_t step, uint8_t max_steps) {
    if (step >= max_steps) return c2;
    if (step == 0) return c1;

    int16_t r1 = (int16_t)(c1 & 0x1F);
    int16_t g1 = (int16_t)((c1 >> 5) & 0x1F);
    int16_t b1 = (int16_t)((c1 >> 10) & 0x1F);

    int16_t r2 = (int16_t)(c2 & 0x1F);
    int16_t g2 = (int16_t)((c2 >> 5) & 0x1F);
    int16_t b2 = (int16_t)((c2 >> 10) & 0x1F);

    uint8_t r = (uint8_t)(r1 + ((r2 - r1) * (int16_t)step) / (int16_t)max_steps);
    uint8_t g = (uint8_t)(g1 + ((g2 - g1) * (int16_t)step) / (int16_t)max_steps);
    uint8_t b = (uint8_t)(b1 + ((b2 - b1) * (int16_t)step) / (int16_t)max_steps);

    return RGB(r, g, b);
}

static palette_color_t cgb_menu_pals[32] = {
    0, RGB8(60, 245, 230), RGB8(15, 110, 115), RGB8(0, 0, 0),        // Pal 0: Cyan Top/Bottom
    0, 0,                  RGB8(180, 215, 255), RGB8(255, 255, 255),  // Pal 1: Center Box
    0, RGB8(84, 216, 0),   RGB8(255, 255, 255), RGB8(0, 0, 0),        // Pal 2: Normal Bar
    0, RGB8(0, 168, 252),  RGB8(255, 255, 255), RGB8(0, 0, 0),        // Pal 3: Practice Bar
    0, 0,                  RGB8(255, 255, 255), RGB8(0, 0, 0),        // Pal 4: unused (face is sprites)
    0, RGB8(189, 242, 71), RGB8(67, 156, 24),  RGB8(0, 0, 0),        // Pal 5: Green Blocks
    0, RGB8(0, 0, 0),      RGB8(180, 215, 255), RGB8(255, 255, 255),  // Pal 6: Headers & BG
    0, RGB8(5, 210, 253),  RGB8(255, 255, 255), RGB8(0, 0, 0),        // Pal 7: Info Button
};

static void apply_cgb_palettes(palette_color_t bg_col, uint8_t level_idx) {
    (void)level_idx;
    if (_cpu == CGB_TYPE) {
        palette_color_t box_bg = get_box_tint(bg_col);
        cgb_menu_pals[0]  = bg_col;
        cgb_menu_pals[4]  = bg_col;
        cgb_menu_pals[5]  = box_bg;
        cgb_menu_pals[8]  = box_bg;
        cgb_menu_pals[12] = box_bg;
        cgb_menu_pals[16] = box_bg;
        cgb_menu_pals[20] = bg_col;
        cgb_menu_pals[24] = bg_col;
        cgb_menu_pals[28] = bg_col;

        fade_set_bkg_palette(0, 8, cgb_menu_pals);

        palette_color_t cap_pals[8];
        // Normal Bar Cap Palette
        cap_pals[0] = RGB8(0, 0, 0);
        cap_pals[1] = RGB8(84, 216, 0);
        cap_pals[2] = box_bg;
        cap_pals[3] = RGB8(0, 0, 0);
        // Practice Bar Cap Palette
        cap_pals[4] = RGB8(0, 0, 0);
        cap_pals[5] = RGB8(0, 168, 252);
        cap_pals[6] = box_bg;
        cap_pals[7] = RGB8(0, 0, 0);
        fade_set_sprite_palette(1, 2, cap_pals);
    }
}

static void setup_cgb_attributes(void) {
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 6);

        // Top banner attributes
        set_bkg_tile_xy(5, 0, 0);
        fill_bkg_rect(6, 0, 3, 1, 5);
        set_bkg_tile_xy(9, 0, 0);
        set_bkg_tile_xy(10, 0, 0);
        fill_bkg_rect(11, 0, 3, 1, 5);
        set_bkg_tile_xy(14, 0, 0);

        // Back button
        fill_bkg_rect(1, 1, 2, 2, 5);

        // Info button
        fill_bkg_rect(17, 1, 2, 2, 7);

        // Center Box
        fill_bkg_rect(3, 4, 14, 6, 1);

        // Normal Mode header & bar
        fill_bkg_rect(3, 11, 14, 1, 6);
        fill_bkg_rect(4, 12, 12, 1, 2);

        // Practice Mode header & bar
        fill_bkg_rect(3, 13, 14, 1, 6);
        fill_bkg_rect(4, 14, 12, 1, 3);

        // Bottom stage blocks
        set_bkg_tile_xy(0, 15, 0);
        set_bkg_tile_xy(0, 16, 5);
        set_bkg_tile_xy(0, 17, 0);
        set_bkg_tile_xy(1, 17, 5);
        set_bkg_tile_xy(2, 17, 0);

        set_bkg_tile_xy(19, 15, 0);
        set_bkg_tile_xy(19, 16, 5);
        set_bkg_tile_xy(19, 17, 0);
        set_bkg_tile_xy(18, 17, 5);
        set_bkg_tile_xy(17, 17, 0);

        VBK_REG = 0;
    }
}

extern const unsigned char FontPusab[];

static void setup_menu_select_font(void) {
    // Remap font background color 0 to color 1 to match box interior
    uint8_t tile_buf[16];
    for (uint8_t t = 0; t < 39; t++) {
        const uint8_t *src = &FontPusab[t * 16];
        for (uint8_t r = 0; r < 8; r++) {
            uint8_t b0 = src[2 * r];
            uint8_t b1 = src[2 * r + 1];
            tile_buf[2 * r] = b0 | (uint8_t)(~(b0 | b1));
            tile_buf[2 * r + 1] = b1;
        }
        set_bkg_data(FONT_PUSAB_START + t, 1, tile_buf);
    }
}

// 4 tiles (8x32) generated from arrow.png:
static const uint8_t arrow_sprite_tiles[64] = {
    // Tile 0 (top of left arrow)
    0x01, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x03, 0x07, 0x05, 0x07, 0x05, 0x0f, 0x09, 0x0f, 0x09,
    // Tile 1
    0x1f, 0x11, 0x1f, 0x11, 0x3f, 0x21, 0x3f, 0x21, 0x7f, 0x41, 0x7f, 0x41, 0xff, 0x81, 0xff, 0x81,
    // Tile 2
    0xff, 0x81, 0xff, 0x81, 0x7f, 0x41, 0x7f, 0x41, 0x3f, 0x21, 0x3f, 0x21, 0x1f, 0x11, 0x1f, 0x11,
    // Tile 3 (bottom of left arrow)
    0x0f, 0x09, 0x0f, 0x09, 0x07, 0x05, 0x07, 0x05, 0x03, 0x03, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01,
};

static const uint8_t cap_sprite_tiles[64] = {
    // Tile 4: Left Cap Empty (0=trans, 2=box_bg, 3=black)
    0x3f, 0x3f, 0x40, 0x7f, 0x80, 0xff, 0x80, 0xff, 0x80, 0xff, 0x80, 0xff, 0x40, 0x7f, 0x3f, 0x3f,
    // Tile 5: Left Cap Filled (0=trans, 1=fill, 3=black)
    0x3f, 0x3f, 0x7f, 0x40, 0xff, 0x80, 0xff, 0x80, 0xff, 0x80, 0xff, 0x80, 0x7f, 0x40, 0x3f, 0x3f,
    // Tile 6: Right Cap Empty (0=trans, 2=box_bg, 3=black)
    0xfc, 0xfc, 0x02, 0xfe, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x02, 0xfe, 0xfc, 0xfc,
    // Tile 7: Right Cap Filled (0=trans, 1=fill, 3=black)
    0xfc, 0xfc, 0xfe, 0x02, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xfe, 0x02, 0xfc, 0xfc
};

static void update_cap_sprite_positions(uint8_t scx) {
    // Difficulty face (scrolls with the level banner)
    uint8_t fx = (uint8_t)((uint8_t)(FACE_SPR_X - scx) + 8);
    move_sprite(FACE_SPR_OAM,     fx,                 FACE_SPR_Y + 16);
    move_sprite(FACE_SPR_OAM + 1, (uint8_t)(fx + 8),  FACE_SPR_Y + 16);
    move_sprite(FACE_SPR_OAM + 2, fx,                 FACE_SPR_Y + 24);
    move_sprite(FACE_SPR_OAM + 3, (uint8_t)(fx + 8),  FACE_SPR_Y + 24);

    uint8_t lx = (uint8_t)((uint8_t)(24 - scx) + 8);
    uint8_t rx = (uint8_t)((uint8_t)(128 - scx) + 8);
    move_sprite(8, lx, 112);
    move_sprite(9, rx, 112);
    move_sprite(10, lx, 128);
    move_sprite(11, rx, 128);
}

static void setup_arrow_sprites(void) {
    SPRITES_8x8;
    set_sprite_data(0, 4, arrow_sprite_tiles);
    set_sprite_data(4, 4, cap_sprite_tiles);

    // Left arrow
    move_sprite(0, 16, 72); set_sprite_tile(0, 0); set_sprite_prop(0, 0);
    move_sprite(1, 16, 80); set_sprite_tile(1, 1); set_sprite_prop(1, 0);
    move_sprite(2, 16, 88); set_sprite_tile(2, 2); set_sprite_prop(2, 0);
    move_sprite(3, 16, 96); set_sprite_tile(3, 3); set_sprite_prop(3, 0);

    // Right arrow (flipped)
    move_sprite(4, 152, 72); set_sprite_tile(4, 0); set_sprite_prop(4, S_FLIPX);
    move_sprite(5, 152, 80); set_sprite_tile(5, 1); set_sprite_prop(5, S_FLIPX);
    move_sprite(6, 152, 88); set_sprite_tile(6, 2); set_sprite_prop(6, S_FLIPX);
    move_sprite(7, 152, 96); set_sprite_tile(7, 3); set_sprite_prop(7, S_FLIPX);

    // Progress bar end caps
    move_sprite(8, 32, 112); set_sprite_tile(8, 4); set_sprite_prop(8, 1 | S_PALETTE);
    move_sprite(9, 136, 112); set_sprite_tile(9, 6); set_sprite_prop(9, 1 | S_PALETTE);
    move_sprite(10, 32, 128); set_sprite_tile(10, 4); set_sprite_prop(10, 2 | S_PALETTE);
    move_sprite(11, 136, 128); set_sprite_tile(11, 6); set_sprite_prop(11, 2 | S_PALETTE);

    // Difficulty face: tiles TL, TR, BL, BR; CGB palettes 3 (top row) / 4 (bottom row)
    for (uint8_t i = 0; i < 4; i++) {
        set_sprite_tile(FACE_SPR_OAM + i, FACE_SPR_TILE + i);
        set_sprite_prop(FACE_SPR_OAM + i, (_cpu == CGB_TYPE) ? (uint8_t)((3 + (i >> 1)) | S_PALETTE) : 0);
    }
    update_cap_sprite_positions(0);

    for (uint8_t s = 16; s < 40; s++) hide_sprite(s);

    if (_cpu == CGB_TYPE) {
        palette_color_t obj_pals[12];
        palette_color_t box_bg = get_box_tint(cgb_level_bg_colors[selected % NUM_LEVEL_STYLES]);

        // Pal 0: Arrows
        obj_pals[0] = RGB8(0, 0, 0);
        obj_pals[1] = RGB8(255, 255, 255);
        obj_pals[2] = RGB8(255, 255, 255);
        obj_pals[3] = RGB8(0, 0, 0);

        // Pal 1: Normal Bar Caps
        obj_pals[4] = RGB8(0, 0, 0);
        obj_pals[5] = RGB8(84, 216, 0);
        obj_pals[6] = box_bg;
        obj_pals[7] = RGB8(0, 0, 0);

        // Pal 2: Practice Bar Caps
        obj_pals[8] = RGB8(0, 0, 0);
        obj_pals[9] = RGB8(0, 168, 252);
        obj_pals[10] = box_bg;
        obj_pals[11] = RGB8(0, 0, 0);

        fade_set_sprite_palette(0, 3, obj_pals);
    }   // DMG: OBP0 / OBP1 set with the BGP (fade_set_dmg_palettes)
    SHOW_SPRITES;
}

static uint8_t get_name_length(const char *s) {
    uint8_t len = 0;
    while (s[len]) len++;
    return len;
}

static void draw_menu_text(uint8_t x, uint8_t y, const char *str) {
    while (*str) {
        char c = *str;
        if (c == ' ') {
            x++;
            str++;
            continue;
        }
        uint8_t tile;
        if (c == '%') tile = 1;
        else if (c == '/') tile = 2;
        else if (c >= '0' && c <= '9') tile = (c - '0') + 3;
        else if (c >= 'A' && c <= 'Z') tile = (c - 'A') + 13;
        else if (c >= 'a' && c <= 'z') tile = (c - 'a') + 13;
        else tile = 0;
        set_bkg_tile_xy(x++, y, FONT_PUSAB_START + tile);
        str++;
    }
}

static const uint8_t empty_mask_table[9] = {
    0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01, 0x00
};

static void render_progress_bar(uint8_t pct, uint8_t vram_start_tile) {
    if (pct > 100) pct = 100;

    // in the shared scratch buffer, not on the stack: 192 bytes of it left the music interrupt
    // (and a nested VBlank) too little room, and the stack ran into the variables below it
    uint8_t *tiles_buffer = scratch_buf;

    char text[6];
    uint8_t text_len = 0;
    if (pct >= 100) {
        text[0] = '1'; text[1] = '0'; text[2] = '0'; text[3] = '%'; text[4] = '\0';
        text_len = 4;
    } else if (pct >= 10) {
        text[0] = (char)('0' + (pct / 10));
        text[1] = (char)('0' + (pct % 10));
        text[2] = '%'; text[3] = '\0';
        text_len = 3;
    } else {
        text[0] = (char)('0' + pct);
        text[1] = '%'; text[2] = '\0';
        text_len = 2;
    }

    uint8_t text_start_body = (text_len == 2) ? 5 : 4;
    uint16_t fill_px = ((uint16_t)pct * 96u) / 100u;

    for (uint8_t b = 0; b < 12; b++) {
        uint8_t *t_dst = &tiles_buffer[b * 16];
        uint16_t x_tile_start = (uint16_t)(b * 8);

        uint8_t empty_mask;
        if (fill_px <= x_tile_start) {
            empty_mask = 0xff;
        } else if (fill_px >= (uint16_t)(x_tile_start + 8)) {
            empty_mask = 0x00;
        } else {
            empty_mask = empty_mask_table[fill_px - x_tile_start];
        }

        int8_t char_idx = (int8_t)(b - text_start_body);
        const uint8_t *glyph_src = NULL;
        if (char_idx >= 0 && char_idx < (int8_t)text_len) {
            char c = text[char_idx];
            uint8_t g_idx = (c == '%') ? 1 : (uint8_t)((c - '0') + 3);
            glyph_src = &FontPusab[g_idx * 16];
        }

        t_dst[0] = 0xff;
        t_dst[1] = 0xff;

        if (glyph_src) {
            for (uint8_t r = 1; r <= 6; r++) {
                uint8_t gb0 = glyph_src[2 * r];
                uint8_t gb1 = glyph_src[2 * r + 1];
                uint8_t glyph_mask = (uint8_t)(gb0 | gb1);
                t_dst[2 * r]     = (uint8_t)((gb0 ^ gb1) | ((uint8_t)(~glyph_mask) & (uint8_t)(~empty_mask)));
                // Glyph body is colour 2 (white on GBC). On DMG colour 2 is dark grey,
                // which is unreadable on the grey fill, so use colour 0 (white) there.
                t_dst[2 * r + 1] = (_cpu == CGB_TYPE) ? glyph_mask : (uint8_t)(gb0 ^ gb1);
            }
        } else {
            for (uint8_t r = 1; r <= 6; r++) {
                t_dst[2 * r]     = (uint8_t)(~empty_mask);
                t_dst[2 * r + 1] = 0x00;
            }
        }

        t_dst[14] = 0xff;
        t_dst[15] = 0xff;
    }

    set_bkg_data(vram_start_tile, 12, tiles_buffer);
}

static uint8_t last_rendered_norm = 0xFF;
static uint8_t last_rendered_prac = 0xFF;

static void update_level_progress_bars(uint8_t level_idx) {
    uint8_t norm_p = level_progress_normal[level_idx % NUM_SAVE_LEVELS];
    uint8_t prac_p = level_progress_practice[level_idx % NUM_SAVE_LEVELS];

    if (norm_p != last_rendered_norm) {
        render_progress_bar(norm_p, 0x50);
        last_rendered_norm = norm_p;
    }
    if (prac_p != last_rendered_prac) {
        render_progress_bar(prac_p, 0x60);
        last_rendered_prac = prac_p;
    }

    // Update rounded cap sprite tiles (Left: tile 5 if filled else 4, Right: tile 7 if filled else 6)
    set_sprite_tile(8, (norm_p > 0) ? 5 : 4);
    set_sprite_tile(9, (norm_p >= 100) ? 7 : 6);
    set_sprite_tile(10, (prac_p > 0) ? 5 : 4);
    set_sprite_tile(11, (prac_p >= 100) ? 7 : 6);
}

// Famidash secret coins of the selected level (row 8 of the level box):
// ring = not collected, filled = collected in a completed run.
#define COIN_ICON_TILE 0x70
static const uint8_t coin_icon_tiles[32] = {
    0xC3, 0x3C, 0xBD, 0x42, 0x7E, 0x81, 0x7E, 0x81, 0x7E, 0x81, 0x7E, 0x81, 0xBD, 0x42, 0xC3, 0x3C,
    0xFF, 0x3C, 0xC3, 0x7E, 0x81, 0xFF, 0x99, 0xFF, 0x99, 0xFF, 0x81, 0xFF, 0xC3, 0x7E, 0xFF, 0x3C
};

static void draw_level_coins(uint8_t level_idx) {
    uint8_t c = level_coins[level_idx % NUM_SAVE_LEVELS];
    for (uint8_t i = 0; i < 3; i++) {
        set_bkg_tile_xy((uint8_t)(13 + i), 8, (uint8_t)(COIN_ICON_TILE + ((c >> i) & 1)));
    }
}

// Text columns inside the level box (6..15); column 16 is the right border.
#define MENU_NAME_COLS 10

static uint8_t last_rendered_diff = 0xFF;

static void draw_selected_level(void) {
    // Clear only the text area (columns 6..15) on rows 6 and 7 with box interior tile 0x16.
    // Column 16 is the box's right border and must stay untouched.
    fill_bkg_rect(6, 6, MENU_NAME_COLS, 2, 0x16);
    update_level_progress_bars(selected);
    draw_level_coins(selected);

    // Update difficulty face sprite tiles/palettes only if difficulty changed
    uint8_t diff = level_difficulties[selected % NUM_LEVEL_STYLES];
    if (diff != last_rendered_diff) {
        if (_cpu == CGB_TYPE) {
            set_sprite_data(FACE_SPR_TILE, 4, difficulty_face_cgb_tiles[diff]);
            // fade_set_sprite_palette is banked: pass RAM, not this bank's ROM table
            palette_color_t face_pals[8];
            for (uint8_t i = 0; i < 8; i++) face_pals[i] = difficulty_face_pals[diff][i];
            fade_set_sprite_palette(3, 2, face_pals);
        } else {
            set_sprite_data(FACE_SPR_TILE, 4, difficulty_face_dmg_tiles[diff]);
        }
        last_rendered_diff = diff;
    }

    const char *name = game_levels[selected]->name;
    uint8_t len = get_name_length(name);

    if (len <= MENU_NAME_COLS) {
        draw_menu_text(6, 6, name);
    } else {
        // Wrap onto two lines at the space that balances them best (both lines must fit),
        // or inside the word when there is no usable space.
        uint8_t split = (uint8_t)((len * 5 + 4) / 8);   // ~60/40: CLUTTERFUNK -> CLUTTER / FUNK
        uint8_t best = 0xFF;
        for (uint8_t i = 1; i < len; i++) {
            if (name[i] != ' ') continue;
            uint8_t l2 = len - i - 1;
            if (i > MENU_NAME_COLS || l2 > MENU_NAME_COLS) continue;
            uint8_t diff = (i > l2) ? (uint8_t)(i - l2) : (uint8_t)(l2 - i);
            if (diff < best) { best = diff; split = i; }
        }

        char line1[MENU_NAME_COLS + 1];
        uint8_t n1 = (split > MENU_NAME_COLS) ? MENU_NAME_COLS : split;
        for (uint8_t i = 0; i < n1; i++) line1[i] = name[i];
        line1[n1] = '\0';

        const char *line2 = &name[split];
        while (*line2 == ' ') line2++;

        draw_menu_text(6, 6, line1);
        if (*line2 != '\0') {
            draw_menu_text(6, 7, line2);
        }
    }
}

// Spring animation scroll curves for level select transition
#define SPRING_ANIM_FRAMES  21
#define SPRING_SWAP_FRAME   4

static const uint8_t scx_table_right[SPRING_ANIM_FRAMES] = {
    0, 35, 80, 120, 145, 180, 215, 242, 254, 6, 14, 18, 17, 13, 7, 0, 253, 251, 253, 255, 0
};

static const uint8_t scx_table_left[SPRING_ANIM_FRAMES] = {
    0, 221, 176, 136, 111, 76, 41, 14, 2, 250, 242, 238, 239, 243, 249, 0, 3, 5, 3, 1, 0
};

#define COLOR_FADE_MAX 16

GameState update_new_menu_select_state(void) BANKED {
    init_save_system();
    // load with the display on (off shows white); fade_from_black shows the palettes
    fade_set_black();
    fade_hold = 1;

    SCX_REG = 0;
    SCY_REG = 0;

    HIDE_SPRITES;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
    HIDE_WIN;

    fill_bkg_rect(0, 0, 32, 32, 0);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 6);
        VBK_REG = 0;
    }

    set_bkg_data(0, menu_select_bg_TILE_COUNT, menu_select_bg_tiles);
    set_bkg_data(COIN_ICON_TILE, 2, coin_icon_tiles);
    set_bkg_tiles(0, 0, 20, 18, menu_select_bg_map);

    // The difficulty face is drawn with sprites: blank its background tiles (box interior)
    fill_bkg_rect(4, 6, 2, 2, 0x16);

    // Clear arrow background tiles (rendered via sprites)
    for (uint8_t r = 7; r <= 10; r++) {
        set_bkg_tile_xy(1, r, 0);
        set_bkg_tile_xy(18, r, 0);
    }

    setup_menu_select_font();

    palette_color_t current_bg_color = cgb_level_bg_colors[selected % NUM_LEVEL_STYLES];
    palette_color_t bg_color_from = current_bg_color;
    palette_color_t bg_color_to = current_bg_color;
    uint8_t color_fade_step = COLOR_FADE_MAX;

    if (_cpu == CGB_TYPE) {
        setup_cgb_attributes();
        apply_cgb_palettes(current_bg_color, selected);
        fade_set_bkg_palette(0, 8, cgb_menu_pals);
    }
    // OBP1 = progress bar caps: colour 1 = bar fill (light grey), colour 2 = bar interior (white)
    fade_set_dmg_palettes(0xE4, 0xE0, 0xC4);

    last_rendered_norm = 0xFF;
    last_rendered_prac = 0xFF;
    last_rendered_diff = 0xFF;
    for (uint8_t c = 0; c < 12; c++) {
        set_bkg_tile_xy((uint8_t)(4 + c), 12, (uint8_t)(0x50 + c));
        set_bkg_tile_xy((uint8_t)(4 + c), 14, (uint8_t)(0x60 + c));
    }
    set_bkg_tile_xy(3, 12, 0);
    set_bkg_tile_xy(16, 12, 0);
    set_bkg_tile_xy(3, 14, 0);
    set_bkg_tile_xy(16, 14, 0);

    setup_arrow_sprites();
    draw_selected_level();

    SHOW_BKG;
    DISPLAY_ON;
    fade_from_black(2);

    // Scanline split: scanlines 32..120 scroll with level banner
    level_banner_scx = 0;
    disable_interrupts();
    add_VBL(level_select_vbl_isr);
    add_LCD(level_select_stat_isr);
    STAT_REG |= STATF_LYC;
    LYC_REG = 30;   // one line early: level_select_stat_isr waits for line 31
    set_interrupts(VBL_IFLAG | LCD_IFLAG | TIM_IFLAG);
    enable_interrupts();

    uint8_t animating = 0;
    int8_t anim_dir = 0;
    uint8_t anim_frame = 0;
    uint8_t prev_joy = joypad();
    uint8_t hold_timer = 0;

    while (1) {
        // Background color fade
        if (color_fade_step < COLOR_FADE_MAX) {
            color_fade_step++;
            current_bg_color = lerp_color(bg_color_from, bg_color_to, color_fade_step, COLOR_FADE_MAX);
            apply_cgb_palettes(current_bg_color, selected);
            if (color_fade_step == COLOR_FADE_MAX && _cpu == CGB_TYPE) {
                fade_set_bkg_palette(0, 8, cgb_menu_pals);
                palette_color_t box_bg = get_box_tint(current_bg_color);
                palette_color_t cap_pals[8];
                cap_pals[0] = RGB8(0, 0, 0);
                cap_pals[1] = RGB8(84, 216, 0);
                cap_pals[2] = box_bg;
                cap_pals[3] = RGB8(0, 0, 0);
                cap_pals[4] = RGB8(0, 0, 0);
                cap_pals[5] = RGB8(0, 168, 252);
                cap_pals[6] = box_bg;
                cap_pals[7] = RGB8(0, 0, 0);
                fade_set_sprite_palette(1, 2, cap_pals);
            }
        }

        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;

        // Auto-repeat when holding directional buttons
        uint8_t repeat = 0;
        if (joy & (J_RIGHT | J_DOWN | J_LEFT | J_UP)) {
            hold_timer++;
            if (hold_timer >= 18 && (hold_timer % 7) == 0) {
                repeat = 1;
            }
        } else {
            hold_timer = 0;
        }

        if ((pressed & (J_RIGHT | J_DOWN)) || (repeat && (joy & (J_RIGHT | J_DOWN)))) {
            if (selected < MAX_LEVELS - 1) selected++;
            else selected = 0;

            bg_color_from = current_bg_color;
            bg_color_to = cgb_level_bg_colors[selected % NUM_LEVEL_STYLES];
            color_fade_step = 0;

            anim_dir = 1;
            if (!animating || anim_frame >= SPRING_SWAP_FRAME) {
                animating = 1;
                anim_frame = 0;
            }
        } else if ((pressed & (J_LEFT | J_UP)) || (repeat && (joy & (J_LEFT | J_UP)))) {
            if (selected > 0) selected--;
            else selected = MAX_LEVELS - 1;

            bg_color_from = current_bg_color;
            bg_color_to = cgb_level_bg_colors[selected % NUM_LEVEL_STYLES];
            color_fade_step = 0;

            anim_dir = -1;
            if (!animating || anim_frame >= SPRING_SWAP_FRAME) {
                animating = 1;
                anim_frame = 0;
            }
        } else if (pressed & (J_A | J_START)) {
            if (animating) {
                animating = 0;
                level_banner_scx = 0;
                update_cap_sprite_positions(0);
                draw_selected_level();
                if (_cpu == CGB_TYPE) apply_cgb_palettes(bg_color_to, selected);
            }

            disable_interrupts();
            remove_LCD(level_select_stat_isr);
            remove_VBL(level_select_vbl_isr);
            set_interrupts(VBL_IFLAG | TIM_IFLAG);
            enable_interrupts();
            SCX_REG = 0;

            music_ready = 0;
            TAC_REG = 0x00;
            play_sample(BANK_SFX_DATA, play_sound_data, PLAY_SOUND_LEN);
            if (_cpu == CGB_TYPE) fade_set_bkg_palette(0, 8, cgb_menu_pals);
            fade_to_black(2);
            HIDE_SPRITES;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            return STATE_PLAY_LEVEL;
        } else if (pressed & J_B) {
            if (_cpu == CGB_TYPE) fade_set_bkg_palette(0, 8, cgb_menu_pals);
            fade_to_black(2);
            disable_interrupts();
            remove_LCD(level_select_stat_isr);
            remove_VBL(level_select_vbl_isr);
            set_interrupts(VBL_IFLAG | TIM_IFLAG);
            enable_interrupts();
            HIDE_SPRITES;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            SCX_REG = 0;
            return STATE_MENU;
        }

        // Spring animation update
        if (animating) {
            anim_frame++;
            if (anim_frame == SPRING_SWAP_FRAME) {
                draw_selected_level();
                if (_cpu == CGB_TYPE) {
                    apply_cgb_palettes(current_bg_color, selected);
                }
            }

            if (anim_frame >= SPRING_ANIM_FRAMES) {
                animating = 0;
                level_banner_scx = 0;
            } else {
                level_banner_scx = (anim_dir > 0) ? scx_table_right[anim_frame] : scx_table_left[anim_frame];
            }
            update_cap_sprite_positions(level_banner_scx);
        }

        wait_vbl_done();
    }
}
