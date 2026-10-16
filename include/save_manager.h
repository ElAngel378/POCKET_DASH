#ifndef SAVE_MANAGER_H
#define SAVE_MANAGER_H

#include <stdint.h>
#include <gbdk/platform.h>

// Save slots (tools/build_levels.py gives each level one: level_save_slot[]), room for more
// levels than exist yet: changing this changes the save layout (see SAVE_VERSION)
#define NUM_SAVE_LEVELS 24
// The first slots (the main levels') are also in WRAM, the arrays below; the others only in SRAM
// (WRAM is full): saved_level_coins / record_level_* reach them
#define NUM_RAM_SAVE_LEVELS 16

extern uint8_t level_progress_normal[NUM_RAM_SAVE_LEVELS];
extern uint8_t level_progress_practice[NUM_RAM_SAVE_LEVELS];
// Famidash secret coins: bit 0/1/2 = coin 1/2/3 collected in a completed run
extern uint8_t level_coins[NUM_RAM_SAVE_LEVELS];

extern uint8_t selected_icon;
extern uint8_t selected_color_primary;
extern uint8_t selected_color_secondary;
extern uint8_t selected_dmg_primary;     // DMG shades of the player (0 white .. 3 black)
extern uint8_t selected_dmg_secondary;

void init_save_system(void) BANKED;
// Coins of save slot `slot` (any slot; 0 for none)
uint8_t saved_level_coins(uint8_t slot) BANKED;
// SETTINGS -> wipe: every slot's progress and coins
void clear_all_level_saves(void) BANKED;
void save_game_data(void) BANKED;
void record_level_progress(uint8_t level_idx, uint8_t pct, uint8_t is_practice) BANKED;
void record_level_coins(uint8_t level_idx, uint8_t coins) BANKED;
void record_level_progress_from_cam(uint8_t level_idx, uint16_t cam_x, uint16_t max_x, uint8_t is_practice) BANKED;

#endif // SAVE_MANAGER_H
