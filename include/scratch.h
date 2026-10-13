#ifndef SCRATCH_H
#define SCRATCH_H

#include <stdint.h>

// One WRAM buffer shared by work that never overlaps in time, so the stack keeps room (WRAM is
// full: the title / level complete loads take the stack close to the variables).
//   - icon_preview_load (icon_previews.c): the recolored preview tiles, during the call
//   - the title menu (state_menu.c): ground_src, while it builds the ground tiles on entry
//   - level complete (state_level_complete.c): its VRAM slot tables, while the screen is shown
//   - the level select (state_new_menu_select.c): the progress bar tiles, during the call
#define SCRATCH_SIZE 256
extern uint8_t scratch_buf[SCRATCH_SIZE];

#endif
