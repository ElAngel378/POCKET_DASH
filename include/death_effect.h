#ifndef DEATH_EFFECT_H
#define DEATH_EFFECT_H

#include <gb/gb.h>

#define DEATH_TILE_BASE 20
#define DEATH_TILE_COUNT 16   // sprite tiles 20..35

#define DEATH_TILE_CIRCLE_P1 (DEATH_TILE_BASE + 0)
#define DEATH_TILE_CIRCLE_P2 (DEATH_TILE_BASE + 2)
#define DEATH_TILE_CIRCLE_P3 (DEATH_TILE_BASE + 4)
#define DEATH_TILE_PART_CYAN (DEATH_TILE_BASE + 6)
#define DEATH_TILE_PART_GREEN (DEATH_TILE_BASE + 8)
#define DEATH_TILE_PART_SMALL_CYAN (DEATH_TILE_BASE + 10)
#define DEATH_TILE_PART_SMALL_GREEN (DEATH_TILE_BASE + 12)
#define DEATH_TILE_PART_TINY (DEATH_TILE_BASE + 14)

// keep_music: practice mode, the music keeps playing through the death. Returns number of checkpoint removal requests during animation.
uint8_t play_death_animation(uint8_t screen_x, uint8_t screen_y, uint8_t scroll_px, uint8_t cam_py, uint8_t keep_music) BANKED;

// Level complete screen: random discs expanding one at a time, each with a burst of particles.
// Sprites 4.. only (0..3 are the percent HUD); sprite tiles END_FX_TILE_BASE..+35 (end_fx_discs.h),
// CGB sprite palette 6. Level sprites are hidden on that screen; a level start reloads them all.
#define END_FX_TILE_BASE 36
void end_fx_init(void) BANKED;
// A splash of stars out of the LEVEL COMPLETE text (once it is drawn)
void end_fx_splash(void) BANKED;
// Advances one frame, draws from sprite oam_idx on and hides the sprites it drew last frame and not
// now; returns the next free sprite
uint8_t end_fx_draw(uint8_t oam_idx) BANKED;

#endif
