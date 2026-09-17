#include "states.h"
#include "gameplay.h"
#include "assets.h"
#include "rainbow.h"
#include "logo.h"
#include <gb/gb.h>
#include <gb/cgb.h>

static uint8_t bg_x = 0;
static uint8_t ground_x = 0;

void menu_stat_isr(void) {
    if (LYC_REG == 16) {
        SCX_REG = bg_x;
        LYC_REG = 120;
    } else {
        SCX_REG = ground_x;
        LYC_REG = 255;
    }
}

GameState update_menu_state(void) {
    DISPLAY_OFF;

    // Restore standard palettes
    BGP_REG = 0xE4;
    OBP0_REG = 0xE4;
    OBP1_REG = 0xD2;

    if (_cpu == CGB_TYPE) {
        apply_rainbow_palette(0);
    }

    extern const uint8_t menu_bg_tiles[];
    extern const uint8_t menu_bg_map[];
    extern const uint8_t menu_ground_tiles[];
    extern const uint8_t menu_ground_map[];
    BANKREF_EXTERN(menu_bg)

    uint8_t prev_bank = _current_bank;
    SWITCH_ROM(BANK(chr_gb));
    set_bkg_data(0, 128, chr_gb_tiles);
    SWITCH_ROM(BANK(menu_bg));
    set_bkg_data(28, 87, menu_bg_tiles);    // BG tiles at index 28-114
    set_bkg_data(115, 9, menu_ground_tiles); // Ground tiles at 115-123
    
    // Clear the whole map first (so top 16px is empty sky/color 0)
    fill_bkg_rect(0, 0, 32, 32, 0);

    // Draw background map starting at row 2 (16px down), drawing only 28 rows to not wrap
    set_bkg_tiles(0, 2, 32, 28, menu_bg_map);
    // Draw ground map at row 15 (120px) - 3 rows tall
    set_bkg_tiles(0, 15, 32, 3, menu_ground_map);

    // Load logo tiles from BANK(logo)
    SWITCH_ROM(BANK(logo));
    set_bkg_data(LOGO_TILE_START, LOGO_TILE_COUNT, logo_tiles);
    SWITCH_ROM(prev_bank);

    // Load Pusab font tiles for the version label
    setup_menu_font();

    // Draw version string "Demo v03" on the Window layer (bottom-right, row 0 of Window).
    // The Window is unaffected by SCX_REG changes from the STAT ISR, so it stays
    // anchored regardless of the parallax scroll. WX=103 means the Window starts at
    // screen pixel 96 (= 7 + 12*8), letting the background/ground show through on the
    // left 12 columns; the Window occupies columns 0-7 (Window-relative origin).
    // Note: '.' has no glyph in FontPusab and renders as a blank tile (tile index 0).
    // Tile indices: ' '=0, '0'=3, '3'=6, 'D'=16, 'e'=17, 'm'=25, 'o'=27, 'v'=34
    static const uint8_t ver_tiles[] = { 16, 17, 25, 27, 0, 34, 3, 6 }; // "Demo v03"
    for (uint8_t i = 0; i < 8; i++) {
        set_win_tile_xy(i, 0, (uint8_t)(0xD0u + ver_tiles[i])); // 0xD0 = FONT_PUSAB_START
    }

    // Title Logo ("POCKETDASH") across 20 tiles in rows 0 and 1 (160x16 pixels)
    for (uint8_t x = 0; x < 20; x++) {
        set_bkg_tile_xy(x, 0, (uint8_t)(LOGO_TILE_START + x));
        set_bkg_tile_xy(x, 1, (uint8_t)(LOGO_TILE_START + 20 + x));
    }

    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0); // Reset all attributes to Palette 0
        fill_bkg_rect(0, 0, 20, 2, 1);  // Set logo area (20x2 tiles) to Palette 1
        VBK_REG = 0;
    }

    // Load Play Button (4x4 tiles / 8 8x16 sprites)
    extern const unsigned char playbutton[];
    // Tiles 1-16 (skip tile 0 which is empty)
    set_sprite_data(0, 16, &playbutton[16]);

    if (_cpu == CGB_TYPE) {
        // Dedicated underlay tiles (8x16 each)
        static const uint8_t yellow_fill_tile[32] = {
            0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
            0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
            0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
            0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00
        };
        // Top blue box: rows 1..5 in first 8x8, second 8x8 empty
        static const uint8_t top_blue_tile[32] = {
            0x00, 0x00, 0x7C, 0x00, 0x7C, 0x00, 0x7C, 0x00,
            0x7C, 0x00, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        // Bottom blue box: rows 6..7 in first 8x8, rows 0..2 in second 8x8
        static const uint8_t bot_blue_tile[32] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x7C, 0x00, 0x7C, 0x00,
            0x7C, 0x00, 0x7C, 0x00, 0x7C, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

        set_sprite_data(16, 2, yellow_fill_tile);
        set_sprite_data(18, 2, top_blue_tile);
        set_sprite_data(20, 2, bot_blue_tile);

        // Green palette for the button cross
        static const uint16_t play_button_palette[] = {
            RGB8(255, 255, 255), // Trans
            RGB8(138, 245, 30),  // Light Green
            RGB8(30, 140, 20),   // Dark Green
            RGB8(0, 0, 0)        // Black Outline
        };
        // Yellow palette for the triangle interior fill
        static const uint16_t play_button_yellow_palette[] = {
            RGB8(255, 255, 255), // Trans
            RGB8(255, 240, 0),   // Bright Yellow
            RGB8(255, 210, 0),   // Yellow
            RGB8(220, 160, 0)    // Dark Yellow
        };
        // Blue palette for the 4 corner squares
        static const uint16_t play_button_blue_palette[] = {
            RGB8(255, 255, 255), // Trans
            RGB8(0, 240, 255),   // Cyan / Bright Blue
            RGB8(0, 160, 255),   // Medium Blue
            RGB8(0, 80, 220)     // Dark Blue
        };
        set_sprite_palette(0, 1, play_button_palette);
        set_sprite_palette(1, 1, play_button_yellow_palette);
        set_sprite_palette(2, 1, play_button_blue_palette);
    }

    SPRITES_8x16;
    uint8_t bx = 72; // Centered X (64 + 8)
    uint8_t by = 68; // Centered Y (52 + 16)

    // Foreground Button (OAM 0..7)
    set_sprite_tile(0, 0);  move_sprite(0, bx, by);           set_sprite_prop(0, 0);
    set_sprite_tile(1, 2);  move_sprite(1, bx, by + 16);      set_sprite_prop(1, 0);
    set_sprite_tile(2, 4);  move_sprite(2, bx + 8, by);       set_sprite_prop(2, 0);
    set_sprite_tile(3, 6);  move_sprite(3, bx + 8, by + 16);  set_sprite_prop(3, 0);
    set_sprite_tile(4, 8);  move_sprite(4, bx + 16, by);      set_sprite_prop(4, 0);
    set_sprite_tile(5, 10); move_sprite(5, bx + 16, by + 16); set_sprite_prop(5, 0);
    set_sprite_tile(6, 12); move_sprite(6, bx + 24, by);      set_sprite_prop(6, 0);
    set_sprite_tile(7, 14); move_sprite(7, bx + 24, by + 16); set_sprite_prop(7, 0);

    if (_cpu == CGB_TYPE) {
        // Yellow Triangle Underlay (OAM 8) - CGB only
        set_sprite_tile(8, 16); move_sprite(8, bx + 12, by + 8);  set_sprite_prop(8, 1);

        // Blue Corner Square Underlays (OAM 9..12) - CGB only
        set_sprite_tile(9, 18);  move_sprite(9, bx + 4, by + 4);   set_sprite_prop(9, 2);  // Top-Left
        set_sprite_tile(10, 18); move_sprite(10, bx + 21, by + 4);  set_sprite_prop(10, 2); // Top-Right
        set_sprite_tile(11, 20); move_sprite(11, bx + 4, by + 16);  set_sprite_prop(11, 2); // Bottom-Left
        set_sprite_tile(12, 20); move_sprite(12, bx + 21, by + 16); set_sprite_prop(12, 2); // Bottom-Right
    } else {
        // Hide color underlays on DMG so monochrome priority doesn't obscure the triangle
        for (uint8_t s = 8; s < 13; s++) hide_sprite(s);
    }

    bg_x = 0;
    ground_x = 0;
    SCX_REG = 0;
    SCY_REG = 0;

    disable_interrupts();
    add_LCD(menu_stat_isr);
    STAT_REG |= STATF_LYC;
    LYC_REG = 16;
    set_interrupts(VBL_IFLAG | LCD_IFLAG | TIM_IFLAG);
    enable_interrupts();

    SHOW_BKG;
    SHOW_SPRITES;
    // Show Window layer at the very bottom of the screen (last 8px row, pixel y=136).
    // WX=103 = 7 + 96 → Window starts at screen pixel 96, so the background ground
    // tiles remain visible for the left 12 columns and only the rightmost 8 columns
    // (64px) show the Window with "Demo v03".
    WY_REG = 136;
    WX_REG = 103;
    SHOW_WIN;
    DISPLAY_ON;

    static uint16_t frame_counter = 0;

    while (1) {
        wait_vbl_done();
        SCX_REG = 0;
        SCY_REG = 0;
        LYC_REG = 16;

        uint8_t joy = joypad();
        if (joy & (J_A | J_START)) {
            // Tear down the parallax STAT ISR BEFORE waitpadup() so the ISR can't fire
            // with a stale LYC during the button-release wait (which blocks the main loop
            // and prevents the per-frame LYC_REG=16 / SCX_REG=0 resets from running).
            disable_interrupts();
            remove_LCD(menu_stat_isr);
            STAT_REG &= ~STATF_LYC;
            SCX_REG = 0;
            SCY_REG = 0;
            set_interrupts(VBL_IFLAG | TIM_IFLAG);
            enable_interrupts();
            waitpadup();
            HIDE_SPRITES;
            HIDE_WIN;
            for (uint8_t s = 0; s < 13; s++) hide_sprite(s);
            return STATE_NEW_MENU_SELECT;
        }

        frame_counter++;
        if ((frame_counter & 1) == 0) {
            bg_x += 1;
        }
        ground_x += 3;

        if (_cpu == CGB_TYPE && (frame_counter & 15) == 0) {
            uint8_t color_index = (frame_counter >> 4) & 127;
            apply_rainbow_palette(color_index);
        }
    }
}
