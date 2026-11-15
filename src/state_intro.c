#pragma bank 15

// Boot splash: the logo and the creators' names, still, for 1.5 s (A / START skips), in
// silence. The menu theme starts on the way out, at the title (an intro jingle may go here
// later). The full credits are in state_menu.c (settings, SELECT).

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "states.h"
#include "gameplay.h"
#include "fade.h"
#include "logo.h"
#include "music.h"
#include "settings.h"
#include "furnace_songs.h"

#define STRIP_ROWS  18          // map rows: strip rows 18..35 the page (put_row)
#define PAGE_SCY    144         // the screen shows the page
#define HOLD_FRAMES 90          // 1.5 s

// Rows of the page. '@': the logo (2 rows), '~' mint, '`' grey, '^' orange, else white.
static const char * const intro_rows[STRIP_ROWS] = {
    "", "", "@", "", "",
    "`A GEOMETRY DASH",
    "`DEMAKE FOR THE",
    "`GAME BOY AND",
    "`GAME BOY COLOR",
    "",
    "~CREATED BY",
    "",
    "SOTOS 24",
    "",
    "ELANGEL378",
    "", "", ""
};
#define NAME_ROW_A 12
#define NAME_ROW_B 14

static uint8_t char_tile(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)((c - '0') + 3);
    if (c >= 'A' && c <= 'Z') return (uint8_t)((c - 'A') + 13);
    return 0;   // space
}

// Strip row t into map row t & 31 (the screen's rows are blank)
static void put_row(uint8_t t) {
    uint8_t buf[20], attr[20];
    uint8_t pal = 0, x;
    const char *s = (t >= STRIP_ROWS && t < 2 * STRIP_ROWS) ? intro_rows[t - STRIP_ROWS] : "";
    uint8_t logo_row = 0xFF;
    if (*s == '@') logo_row = 0;
    else if (t > STRIP_ROWS && *intro_rows[t - STRIP_ROWS - 1] == '@') logo_row = 20;
    if (logo_row != 0xFF) {
        for (x = 0; x < 20; x++) { buf[x] = (uint8_t)(LOGO_TILE_START + logo_row + x); attr[x] = 4; }
    } else {
        if (*s == '~') { pal = 3; s++; }
        else if (*s == '`') { pal = 2; s++; }
        else if (*s == '^') { pal = 1; s++; }
        uint8_t len = 0;
        while (s[len]) len++;
        uint8_t x0 = (uint8_t)((20u - len) >> 1);
        for (x = 0; x < 20; x++) {
            buf[x] = (uint8_t)(FONT_PUSAB_START + ((x >= x0 && x < x0 + len) ? char_tile(s[x - x0]) : 0));
            attr[x] = pal;
        }
    }
    uint8_t mapy = t & 31u;
    set_bkg_tiles(0, mapy, 20, 1, buf);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_bkg_tiles(0, mapy, 20, 1, attr);
        VBK_REG = 0;
    }
}

GameState update_intro_state(void) BANKED {
    // load with the display on (off shows white); fade_from_black shows the palettes
    fade_set_black();
    fade_hold = 1;
    HIDE_SPRITES;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
    HIDE_WIN;
    SCX_REG = 0;
    SCY_REG = 0;
    SPRITES_8x16;

    fill_bkg_rect(0, 0, 32, 32, 0);
    setup_menu_font();
    load_logo_tiles();
    if (_cpu == CGB_TYPE) {
        // 0 text (cyan / white), 1 orange, 2 grey-blue, 3 mint headings, 4 the logo
        static const palette_color_t intro_pals[20] = {
            RGB8(15, 20, 35), RGB8(0, 130, 180), RGB8(60, 210, 255), RGB8(255, 255, 255),
            RGB8(15, 20, 35), RGB8(210, 140, 20), RGB8(255, 225, 40), RGB8(255, 255, 255),
            RGB8(15, 20, 35), RGB8(60, 75, 105), RGB8(110, 135, 175), RGB8(200, 215, 235),
            RGB8(15, 20, 35), RGB8(20, 150, 70), RGB8(60, 245, 120), RGB8(255, 255, 255),
            RGB8(15, 20, 35), RGB8(185, 250, 70), RGB8(60, 175, 40), RGB8(0, 0, 0)
        };
        palette_color_t pals[20];   // fade_set_* are banked: a RAM copy
        for (uint8_t i = 0; i < 20; i++) pals[i] = intro_pals[i];
        fade_set_bkg_palette(0, 5, pals);
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0);
        VBK_REG = 0;
    }
    fade_set_dmg_palettes(0xE4, 0xE4, 0xE4);

    for (uint8_t t = STRIP_ROWS; t < 2u * STRIP_ROWS; t++) put_row(t);
    SCY_REG = PAGE_SCY;

    SHOW_BKG;
    DISPLAY_ON;
#ifdef FUR_SPLASH
    if (setting_music_enabled) {   // the jingle (music/Furnace/Splash.fur): one pass, ~1.1 s
        music_play_fur(FUR_SPLASH);
        music_ready = 1;
    }
#endif
    fade_from_black(2);

    uint8_t prev_joy = joypad();
    for (uint8_t hold = 0; hold < HOLD_FRAMES; hold++) {
        wait_vbl_done();
        uint8_t joy = joypad();
        if ((joy & ~prev_joy) & (J_START | J_A)) break;
        prev_joy = joy;
    }

    fade_capture_current();
    fade_to_black(2);
    HIDE_SPRITES;
    SCX_REG = 0;
    SCY_REG = 0;
    if (setting_music_enabled) {   // (main.c leaves the boot to the jingle)
        music_play_menu();
        music_ready = 1;
    }
    return STATE_MENU;
}
