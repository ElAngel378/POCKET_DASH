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

void draw_levels(void) {
    if (_cpu == CGB_TYPE) {
        fade_set_bkg_palette(0, 1, menu_pal);

        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0x00);
        VBK_REG = 0;
    }
    fade_set_dmg_palettes(0x2F, 0xE4, 0xE4);
    fill_bkg_rect(0, 0, 20, 18, 0x00);
    draw_text(0, 0, "LEVEL SELECT");
    for (uint8_t i = 0; i < MAX_LEVELS; i++) {
        if (i == selected) {
            draw_text(1, 2 + i, "0");
            draw_text(3, 2 + i, game_levels[i]->name);
        } else {
            draw_text(3, 2 + i, game_levels[i]->name);
        }
    }
    draw_text(0, 16, "PRESS A TO PLAY");
    SHOW_BKG;
    redraw = 0;
}

GameState update_level_select_state(void) {
    fade_set_black();
    DISPLAY_OFF;
    // Clear VRAM tiles and map to ensure no logo leftovers
    fill_bkg_rect(0, 0, 32, 32, 0);
    // Overwrite tile 0 with blank
    uint8_t blank_tile[16] = {0};
    set_bkg_data(0, 1, blank_tile);

    setup_menu_font();

    // Ensure scroll is reset for static background
    SCX_REG = 0;
    SCY_REG = 0;

    draw_levels();

    SHOW_BKG;
    fade_set_black();
    DISPLAY_ON;
    fade_from_black(2);

    uint8_t prev_joy = joypad();
    while (1) {
        if (redraw) draw_levels();

        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;

        if (pressed & J_UP) {
            if (selected > 0) { selected--; redraw = 1; }
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
