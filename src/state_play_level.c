#include "states.h"
#include "music.h"
#include "gameplay.h"
#include "hUGEDriver.h"
#include <gb/gb.h>

extern uint8_t selected;
extern uint8_t music_ready;
extern volatile uint8_t current_song_bank;
extern const hUGESong_t menuloop;

#include "settings.h"
#include "level_complete.h"

GameState update_play_level_state(void) {
    play_level(selected);

    // Level completed: the LEVEL COMPLETE state plays on (it restores the menu music itself)
    if (lc_pending) {
        lc_pending = 0;
        return STATE_LEVEL_COMPLETE;
    }

    // After play_level returns (win/death/quit), return to level select
    // Restore menu music
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    if (setting_music_enabled) {
        music_play_menu();
        TAC_REG = 0x04;
        music_ready = 1;
    } else {
        music_ready = 0;
    }

    return STATE_NEW_MENU_SELECT;
}
