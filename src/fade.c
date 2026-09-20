#pragma bank 221

#include "fade.h"

static palette_color_t shadow_bkg_palettes[32];
static palette_color_t shadow_spr_palettes[32];
static uint8_t active_bkg_count = 0;
static uint8_t active_spr_count = 0;

static uint8_t shadow_bgp = 0xE4;
static uint8_t shadow_obp0 = 0xE4;
static uint8_t shadow_obp1 = 0xE4;

void fade_init(void) BANKED {
    uint8_t i;
    for (i = 0; i < 32; i++) {
        shadow_bkg_palettes[i] = 0;
        shadow_spr_palettes[i] = 0;
    }
    active_bkg_count = 0;
    active_spr_count = 0;
    shadow_bgp = 0xE4;
    shadow_obp0 = 0xE4;
    shadow_obp1 = 0xE4;
}

void fade_set_bkg_palette(uint8_t first, uint8_t count, const palette_color_t *data) BANKED {
    if (_cpu == CGB_TYPE) {
        uint8_t start_idx = first << 2;
        uint8_t total_colors = count << 2;
        uint8_t i;
        for (i = 0; i < total_colors; i++) {
            if (start_idx + i < 32) {
                shadow_bkg_palettes[start_idx + i] = data[i];
            }
        }
        if (first + count > active_bkg_count) {
            active_bkg_count = first + count;
        }
        set_bkg_palette(first, count, data);
    }
}

void fade_set_sprite_palette(uint8_t first, uint8_t count, const palette_color_t *data) BANKED {
    if (_cpu == CGB_TYPE) {
        uint8_t start_idx = first << 2;
        uint8_t total_colors = count << 2;
        uint8_t i;
        for (i = 0; i < total_colors; i++) {
            if (start_idx + i < 32) {
                shadow_spr_palettes[start_idx + i] = data[i];
            }
        }
        if (first + count > active_spr_count) {
            active_spr_count = first + count;
        }
        set_sprite_palette(first, count, data);
    }
}

void fade_set_dmg_palettes(uint8_t bgp, uint8_t obp0, uint8_t obp1) BANKED {
    shadow_bgp = bgp;
    shadow_obp0 = obp0;
    shadow_obp1 = obp1;
    BGP_REG = bgp;
    OBP0_REG = obp0;
    OBP1_REG = obp1;
}

void fade_set_black(void) BANKED {
    if (_cpu == CGB_TYPE) {
        static const palette_color_t black_pals[32] = {0};
        set_bkg_palette(0, 8, black_pals);
        set_sprite_palette(0, 8, black_pals);
    } else {
        BGP_REG = 0xFF;
        OBP0_REG = 0xFF;
        OBP1_REG = 0xFF;
    }
}

static inline uint16_t dim_color(uint16_t c, uint8_t step) {
    if (step == 0) return 0;
    if (step >= 4) return c;
    uint8_t r = (uint8_t)(c & 0x1F);
    uint8_t g = (uint8_t)((c >> 5) & 0x1F);
    uint8_t b = (uint8_t)((c >> 10) & 0x1F);
    r = (uint8_t)((r * step) >> 2);
    g = (uint8_t)((g * step) >> 2);
    b = (uint8_t)((b * step) >> 2);
    return (uint16_t)r | ((uint16_t)g << 5) | ((uint16_t)b << 10);
}

static void apply_cgb_fade_step(uint8_t step) {
    palette_color_t temp_bkg[32];
    palette_color_t temp_spr[32];
    uint8_t i;
    uint8_t bkg_colors = active_bkg_count << 2;
    uint8_t spr_colors = active_spr_count << 2;

    for (i = 0; i < bkg_colors; i++) {
        temp_bkg[i] = dim_color(shadow_bkg_palettes[i], step);
    }
    for (i = 0; i < spr_colors; i++) {
        temp_spr[i] = dim_color(shadow_spr_palettes[i], step);
    }

    if (active_bkg_count > 0) {
        set_bkg_palette(0, active_bkg_count, temp_bkg);
    }
    if (active_spr_count > 0) {
        set_sprite_palette(0, active_spr_count, temp_spr);
    }
}

void fade_apply_pause_tint(void) BANKED {
    if (_cpu == CGB_TYPE) {
        palette_color_t temp_bkg[32];
        palette_color_t temp_spr[32];
        uint8_t i;
        uint8_t bkg_colors = (active_bkg_count > 0 ? active_bkg_count : 8) << 2;
        uint8_t spr_colors = (active_spr_count > 0 ? active_spr_count : 8) << 2;
        for (i = 0; i < bkg_colors; i++) {
            temp_bkg[i] = dim_color(shadow_bkg_palettes[i], 3);
        }
        for (i = 0; i < spr_colors; i++) {
            temp_spr[i] = dim_color(shadow_spr_palettes[i], 3);
        }
        set_bkg_palette(0, (active_bkg_count > 0 ? active_bkg_count : 8), temp_bkg);
        set_sprite_palette(0, (active_spr_count > 0 ? active_spr_count : 8), temp_spr);
    }
}

void fade_restore_pause_tint(void) BANKED {
    if (_cpu == CGB_TYPE) {
        uint8_t bkg_cnt = (active_bkg_count > 0 ? active_bkg_count : 8);
        uint8_t spr_cnt = (active_spr_count > 0 ? active_spr_count : 8);
        set_bkg_palette(0, bkg_cnt, shadow_bkg_palettes);
        set_sprite_palette(0, spr_cnt, shadow_spr_palettes);
    }
}

void fade_to_black(uint8_t delay_frames) BANKED {
    int8_t step;
    uint8_t f;

    if (_cpu == CGB_TYPE) {
        for (step = 3; step >= 0; step--) {
            wait_vbl_done();
            apply_cgb_fade_step((uint8_t)step);
            for (f = 1; f < delay_frames; f++) wait_vbl_done();
        }
    } else {
        for (step = 1; step <= 3; step++) {
            wait_vbl_done();
            BGP_REG = dim_dmg_byte(shadow_bgp, (uint8_t)step);
            OBP0_REG = dim_dmg_byte(shadow_obp0, (uint8_t)step);
            OBP1_REG = dim_dmg_byte(shadow_obp1, (uint8_t)step);
            for (f = 1; f < delay_frames; f++) wait_vbl_done();
        }
    }
}

void fade_from_black(uint8_t delay_frames) BANKED {
    uint8_t step;
    uint8_t f;

    if (_cpu == CGB_TYPE) {
        for (step = 1; step <= 4; step++) {
            wait_vbl_done();
            apply_cgb_fade_step(step);
            for (f = 1; f < delay_frames; f++) wait_vbl_done();
        }
    } else {
        for (step = 3; ; step--) {
            wait_vbl_done();
            BGP_REG = dim_dmg_byte(shadow_bgp, step);
            OBP0_REG = dim_dmg_byte(shadow_obp0, step);
            OBP1_REG = dim_dmg_byte(shadow_obp1, step);
            for (f = 1; f < delay_frames; f++) wait_vbl_done();
            if (step == 0) break;
        }
    }
}
