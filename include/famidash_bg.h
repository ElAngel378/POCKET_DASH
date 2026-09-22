#ifndef FAMIDASH_BG_H
#define FAMIDASH_BG_H

#include <gb/gb.h>
#include <stdint.h>

// Apply a background color trigger by NES palette index.
void famidash_apply_bg_trigger(uint8_t color_id) BANKED;

// Apply a ground color trigger by NES palette index.
void famidash_apply_g_trigger(uint8_t color_id) BANKED;

// Reset all BG palettes to the defaults for level index idx (0-10).
void famidash_reset_bg_palettes(uint8_t idx) BANKED;

#endif
