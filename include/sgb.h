#ifndef SGB_H
#define SGB_H

#include <stdint.h>
#include <gbdk/platform.h>

// Super Game Boy: 1 on an SGB / SGB2 (set by sgb_init), 0 on every other console.
extern uint8_t sgb_present;

// Boot only (before the timer interrupt and the joypad reads: the packets go out through the joypad
// register): detects the SGB, then gives it the game's default colors. Does nothing elsewhere.
void sgb_init(void) BANKED;

// Colors of the screen (SGB palette 0, the one every tile uses): c0 is the lightest shade (BGP
// color 0), c3 the darkest, each a 15 bit RGB value (RGB8() in gb/cgb.h). No-op unless sgb_present.
void sgb_set_palette(uint16_t c0, uint16_t c1, uint16_t c2, uint16_t c3) BANKED;

// The level's SGB colours: one palette for the whole screen (the SNES colours regions, not tiles), the
// CGB level palette's colours. On SGB the DMG theme stays bg_pals[1] (the gradient setting does
// nothing, like on CGB) and the colour triggers change these instead. color_id is the background
// colour id (the colour triggers' ids). The packet goes out later, sgb_frame() (once per frame while
// playing), or at once with sgb_flush(). No-ops unless sgb_present.
void sgb_set_sky(uint8_t color_id) BANKED;
void sgb_flush(void) BANKED;
// The menus' colours again (a level was left)
void sgb_leave_level(void) BANKED;
void sgb_update(void) BANKED;

extern uint8_t sgb_pending;   // a palette waits to be sent
extern uint8_t sgb_sky_id;    // the sky colour id the level has now (practice checkpoints keep it)
extern uint8_t sgb_in_level;
extern uint8_t sgb_gap;
// the level's sky is near black (set with the palette it goes out with): the player gets a white outline
extern uint8_t sgb_dark_sky;

// Once per frame in the level: counts the gap down, sends a waiting palette
inline static void sgb_frame(void) {
  if (sgb_pending || sgb_gap) sgb_update();
}

#endif // SGB_H
