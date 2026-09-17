#ifndef STATES_H
#define STATES_H

#include <gb/gb.h>
#include <stdint.h>

typedef enum {
    STATE_MENU,
    STATE_LEVEL_SELECT,
    STATE_NEW_MENU_SELECT,
    STATE_PLAY_LEVEL
} GameState;

// State functions. Each returns the next state to transition to.
GameState update_menu_state(void);
GameState update_level_select_state(void);
GameState update_new_menu_select_state(void) BANKED;
GameState update_play_level_state(void);

extern volatile uint8_t level_banner_scx;
void level_select_stat_isr(void);

#endif
