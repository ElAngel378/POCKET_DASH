#pragma bank 27

#include "states.h"
#include "gameplay.h"
#include "assets.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "fade.h"
#include "hUGEDriver.h"
#include "gbc_palettes.h"
#include <gb/gb.h>

extern uint8_t selected;
extern uint8_t redraw;
extern uint8_t music_ready;
extern volatile uint8_t current_song_bank;
extern const hUGESong_t menuloop;

// The extra levels (the wrench in the main menu): game_levels[NUM_MAIN_LEVELS..], one per row
#define FIRST_ROW 2
#define CURSOR_TILE (FONT_PUSAB_START + 40)   // after the font, like the settings' cursor
static const uint8_t cursor_tile[16] = {     // right arrow > (state_settings.c)
    0x00,0x00, 0x20,0x30, 0x38,0x3C, 0x3E,0x3F, 0x3E,0x3F, 0x38,0x3C, 0x20,0x30, 0x00,0x00
};

// draw_text runs in another bank: this bank's strings go through RAM (the level names are in
// bank 0, assets.c)
static void draw_text_here(uint8_t x, uint8_t y, const char *s) {
    char buf[16];
    uint8_t i = 0;
    while (s[i] && i < sizeof(buf) - 1) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    draw_text(x, y, buf);
}

void draw_levels(void) {
    if (_cpu == CGB_TYPE) {
        fade_set_bkg_palette(0, 1, menu_pal);

        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0x00);
        VBK_REG = 0;
    }
    fade_set_dmg_palettes(0x2F, 0xE4, 0xE4);
    fill_bkg_rect(0, 0, 20, 18, 0x00);
    draw_text_here(0, 0, "EXTRA LEVELS");
    for (uint8_t i = NUM_MAIN_LEVELS; i < MAX_LEVELS; i++) {
        uint8_t row = (uint8_t)(FIRST_ROW + i - NUM_MAIN_LEVELS);
        if (i == selected) set_bkg_tile_xy(0, row, CURSOR_TILE);
        draw_text(2, row, game_levels[i]->name);   // up to 18 letters
    }
    draw_text_here(0, 16, "PRESS A TO PLAY");
    SHOW_BKG;
    redraw = 0;
}

GameState update_level_select_state(void) BANKED {
    fade_set_black();
    DISPLAY_OFF;
    // clear the title logo
    fill_bkg_rect(0, 0, 32, 32, 0);
    uint8_t blank_tile[16] = {0};
    set_bkg_data(0, 1, blank_tile);

    setup_menu_font();
    set_bkg_data(CURSOR_TILE, 1, cursor_tile);

    SCX_REG = 0;
    SCY_REG = 0;

    draw_levels();

    SHOW_BKG;
    fade_set_black();
    DISPLAY_ON;
    fade_from_black(2);

    if (selected < NUM_MAIN_LEVELS || selected >= MAX_LEVELS) selected = NUM_MAIN_LEVELS;
    uint8_t prev_joy = joypad();
    while (1) {
        if (redraw) draw_levels();

        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;

        if (pressed & J_UP) {
            if (selected > NUM_MAIN_LEVELS) { selected--; redraw = 1; }
        } else if (pressed & J_DOWN) {
            if (selected < MAX_LEVELS - 1) { selected++; redraw = 1; }
        } else if (pressed & J_A) {
            music_ready = 0;
            TAC_REG = 0x00;
            play_sample(BANK_SFX_DATA, play_sound_data, PLAY_SOUND_LEN);
            fade_to_black(2);
            return STATE_PLAY_LEVEL;
        } else if (pressed & J_B) {
            return STATE_MENU;
        }

        wait_vbl_done();
    }
}
