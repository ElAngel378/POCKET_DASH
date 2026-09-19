#ifndef SAVE_MANAGER_H
#define SAVE_MANAGER_H

#include <stdint.h>
#include <gbdk/platform.h>

#define NUM_SAVE_LEVELS 11

extern uint8_t level_progress_normal[NUM_SAVE_LEVELS];
extern uint8_t level_progress_practice[NUM_SAVE_LEVELS];

void init_save_system(void) BANKED;
void save_game_data(void) BANKED;
void record_level_progress(uint8_t level_idx, uint8_t pct, uint8_t is_practice) BANKED;
void record_level_progress_from_cam(uint8_t level_idx, uint16_t cam_x, uint16_t max_x) BANKED;

#endif // SAVE_MANAGER_H
