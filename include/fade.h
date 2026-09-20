#ifndef FADE_H
#define FADE_H

#include <gb/gb.h>
#include <gb/cgb.h>

void fade_init(void) BANKED;
void fade_set_bkg_palette(uint8_t first, uint8_t count, const palette_color_t *data) BANKED;
void fade_set_sprite_palette(uint8_t first, uint8_t count, const palette_color_t *data) BANKED;
void fade_set_dmg_palettes(uint8_t bgp, uint8_t obp0, uint8_t obp1) BANKED;
void fade_set_black(void) BANKED;
void fade_to_black(uint8_t delay_frames) BANKED;
void fade_from_black(uint8_t delay_frames) BANKED;
void fade_apply_pause_tint(void) BANKED;
void fade_restore_pause_tint(void) BANKED;
void fade_apply_pause_box_palettes(void) BANKED;
void fade_restore_pause_box_palettes(void) BANKED;

inline static uint8_t dim_dmg_byte(uint8_t pal, uint8_t step) {
    uint8_t out = 0;
    for (uint8_t shift = 0; shift < 8; shift += 2) {
        uint8_t col = ((pal >> shift) & 0x03) + step;
        if (col > 3) col = 3;
        out |= (col << shift);
    }
    return out;
}

#endif
