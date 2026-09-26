#ifndef FAMIDASH_BG_H
#define FAMIDASH_BG_H

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>

extern palette_color_t famidash_bg_palettes[20];
extern uint8_t famidash_bkg_palettes_dirty;

// Apply a background color trigger by NES palette index.
void famidash_apply_bg_trigger(uint8_t color_id) BANKED;

// Apply a ground color trigger by NES palette index.
void famidash_apply_g_trigger(uint8_t color_id) BANKED;

// Reset all BG palettes to the defaults for level index idx (0-10).
void famidash_reset_bg_palettes(uint8_t idx) BANKED;

inline static void famidash_apply_palettes(void) {
    if (_cpu == CGB_TYPE && famidash_bkg_palettes_dirty) {
        BCPS_REG = 0x80 | 0;
        const uint8_t *p = (const uint8_t *)famidash_bg_palettes;
        uint8_t n = 40;
        do {
            BCPD_REG = *p++;
        } while (--n);
        famidash_bkg_palettes_dirty = 0;
    }
}

#endif
