#pragma bank 15

// Credits at boot, before the menu: 3 pages, 5 s each. The next page scrolls up from below.
// START skips. Scheming Weasel plays, then the menu theme comes back.

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "states.h"
#include "gameplay.h"
#include "fade.h"
#include "music.h"
#include "settings.h"
#include "furnace_songs.h"

#define NUM_PAGES    3
#define PAGE_ROWS    18          // tile rows per page = the screen
#define PAGE_FRAMES  300         // 5 s per page
#define SCROLL_SPEED 2           // pixels per frame while a page scrolls in

// '~' at the start of a row: heading colour. 20 columns at most.
static const char * const pages[NUM_PAGES][PAGE_ROWS] = {
    {
        "",
        "",
        "",
        "~    POCKET DASH",
        "",
        "  A GEOMETRY DASH",
        "   DEMAKE FOR THE",
        "    GAME BOY AND",
        "   GAME BOY COLOR",
        "",
        "     CREATED BY",
        "      SOTOS 24",
        "     ELANGEL378",
        "", "", "", "", ""
    }, {
        "",
        "~      CREDITS",
        "--------------------",
        " SOTOS 24",
        " ELANGEL378",
        " ZIQIANGAO",
        " CRAFTY JUMPER",
        " NERDBOY628",
        " USERSNIPER",
        " RANEDOM",
        " PBAXX",
        " FINNTENDO",
        " SWITCHROOT",
        " BOBAJOEJOE",
        "", "", "", ""
    }, {
        "",
        "~     THANK YOU",
        "--------------------",
        "",
        " TFDSOFT AND THE",
        " FAMIDASH TEAM",
        " FOR THE ASSETS",
        "",
        " GEOMETRY DASH",
        " BY ROBTOP GAMES",
        "",
        " PRESS START",
        "", "", "", "", "", ""
    }
};

// Dash glyph (the music test's tile 41)
static const uint8_t dash_tile[16] = {
    0x00,0x00, 0x00,0x00, 0x00,0x7C, 0x38,0x7C, 0x00,0x7C, 0x00,0x00, 0x00,0x00, 0x00,0x00
};

extern uint8_t music_ready;

static uint8_t char_tile(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)((c - '0') + 3);
    if (c >= 'A' && c <= 'Z') return (uint8_t)((c - 'A') + 13);
    if (c == '-') return 41;
    return 0;   // space
}

// Strip tile row t (page 0 = rows 0..17, page 1 below it = 18..35, ...) into its map row
static void put_row(uint8_t t) {
    uint8_t buf[20], attr[20];
    const char *s = "";
    uint8_t pal = 0, x = 0, mapy = t & 31u;
    uint8_t p = t / PAGE_ROWS;
    if (p < NUM_PAGES) s = pages[p][t - p * PAGE_ROWS];
    if (*s == '~') { pal = 3; s++; }
    for (; x < 20; x++) {
        buf[x] = (uint8_t)(FONT_PUSAB_START + (*s ? char_tile(*s++) : 0));
        attr[x] = pal;
    }
    set_bkg_tiles(0, mapy, 20, 1, buf);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_bkg_tiles(0, mapy, 20, 1, attr);
        VBK_REG = 0;
    }
}

GameState update_credits_state(void) BANKED {
    // load with the display on (off shows white); fade_from_black shows the palettes
    fade_set_black();
    fade_hold = 1;
    HIDE_SPRITES;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
    HIDE_WIN;
    SCX_REG = 0;
    SCY_REG = 0;

    fill_bkg_rect(0, 0, 32, 32, 0);
    setup_menu_font();
    set_bkg_data((uint8_t)(FONT_PUSAB_START + 41), 1, dash_tile);

    if (_cpu == CGB_TYPE) {
        // same colours as the music test: palette 0 text (cyan / white), 3 headings (mint green)
        static const palette_color_t credits_pals[16] = {
            RGB8(15, 20, 35), RGB8(0, 130, 180), RGB8(60, 210, 255), RGB8(255, 255, 255),
            RGB8(15, 20, 35), RGB8(210, 140, 20), RGB8(255, 225, 40), RGB8(255, 255, 255),
            RGB8(15, 20, 35), RGB8(60, 75, 105), RGB8(110, 135, 175), RGB8(200, 215, 235),
            RGB8(15, 20, 35), RGB8(20, 150, 70), RGB8(60, 245, 120), RGB8(255, 255, 255)
        };
        palette_color_t pals[16];   // fade_set_* are banked: RAM copies
        for (uint8_t i = 0; i < 16; i++) pals[i] = credits_pals[i];
        fade_set_bkg_palette(0, 4, pals);
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0);
        VBK_REG = 0;
    }
    fade_set_dmg_palettes(0xE4, 0xD2, 0xE4);

    for (uint8_t t = 0; t < PAGE_ROWS; t++) put_row(t);

    SHOW_BKG;
    DISPLAY_ON;
    fade_from_black(2);

#ifdef FUR_SCHEMINGWEASEL
    if (setting_music_enabled) {
        music_play_fur(FUR_SCHEMINGWEASEL);
        music_ready = 1;
    }
#endif

    uint16_t cam = 0;                // top of the screen in the strip, in pixels (counts up)
    uint8_t filled = PAGE_ROWS - 1;  // bottom tile row of the strip drawn so far
    uint8_t prev_joy = joypad();
    uint8_t skip = 0;

    for (uint8_t page = 0; page < NUM_PAGES && !skip; page++) {
        for (uint16_t f = 0; f < PAGE_FRAMES && !skip; f++) {
            wait_vbl_done();
            uint8_t joy = joypad();
            if ((joy & ~prev_joy) & J_START) skip = 1;
            prev_joy = joy;
        }
        if (page == NUM_PAGES - 1 || skip) break;
        // the next page scrolls in from the bottom: the picture moves up
        uint16_t target = cam + (uint16_t)PAGE_ROWS * 8;
        while (cam < target && !skip) {
            wait_vbl_done();
            cam += SCROLL_SPEED;
            uint8_t t = (uint8_t)((cam + 143u) >> 3);   // the bottom tile row on screen
            if (t > filled) {
                put_row(t);
                filled = t;
            }
            SCY_REG = (uint8_t)cam;
            uint8_t joy = joypad();
            if ((joy & ~prev_joy) & J_START) skip = 1;
            prev_joy = joy;
        }
    }

    fade_capture_current();
    fade_to_black(2);
    if (setting_music_enabled) {   // back to the menu theme
        music_play_menu();
        music_ready = 1;
    }
    SCX_REG = 0;
    SCY_REG = 0;
    return STATE_MENU;
}
