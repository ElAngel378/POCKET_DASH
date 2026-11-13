#pragma bank 15

// Boot intro: the logo and a few lines roll up from below and stop, the creators' icons spin
// next to their names and squares drift up behind. ~5 s, A / START skips. The menu theme
// plays from the start (main.c). The full credits are in state_menu.c (settings, SELECT).

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "states.h"
#include "gameplay.h"
#include "fade.h"
#include "logo.h"
#include "bg_parallax.h"        // cube_tiles_load_now
#include "cube_icon_frames.h"

#define STRIP_ROWS  18          // the screen: strip rows 0..17 blank, 18..35 the page
#define ROLL_PX     144         // the page scrolls up until it fills the screen
#define HOLD_FRAMES 200

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

// sprites (8x16): icons OAM 0..1 and 2..3 (tiles 0..3, 4..7), particles OAM 4.. (tile 8 + blank 9)
#define ICON_GAP   6            // pixels between an icon and its name
#define PART_TILE  8
#define NUM_PARTS  6
static const uint8_t part_tile[32] = {
    0x7E,0x00, 0x42,0x00, 0x42,0x00, 0x42,0x00, 0x42,0x00, 0x42,0x00, 0x7E,0x00, 0x00,0x00,
    0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0
};
static const uint8_t part_x[NUM_PARTS] = { 14, 136, 52, 100, 30, 150 };
static const uint8_t part_y0[NUM_PARTS] = { 20, 60, 110, 150, 190, 90 };

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

static const uint8_t *icon_src(uint8_t k, uint8_t *bank) {
    if (k == 0) { *bank = BANK(cube_icon_frames); return cube_icon_frames; }
    *bank = BANK(cube_fd_frames_0);   // icons 1..10
    return cube_fd_frames_0 + (uint16_t)(k - 1u) * CUBE_FD_FRAMES * 64u;
}

// Icon b (0, 1) at strip row row, turned to step (0..47), the screen showing strip pixels from cam
static void place_icon(uint8_t b, uint8_t row, uint16_t cam, uint8_t step) {
    uint8_t s = (uint8_t)(b << 1);
    int16_t y = (int16_t)((uint16_t)(STRIP_ROWS + row) * 8u) - (int16_t)cam - 4;
    if (y <= -16 || y >= 144) { hide_sprite(s); hide_sprite((uint8_t)(s + 1)); return; }
    uint8_t flip = (step >= CUBE_ICON_FRAMES) ? (S_FLIPX | S_FLIPY) : 0;
    uint8_t t = (uint8_t)(b << 2);
    uint8_t tl = t, tr = (uint8_t)(t + 2u);
    if (flip) { tl = tr; tr = t; }
    uint8_t prop = (_cpu == CGB_TYPE) ? b : 0;
    set_sprite_tile(s, tl); set_sprite_prop(s, (uint8_t)(prop | flip));
    set_sprite_tile((uint8_t)(s + 1), tr); set_sprite_prop((uint8_t)(s + 1), (uint8_t)(prop | flip));
    // left of the name (centred like put_row does)
    const char *name = intro_rows[row];
    uint8_t len = 0;
    while (name[len]) len++;
    uint8_t x = (uint8_t)((((20u - len) >> 1) << 3) - 16u - ICON_GAP);
    move_sprite(s, (uint8_t)(x + 8u), (uint8_t)(y + 16));
    move_sprite((uint8_t)(s + 1), (uint8_t)(x + 16u), (uint8_t)(y + 16));
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
    set_sprite_data(PART_TILE, 2, part_tile);

    if (_cpu == CGB_TYPE) {
        // 0 text (cyan / white), 1 orange, 2 grey-blue, 3 mint headings, 4 the logo
        static const palette_color_t intro_pals[20] = {
            RGB8(15, 20, 35), RGB8(0, 130, 180), RGB8(60, 210, 255), RGB8(255, 255, 255),
            RGB8(15, 20, 35), RGB8(210, 140, 20), RGB8(255, 225, 40), RGB8(255, 255, 255),
            RGB8(15, 20, 35), RGB8(60, 75, 105), RGB8(110, 135, 175), RGB8(200, 215, 235),
            RGB8(15, 20, 35), RGB8(20, 150, 70), RGB8(60, 245, 120), RGB8(255, 255, 255),
            RGB8(15, 20, 35), RGB8(185, 250, 70), RGB8(60, 175, 40), RGB8(0, 0, 0)
        };
        // sprites: 0 Sotos' icon (lime, cyan), 1 ElAngel's (orange, yellow), 2 the squares
        static const palette_color_t intro_spr[12] = {
            RGB8(255, 255, 255), RGB8(0, 255, 255), RGB8(125, 255, 0), RGB8(0, 0, 0),
            RGB8(255, 255, 255), RGB8(255, 230, 0), RGB8(255, 120, 0), RGB8(0, 0, 0),
            RGB8(15, 20, 35), RGB8(40, 60, 120), RGB8(40, 60, 120), RGB8(40, 60, 120)
        };
        palette_color_t pals[20];   // fade_set_* are banked: RAM copies
        for (uint8_t i = 0; i < 20; i++) pals[i] = intro_pals[i];
        fade_set_bkg_palette(0, 5, pals);
        for (uint8_t i = 0; i < 12; i++) pals[i] = intro_spr[i];
        fade_set_sprite_palette(0, 3, pals);
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0);
        VBK_REG = 0;
    }
    fade_set_dmg_palettes(0xE4, 0xE4, 0xE4);

    for (uint8_t t = 0; t < STRIP_ROWS; t++) put_row(t);

    for (uint8_t i = 0; i < NUM_PARTS; i++) {
        uint8_t s = (uint8_t)(4u + i);
        set_sprite_tile(s, PART_TILE);
        set_sprite_prop(s, (uint8_t)(S_PRIORITY | ((_cpu == CGB_TYPE) ? 2u : 0u)));   // behind the text
    }

    uint8_t bank_a, bank_b;
    const uint8_t *src_a = icon_src(0, &bank_a);
    const uint8_t *src_b = icon_src(5, &bank_b);
    uint8_t shown = 0xFF;

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;
    fade_from_black(2);

    uint16_t cam = 0;                 // top of the screen in the strip, in pixels
    uint8_t filled = STRIP_ROWS - 1;  // the last strip row drawn
    uint8_t prev_joy = joypad();
    uint16_t f = 0;
    uint8_t hold = 0;

    while (hold < HOLD_FRAMES) {
        wait_vbl_done();
        SCY_REG = (uint8_t)cam;
        f++;
        if (cam < ROLL_PX) {
            uint8_t rem = (uint8_t)(ROLL_PX - cam);
            cam += (rem > 48u) ? 2u : (rem > 16u || (f & 1u)) ? 1u : 0u;
            uint8_t t = (uint8_t)((cam + 143u) >> 3);   // the bottom row on screen
            while (filled < t) put_row(++filled);
        } else {
            hold++;
        }

        // the icons turn a step every 3 frames, the second one the other way
        uint8_t step = (uint8_t)((f / 3u) % 48u);
        if (step != shown) {
            shown = step;
            uint8_t id = (step >= CUBE_ICON_FRAMES) ? (uint8_t)(step - CUBE_ICON_FRAMES) : step;
            cube_tiles_load_now(src_a + (uint16_t)id * 64u, bank_a, 0);
            uint8_t rid = (uint8_t)(CUBE_ICON_FRAMES - 1u - id);
            cube_tiles_load_now(src_b + (uint16_t)rid * 64u, bank_b, 4);
        }
        place_icon(0, NAME_ROW_A, cam, step);
        place_icon(1, NAME_ROW_B, cam, (uint8_t)(47u - step));

        // squares drift up, 1 px every 2..4 frames
        for (uint8_t i = 0; i < NUM_PARTS; i++) {
            uint8_t sp = (uint8_t)(2u + (i % 3u));
            uint8_t y = (uint8_t)(((uint16_t)part_y0[i] + 1760u - f / sp) % 176u);
            move_sprite((uint8_t)(4u + i), part_x[i], y);
        }

        uint8_t joy = joypad();
        if ((joy & ~prev_joy) & (J_START | J_A)) break;
        prev_joy = joy;
    }

    fade_capture_current();
    fade_to_black(2);
    HIDE_SPRITES;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
    SCX_REG = 0;
    SCY_REG = 0;
    return STATE_MENU;
}
