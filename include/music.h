#ifndef MUSIC_H
#define MUSIC_H

// Which driver plays the music: hUGEDriver songs (src/music) or Furnace songs (src/furnace,
// tools/convert_furnace.py). The timer interrupt in main.c ticks the one in music_driver.
#include <gb/gb.h>
#include "furnace_songs.h"

#define MUSIC_HUGE 0
#define MUSIC_FUR  1
extern volatile uint8_t music_driver;   // in HRAM (src/furnace/music_hram.s), like the below

// Timer pacing of the Furnace songs (main.c): the timer counts at 4096 Hz (8192 in CGB double
// speed); a tick lasts fur_tbase counts plus fur_tfrac / 16384.
extern uint8_t fur_tbase;
extern uint16_t fur_tfrac, fur_tacc;
extern int16_t fur_left;

// Menu theme: the Furnace one, the hUGETracker one with the LEGACY setting. Doesn't set music_ready.
void music_play_menu(void) BANKED;
// Level song: its Furnace version, unless the LEGACY setting is on or the level has none.
// Returns 0 when it didn't start one (play the hUGE song).
uint8_t music_play_level_fur(uint8_t level_idx) BANKED;
// Start a Furnace song (FUR_* id). Doesn't set music_ready.
void music_play_fur(uint8_t id) BANKED;
// Lend channels (4: wave, 8: noise) to a sound effect: the song stops writing them.
// Bank 0 (main.c): the timer interrupt calls them, and a banked call there costs stack.
void music_borrow(uint8_t channels);
void music_give_back(uint8_t channels);
// After something else used wave RAM (pause): the song reloads its wave
void music_reload_wave(void) BANKED;
// Music paced by the level (main.c): music_gate_on when play_level's loop (re)starts, then
// music_grant once per iteration; music_gate_off whenever the loop stops (pause, death, exit)
extern volatile uint8_t music_gate;
void music_gate_on(void);
void music_gate_off(void);
void music_grant(void);
// In a level, Furnace ticks run whole (no interrupt in between, like Furnace's own writes); one
// due too close to VBlank waits for the VBlank handler, which then runs it (music_vbl_run)
extern volatile uint8_t music_vbl_ticks;
void music_vbl_run(void);

#endif
