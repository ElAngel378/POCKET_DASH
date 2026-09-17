#pragma bank 22

#include "states.h"
#include "gameplay.h"
#include "assets.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "fade.h"
#include "hUGEDriver.h"
#include "menu_select_bg.h"
#include <gb/gb.h>
#include <gb/cgb.h>

extern uint8_t selected;
extern uint8_t redraw;
extern uint8_t music_ready;
extern volatile uint8_t current_song_bank;
extern const hUGESong_t menuloop;

// Authentic Geometry Dash / Famidash level background colors
static const palette_color_t cgb_level_bg_colors[11] = {
    RGB8(40, 84, 252),   // 0: Stereo Madness (Famidash Cobalt Blue)
    RGB8(181, 62, 180),  // 1: Back On Track (Famidash Magenta / Pink)
    RGB8(0, 180, 40),    // 2: Polargeist (Famidash Lime Green)
    RGB8(210, 50, 10),   // 3: Dry Out (Famidash Red-Orange)
    RGB8(80, 30, 160),   // 4: Base After Base (Famidash Indigo / Purple)
    RGB8(160, 20, 90),   // 5: Cant Let Go (Famidash Dark Magenta)
    RGB8(0, 140, 230),   // 6: Jumper (Famidash Sky Blue / Cyan)
    RGB8(200, 30, 20),   // 7: Time Machine (Famidash Crimson / Red)
    RGB8(145, 75, 10),   // 8: Cycles (Famidash Amber / Brown)
    RGB8(0, 70, 140),    // 9: xStep (Famidash Navy Blue)
    RGB8(110, 20, 130)   // 10: Ultimate Destruction (Famidash Deep Violet)
};

static const palette_color_t diff_skin_colors[6] = {
    RGB8(0, 190, 255),  // 0: Easy (Sky Blue)
    RGB8(0, 245, 0),    // 1: Normal (Vibrant Electric Lime Green)
    RGB8(255, 215, 0),  // 2: Hard (Vibrant Golden Yellow)
    RGB8(255, 50, 0),   // 3: Harder (Vibrant Red-Orange)
    RGB8(255, 75, 215), // 4: Insane (Vibrant Hot Pink)
    RGB8(225, 30, 40)   // 5: Demon (Vibrant Crimson Red)
};

static const uint8_t level_difficulties[11] = {
    0, // 0: Stereo Madness (Easy)
    0, // 1: Back On Track (Easy)
    1, // 2: Polargeist (Normal)
    1, // 3: Dry Out (Normal)
    2, // 4: Base After Base (Hard)
    2, // 5: Cant Let Go (Hard)
    3, // 6: Jumper (Harder)
    3, // 7: Time Machine (Harder)
    3, // 8: Cycles (Harder)
    4, // 9: xStep (Insane)
    5  // 10: Ultimate Destruction (Demon)
};

// 6 Difficulty Faces x 4 tiles (16x16 pixels)
static const uint8_t difficulty_face_tiles[6][64] = {
    // 0: Easy
    {
        0x00, 0x00, 0x0f, 0x0c, 0x1f, 0x10, 0x3f, 0x20, 0x7f, 0x40, 0x7b, 0x4c, 0x7f, 0x0c, 0x7f, 0x00,
        0x00, 0x00, 0xf0, 0x30, 0xf8, 0x08, 0xfc, 0x04, 0xfe, 0x02, 0xde, 0x32, 0xfe, 0x30, 0xfe, 0x00,
        0x7f, 0x00, 0x7f, 0x1f, 0x70, 0x5f, 0x7f, 0x4f, 0x3f, 0x27, 0x1f, 0x10, 0x0f, 0x0c, 0x00, 0x00,
        0xfe, 0x00, 0xfe, 0xf8, 0x0e, 0xfa, 0xfe, 0xf2, 0xfc, 0xe4, 0xf8, 0x08, 0xf0, 0x30, 0x00, 0x00,
    },
    // 1: Normal
    {
        0x00, 0x00, 0x0f, 0x0c, 0x1f, 0x10, 0x3f, 0x20, 0x7f, 0x40, 0x7b, 0x4c, 0x7f, 0x0c, 0x7f, 0x00,
        0x00, 0x00, 0xf0, 0x30, 0xf8, 0x08, 0xfc, 0x04, 0xfe, 0x02, 0xde, 0x32, 0xfe, 0x30, 0xfe, 0x00,
        0x7f, 0x00, 0x7f, 0x10, 0x7f, 0x48, 0x7f, 0x47, 0x3f, 0x20, 0x1f, 0x10, 0x0f, 0x0c, 0x00, 0x00,
        0xfe, 0x00, 0xfe, 0x08, 0xfe, 0x12, 0xfe, 0xe2, 0xfc, 0x04, 0xf8, 0x08, 0xf0, 0x30, 0x00, 0x00,
    },
    // 2: Hard
    {
        0x00, 0x00, 0x0f, 0x0c, 0x1f, 0x10, 0x3f, 0x2c, 0x7f, 0x42, 0x7b, 0x4c, 0x7f, 0x0c, 0x7f, 0x00,
        0x00, 0x00, 0xf0, 0x30, 0xf8, 0x08, 0xfc, 0x34, 0xfe, 0x42, 0xde, 0x32, 0xfe, 0x30, 0xfe, 0x00,
        0x7f, 0x00, 0x7f, 0x00, 0x7f, 0x43, 0x7f, 0x40, 0x3f, 0x20, 0x1f, 0x10, 0x0f, 0x0c, 0x00, 0x00,
        0xfe, 0x00, 0xfe, 0x00, 0xfe, 0xc2, 0xfe, 0x02, 0xfc, 0x04, 0xf8, 0x08, 0xf0, 0x30, 0x00, 0x00,
    },
    // 3: Harder
    {
        0x00, 0x00, 0x0f, 0x0c, 0x1f, 0x10, 0x3f, 0x30, 0x7f, 0x4c, 0x7f, 0x42, 0x77, 0x0c, 0x7f, 0x0c,
        0x00, 0x00, 0xf0, 0x30, 0xf8, 0x08, 0xfc, 0x0c, 0xfe, 0x32, 0xfe, 0x42, 0xee, 0x30, 0xfe, 0x30,
        0x7f, 0x00, 0x7f, 0x00, 0x7f, 0x43, 0x7f, 0x44, 0x3f, 0x28, 0x1f, 0x10, 0x0f, 0x0c, 0x00, 0x00,
        0xfe, 0x00, 0xfe, 0x00, 0xfe, 0xc2, 0xfe, 0x22, 0xfc, 0x14, 0xf8, 0x08, 0xf0, 0x30, 0x00, 0x00,
    },
    // 4: Insane
    {
        0x00, 0x00, 0x0f, 0x0c, 0x1f, 0x10, 0x3f, 0x28, 0x7f, 0x44, 0x77, 0x4e, 0x7f, 0x0c, 0x7f, 0x00,
        0x00, 0x00, 0xf0, 0x30, 0xf8, 0x08, 0xfc, 0x14, 0xfe, 0x22, 0xee, 0x72, 0xfe, 0x30, 0xfe, 0x00,
        0x7f, 0x03, 0x7f, 0x07, 0x7f, 0x4f, 0x7f, 0x4f, 0x3c, 0x27, 0x1f, 0x10, 0x0f, 0x0c, 0x00, 0x00,
        0xfe, 0xc0, 0xfe, 0xe0, 0xfe, 0xf2, 0xfe, 0xf2, 0x3c, 0xe4, 0xf8, 0x08, 0xf0, 0x30, 0x00, 0x00,
    },
    // 5: Demon
    {
        0x83, 0x83, 0xcf, 0xcc, 0xbf, 0xb0, 0xbf, 0xe0, 0x5f, 0x70, 0x4f, 0x6c, 0xc3, 0xf6, 0xff, 0xbc,
        0xc1, 0xc1, 0xf3, 0x33, 0xfd, 0x0d, 0xfd, 0x07, 0xfa, 0x0e, 0xf2, 0x36, 0xc3, 0x6f, 0xff, 0x3d,
        0xff, 0x98, 0xe7, 0xef, 0x60, 0x7f, 0x60, 0x6a, 0x3f, 0x3f, 0x3f, 0x30, 0x0f, 0x0c, 0x03, 0x03,
        0xff, 0x79, 0x87, 0xaf, 0x06, 0xfe, 0x06, 0x56, 0xfc, 0xfc, 0xfc, 0x0c, 0xf0, 0x30, 0xc0, 0xc0,
    },
};

static void apply_cgb_palettes(uint8_t level_idx) {
    if (_cpu == CGB_TYPE) {
        palette_color_t bg_col = cgb_level_bg_colors[level_idx % 11];
        palette_color_t pals[32];

        // Palette 0: Screen Background & Default
        pals[0] = bg_col;
        pals[1] = RGB8(84, 216, 0);    // Back button / Stage green accent
        pals[2] = RGB8(255, 255, 255);  // Pure White
        pals[3] = RGB8(0, 0, 0);        // Pitch Black

        // Palette 1: Center Box & Level Title Text (Solid pitch black box, pure white text)
        pals[4] = RGB8(0, 0, 0);        // Box / font background: PITCH BLACK
        pals[5] = RGB8(0, 0, 0);        // Box interior: PITCH BLACK
        pals[6] = RGB8(180, 215, 255);  // Font inner highlight: Light Sky Blue
        pals[7] = RGB8(255, 255, 255);  // Font letter face: PURE WHITE

        // Palette 2: Normal Mode Section
        pals[8] = bg_col;
        pals[9] = RGB8(84, 216, 0);     // Progress bar fill: Green
        pals[10] = RGB8(255, 255, 255); // White
        pals[11] = RGB8(0, 0, 0);       // Black outline

        // Palette 3: Practice Mode Section
        pals[12] = bg_col;
        pals[13] = RGB8(0, 168, 252);   // Progress bar fill: Cyan/Blue
        pals[14] = RGB8(255, 255, 255); // White
        pals[15] = RGB8(0, 0, 0);       // Black outline

        // Palette 4: Difficulty Face (Dynamic per level difficulty!)
        uint8_t diff = level_difficulties[level_idx % 11];
        pals[16] = RGB8(0, 0, 0);                 // Color 0: Pitch Black (seamless with box)
        pals[17] = diff_skin_colors[diff];        // Color 1: Skin (Cyan, Green, Orange, Red, Pink, Purple)
        pals[18] = RGB8(255, 255, 255);           // Color 2: White eyes / teeth / horns
        pals[19] = RGB8(0, 0, 0);                 // Color 3: Black outline / pupils / mouth

        // Palette 5: Top & Bottom Stage Blocks
        pals[20] = bg_col;
        pals[21] = RGB8(0, 88, 248);    // Stage Blue
        pals[22] = RGB8(84, 216, 0);    // Stage Green
        pals[23] = RGB8(0, 0, 0);       // Black outline

        // Palette 6: Info (i) Button (Top Right)
        pals[24] = bg_col;                        // Color 0: Background behind button
        pals[25] = RGB8(0, 180, 255);             // Color 1: GD Info Sky Blue
        pals[26] = RGB8(255, 255, 255);           // Color 2: Pure White 'i' symbol
        pals[27] = RGB8(0, 0, 0);                 // Color 3: Black outline

        // Palette 7: Navigation Arrows (Left & Right)
        pals[28] = bg_col;                        // Color 0: Background behind arrow
        pals[29] = RGB8(255, 255, 255);           // Color 1: Pure White arrow body
        pals[30] = RGB8(255, 255, 255);           // Color 2: Pure White arrow highlight
        pals[31] = RGB8(0, 0, 0);                 // Color 3: Black outline

        fade_set_bkg_palette(0, 8, pals);
    }
}

static void setup_cgb_attributes(void) {
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        // Default: Palette 0 for entire screen
        fill_bkg_rect(0, 0, 20, 18, 0);

        // Top banner: Palette 5 (Stage blocks)
        fill_bkg_rect(0, 0, 20, 1, 5);

        // Info (i) button: Columns 17..18, Rows 1..2 use Palette 6 (Vibrant Blue button)
        fill_bkg_rect(17, 1, 2, 2, 6);

        // Center Box: Columns 3..16, Rows 4..9 use Palette 1 (Pitch black box, pure white text)
        fill_bkg_rect(3, 4, 14, 6, 1);

        // Difficulty Face: Columns 4..5, Rows 6..7 use Palette 4
        fill_bkg_rect(4, 6, 2, 2, 4);

        // Normal Mode bar: Columns 3..16, Rows 11..12 use Palette 2 (Green bar, white text)
        fill_bkg_rect(3, 11, 14, 2, 2);

        // Practice Mode bar: Columns 3..16, Rows 13..14 use Palette 3 (Blue bar, white text)
        fill_bkg_rect(3, 13, 14, 2, 3);

        // Bottom stage blocks: Rows 16..17 use Palette 5
        fill_bkg_rect(0, 16, 20, 2, 5);

        VBK_REG = 0;
    }
}

extern const unsigned char FontPusab[];

static void setup_menu_select_font(void) {
    if (_cpu == CGB_TYPE) {
        set_bkg_data(FONT_PUSAB_START, 39, FontPusab);
    } else {
        // On DMG, replace background color 0 with color 1 (Light Gray)
        // so font background seamlessly blends into box interior tile 0x16.
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
}

static void setup_arrow_sprites(void) {
    SPRITES_8x8;
    // Left arrow (4 tiles stacked vertically at screen x=8, y=56 -> OAM x=16, y=72)
    move_sprite(0, 16, 72); set_sprite_tile(0, 26);
    move_sprite(1, 16, 80); set_sprite_tile(1, 30);
    move_sprite(2, 16, 88); set_sprite_tile(2, 32);
    move_sprite(3, 16, 96); set_sprite_tile(3, 37);

    // Right arrow (4 tiles stacked vertically at screen x=144, y=56 -> OAM x=152, y=72)
    move_sprite(4, 152, 72); set_sprite_tile(4, 29);
    move_sprite(5, 152, 80); set_sprite_tile(5, 31);
    move_sprite(6, 152, 88); set_sprite_tile(6, 36);
    move_sprite(7, 152, 96); set_sprite_tile(7, 38);

    if (_cpu == CGB_TYPE) {
        palette_color_t obj_pals[4];
        obj_pals[0] = RGB8(0, 0, 0);       // Transparent
        obj_pals[1] = RGB8(255, 255, 255); // Pure White arrow body
        obj_pals[2] = RGB8(255, 255, 255); // Pure White highlight
        obj_pals[3] = RGB8(0, 0, 0);       // Black outline
        set_sprite_palette(0, 1, obj_pals);
        for (uint8_t s = 0; s < 8; s++) set_sprite_prop(s, 0);
    } else {
        OBP0_REG = 0xE0; // Color 1 = White, Color 3 = Black
    }
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

static void draw_selected_level(void) {
    // Restore rows 6 and 7 from background map before drawing new text
    set_bkg_tiles(0, 6, 20, 2, &menu_select_bg_map[6 * 20]);
    // Clear arrow background positions
    set_bkg_tile_xy(1, 7, 0);
    set_bkg_tile_xy(18, 7, 0);

    // Update difficulty face tiles in VRAM to match the level difficulty
    uint8_t diff = level_difficulties[selected % 11];
    if (_cpu == CGB_TYPE) {
        set_bkg_data(24, 2, &difficulty_face_tiles[diff][0]);
        set_bkg_data(27, 2, &difficulty_face_tiles[diff][32]);
    } else {
        // On DMG, transform 2BPP bitplanes for solid, high-contrast face graphics:
        //   Pixel 0 (exterior) -> Color 1 (Light Gray, blends into box background)
        //   Pixel 1 (face skin) -> Color 2 (Dark Gray, solid head standing out against box)
        //   Pixel 2 (teeth/eyes/horns) -> Color 0 (Pure White, pops brightly on dark face)
        //   Pixel 3 (outline/pupils) -> Color 3 (Pitch Black, sharp definition)
        // Formula: new_b0 = ~(b0 ^ b1), new_b1 = b0
        uint8_t buf[32];
        for (uint8_t i = 0; i < 16; i++) {
            uint8_t b0 = difficulty_face_tiles[diff][2 * i];
            uint8_t b1 = difficulty_face_tiles[diff][2 * i + 1];
            buf[2 * i] = (uint8_t)(~(b0 ^ b1));
            buf[2 * i + 1] = b0;
        }
        set_bkg_data(24, 2, buf);
        for (uint8_t i = 0; i < 16; i++) {
            uint8_t b0 = difficulty_face_tiles[diff][32 + 2 * i];
            uint8_t b1 = difficulty_face_tiles[diff][32 + 2 * i + 1];
            buf[2 * i] = (uint8_t)(~(b0 ^ b1));
            buf[2 * i + 1] = b0;
        }
        set_bkg_data(27, 2, buf);
    }

    const char *name = game_levels[selected]->name;
    uint8_t len = get_name_length(name);

    if (len <= 10) {
        // Single line left-aligned at x=6, y=6
        draw_menu_text(6, 6, name);
    } else {
        // Find best word split point <= 10
        int8_t split_idx = -1;
        for (int8_t i = 10; i >= 0; i--) {
            if (name[i] == ' ') {
                split_idx = i;
                break;
            }
        }
        if (split_idx == -1) {
            split_idx = 10;
        }

        char line1[12];
        uint8_t i;
        for (i = 0; i < (uint8_t)split_idx && i < 10; i++) {
            line1[i] = name[i];
        }
        line1[i] = '\0';

        const char *line2 = &name[split_idx];
        while (*line2 == ' ') line2++; // skip spaces

        draw_menu_text(6, 6, line1);
        if (*line2 != '\0') {
            draw_menu_text(6, 7, line2);
        }
    }
}

// =============================================================================
// Level Select Banner & Progress Bar Scroll Animation Configuration
// =============================================================================
// The scroll effect sweeps the level box and progress bars across the screen,
// wrapping around with a soft spring overshoot and settle.
// You can adjust the spring force, duration, and swap timing below:
// =============================================================================
#define SPRING_ANIM_FRAMES  16  // Total animation length in frames (16 frames ≈ 0.26s at 60fps)
#define SPRING_SWAP_FRAME   5   // Frame index where banner is completely offscreen

// Soft spring curve for scrolling RIGHT (pressing Right / Down)
// Values represent horizontal pixel offset (SCX) wrapping around 256px.
// Peak overshoot is soft (+4px past 0), settling gently.
static const uint8_t scx_table_right[SPRING_ANIM_FRAMES] = {
    0, 12, 32, 68, 112, 150, 186, 218, 242, 253, 4, 3, 1, 255, 0, 0
};

// Soft spring curve for scrolling LEFT (pressing Left / Up)
// Peak overshoot is soft (-4px / 252 past 0), settling gently.
static const uint8_t scx_table_left[SPRING_ANIM_FRAMES] = {
    0, 244, 224, 188, 144, 106, 70, 38, 14, 3, 252, 253, 255, 1, 0, 0
};

GameState update_new_menu_select_state(void) BANKED {
    fade_set_black();
    DISPLAY_OFF;

    // Reset scroll registers
    SCX_REG = 0;
    SCY_REG = 0;

    // Hide any sprites or window leftover from other states
    HIDE_SPRITES;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
    HIDE_WIN;

    // Clear entire 32x32 background map so offscreen tiles (cols 20..31) are clean
    fill_bkg_rect(0, 0, 32, 32, 0);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0);
        VBK_REG = 0;
    }

    // Load background artwork (both this code and menu_select_bg reside in Bank 22)
    set_bkg_data(0, menu_select_bg_TILE_COUNT, menu_select_bg_tiles);
    set_bkg_tiles(0, 0, 20, 18, menu_select_bg_map);

    // Clear arrow background tiles (they are now rendered as stationary hardware sprites)
    for (uint8_t r = 7; r <= 10; r++) {
        set_bkg_tile_xy(1, r, 0);
        set_bkg_tile_xy(18, r, 0);
    }

    // Setup Pusab font tiles (with DMG background fix)
    setup_menu_select_font();

    if (_cpu == CGB_TYPE) {
        setup_cgb_attributes();
        apply_cgb_palettes(selected);
    }
    fade_set_dmg_palettes(0xE4, 0xE4, 0xE4);
    BGP_REG = 0xE4;

    draw_selected_level();
    setup_arrow_sprites();

    SHOW_BKG;
    fade_set_black();
    DISPLAY_ON;
    fade_from_black(2);

    // Setup HBlank / STAT scanline split-screen:
    // Scanlines 0..31: SCX = 0 (top header locked)
    // Scanlines 32..119: SCX = level_banner_scx (level box + progress bars scrolling)
    // Scanlines 120..143: SCX = 0 (bottom floor locked)
    level_banner_scx = 0;
    disable_interrupts();
    add_LCD(level_select_stat_isr);
    STAT_REG |= STATF_LYC;
    LYC_REG = 31;
    set_interrupts(VBL_IFLAG | LCD_IFLAG | TIM_IFLAG);
    enable_interrupts();

    uint8_t animating = 0;
    int8_t anim_dir = 0;
    uint8_t anim_frame = 0;
    uint8_t prev_joy = joypad();

    while (1) {
        wait_vbl_done();
        SCX_REG = 0;
        LYC_REG = 31;

        if (animating) {
            anim_frame++;
            if (anim_frame == SPRING_SWAP_FRAME) {
                // Banner is completely off-screen: update level content seamlessly
                if (anim_dir > 0) {
                    if (selected < MAX_LEVELS - 1) selected++;
                    else selected = 0;
                } else {
                    if (selected > 0) selected--;
                    else selected = MAX_LEVELS - 1;
                }
                draw_selected_level();
                if (_cpu == CGB_TYPE) {
                    apply_cgb_palettes(selected);
                }
            }

            if (anim_frame >= SPRING_ANIM_FRAMES) {
                animating = 0;
                level_banner_scx = 0;
            } else {
                level_banner_scx = (anim_dir > 0) ? scx_table_right[anim_frame] : scx_table_left[anim_frame];
            }
        } else {
            uint8_t joy = joypad();
            uint8_t pressed = joy & ~prev_joy;
            prev_joy = joy;

            if (pressed & (J_RIGHT | J_DOWN)) {
                animating = 1;
                anim_dir = 1;
                anim_frame = 0;
                level_banner_scx = scx_table_right[0];
            } else if (pressed & (J_LEFT | J_UP)) {
                animating = 1;
                anim_dir = -1;
                anim_frame = 0;
                level_banner_scx = scx_table_left[0];
            } else if (pressed & (J_A | J_START)) {
                // Teardown STAT ISR before fade
                disable_interrupts();
                remove_LCD(level_select_stat_isr);
                set_interrupts(VBL_IFLAG | TIM_IFLAG);
                enable_interrupts();
                HIDE_SPRITES;
                for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
                SCX_REG = 0;

                waitpadup();
                music_ready = 0;
                TAC_REG = 0x00;
                play_sample(BANK_SFX_DATA, play_sound_data, PLAY_SOUND_LEN);
                fade_to_black(2);
                return STATE_PLAY_LEVEL;
            } else if (pressed & J_B) {
                disable_interrupts();
                remove_LCD(level_select_stat_isr);
                set_interrupts(VBL_IFLAG | TIM_IFLAG);
                enable_interrupts();
                HIDE_SPRITES;
                for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
                SCX_REG = 0;

                waitpadup();
                return STATE_MENU;
            }
        }
    }
}
