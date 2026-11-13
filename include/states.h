#ifndef STATES_H
#define STATES_H

#include <gb/gb.h>
#include <stdint.h>

typedef enum {
    STATE_MENU,
    STATE_LEVEL_SELECT,
    STATE_NEW_MENU_SELECT,
    STATE_PLAY_LEVEL,
    STATE_MUSIC_TEST,
    STATE_SETTINGS,
    STATE_LEVEL_COMPLETE,
    STATE_ICON_SELECT,
    STATE_CREDITS,              // the full credits, back to the menu (title: SELECT)
    STATE_CREDITS_SETTINGS,     // the full credits, back to the settings
    STATE_SETTINGS_AT_CREDITS,  // the settings, the cursor on CREDITS
    STATE_INTRO                 // at boot, before the menu
} GameState;

// State functions. Each returns the next state to transition to.
GameState update_menu_state(void) BANKED;
GameState update_level_select_state(void) BANKED;
GameState update_new_menu_select_state(void) BANKED;
GameState update_play_level_state(void);
GameState update_music_test_state(void) BANKED;
GameState update_settings_state(uint8_t at_credits) BANKED;
GameState update_level_complete_state(void) BANKED;
GameState update_credits_state(GameState back) BANKED;
GameState update_intro_state(void) BANKED;
GameState update_icon_select_state(void) BANKED;

extern volatile uint8_t level_banner_scx;
void level_select_stat_isr(void);
void level_select_vbl_isr(void);

#endif
