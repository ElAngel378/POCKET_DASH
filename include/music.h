#ifndef MUSIC_H
#define MUSIC_H

// Which driver plays the music: hUGEDriver songs (src/music) or Furnace songs (src/furnace,
// tools/convert_furnace.py). The timer interrupt in main.c ticks the one in music_driver.
#include <gb/gb.h>
#include "furnace_songs.h"

#define MUSIC_HUGE 0
#define MUSIC_FUR  1
extern volatile uint8_t music_driver;

// Timer pacing of the Furnace songs (main.c): the timer counts at 4096 Hz (8192 in CGB double
// speed); a tick lasts fur_tbase counts plus fur_tfrac / fur_tunit.
extern uint8_t fur_tbase;
extern uint16_t fur_tfrac, fur_tunit, fur_tacc;
extern int16_t fur_left;

// Menu theme: the Furnace one with the FURNACE setting. Doesn't set music_ready.
void music_play_menu(void) BANKED;
// Level song: its Furnace version when it has one and (FURNACE is on or it is SM, BoT, PG).
// Returns 0 if the level has no such song (play the hUGE one).
uint8_t music_play_level_fur(uint8_t level_idx) BANKED;
// Start a Furnace song (FUR_* id). Doesn't set music_ready.
void music_play_fur(uint8_t id) BANKED;
// Lend channels (4: wave, 8: noise) to a sound effect: the song stops writing them.
void music_borrow(uint8_t channels) BANKED;
void music_give_back(uint8_t channels) BANKED;
// After something else used wave RAM (pause): the song reloads its wave
void music_reload_wave(void) BANKED;

#endif
