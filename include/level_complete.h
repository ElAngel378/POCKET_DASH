#ifndef LEVEL_COMPLETE_H
#define LEVEL_COMPLETE_H

#include <stdint.h>

// Result of a completed level, filled in by play_level() for STATE_LEVEL_COMPLETE
extern uint8_t  lc_pending;    // 1: the level was just completed (state_play_level.c switches state)
extern uint8_t  lc_practice;
extern uint8_t  lc_coins;      // coin bits collected in this run
extern uint16_t lc_attempts;

#endif
