#include "states.h"
#include "gameplay.h"
#include "hUGEDriver.h"
#include <gb/gb.h>

extern uint8_t selected;
extern uint8_t music_ready;
extern volatile uint8_t current_song_bank;
extern const hUGESong_t menuloop;

GameState update_play_level_state(void) {
    play_level(selected);

    // After play_level returns (win/death/quit), return to level select
    // Restore menu music
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    init_music_banked(&menuloop, 1, 176);
    current_song_bank = 1;
    TAC_REG = 0x04;
    music_ready = 1;

    return STATE_NEW_MENU_SELECT;
}
