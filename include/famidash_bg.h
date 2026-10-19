#ifndef FAMIDASH_BG_H
#define FAMIDASH_BG_H

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>

extern palette_color_t famidash_bg_palettes[20];
extern uint8_t famidash_bkg_palettes_dirty;
// Colours the colour triggers fade to (famidash_bg_palettes = what is on screen)
extern palette_color_t famidash_bg_target[20];
// CGB, once per frame: the 0.3 s fade to famidash_bg_target after a colour trigger
void famidash_bg_fade_step(void) BANKED;
// Shows pal (20 colours) right away, no fade
void famidash_bg_set_now(const palette_color_t *pal) BANKED;

// Apply a background color trigger by NES palette index.
void famidash_apply_bg_trigger(uint8_t color_id) BANKED;

// Block outline colour trigger n (0..15, Famidash objects $B0..$BF): colour 3 of palettes 0, 1
void famidash_apply_outline_trigger(uint8_t n) BANKED;

// Apply a ground color trigger by NES palette index.
void famidash_apply_g_trigger(uint8_t color_id) BANKED;

// Reset all BG palettes to the defaults for level index idx (0-10).
void famidash_reset_bg_palettes(uint8_t idx) BANKED;

// Colour of the background decorations (BG palette 2 colour 3): the cube's colour 1
void famidash_set_bg_accent(palette_color_t c) BANKED;

inline static void famidash_apply_palettes(void) {
    if (_cpu == CGB_TYPE && famidash_bkg_palettes_dirty) {
        BCPS_REG = 0x80 | 0;
        const uint8_t *p = (const uint8_t *)famidash_bg_palettes;
        uint8_t n = 40;
        do {
            BCPD_REG = *p++;
        } while (--n);
        // palette 5 (fake spikes): sky, darker sky, black, black (see upload_palette)
        p = (const uint8_t *)famidash_bg_palettes;
        n = 4;
        do {
            BCPD_REG = *p++;
        } while (--n);
        n = 4;
        do {
            BCPD_REG = 0;
        } while (--n);
        famidash_bkg_palettes_dirty = 0;
    }
}

#endif
