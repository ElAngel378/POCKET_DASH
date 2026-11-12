#ifndef MENU_ICON_CUT_H
#define MENU_ICON_CUT_H

#include <gb/gb.h>
#include <stdint.h>

// DMG title menu icon: the pixels of its n tiles (4: 2 x 2, or 1) at screen (sx, sy), drawn with the
// sprite flips in flags, that the big buttons cover: a byte per tile row into out[32] (the tiles in
// VRAM order). Returns 1 if there are any.
uint8_t menu_icon_cut(int16_t sx, uint8_t sy, uint8_t flags, uint8_t n, uint8_t *out) BANKED;

#endif
