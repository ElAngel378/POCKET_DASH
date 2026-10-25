#pragma bank 221

#include "fade.h"

// CGB palette RAM ignores writes (and reads 0xFF) while the LCD draws a line (mode 3). GBDK's
// set_bkg_palette / set_sprite_palette write without checking: called late in a frame they
// dropped bytes, leaving half-written colours (a red patch on the title menu while it faded). All
// palette writes here go through pal_write instead: a byte at a time, only in modes 0 and 1
// (HBlank / VBlank; mode 2 follows mode 0, so a write that starts in mode 0 is done before mode 3),
// interrupts off for the check and the write.
static const palette_color_t *pw_src;
static uint8_t pw_bytes;      // 8 per palette, 1..64
static uint8_t pw_port;       // 0x68: BG (BCPS, data at 0x69), 0x6A: OBJ (OCPS, data at 0x6B)
static uint8_t pw_first;      // first byte index
static void pal_write_asm(void) __naked {
    __asm
        ld      a, (_pw_port)
        ld      c, a
        ld      a, (_pw_first)
        or      a, #0x80                ; auto increment
        ldh     (c), a
        inc     c                       ; data port
        ld      hl, #_pw_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      a, (_pw_bytes)
        ld      b, a
    1$:
        di
        ldh     a, (_STAT_REG + 0)
        and     a, #2                   ; mode 2 or 3: wait
        jr      Z, 2$
        ei
        jr      1$
    2$:
        ld      a, (hl+)
        ldh     (c), a
        ei
        dec     b
        jr      NZ, 1$
        ret
    __endasm;
}
static void pal_write(uint8_t port, uint8_t first, uint8_t count, const palette_color_t *data) {
    if (!count) return;
    pw_port = port; pw_first = (uint8_t)(first << 3); pw_bytes = (uint8_t)(count << 3); pw_src = data;
    pal_write_asm();
}
#define set_bkg_palette(first, count, data) pal_write(0x68, (first), (count), (data))
#define set_sprite_palette(first, count, data) pal_write(0x6A, (first), (count), (data))

palette_color_t shadow_bkg_palettes[32];
static palette_color_t shadow_spr_palettes[32];
static uint8_t active_bkg_count = 0;
static uint8_t active_spr_count = 0;

uint8_t fade_palettes_dirty = 0;
// Set while a menu loads with the display on and the screen black: fade_set_* only store the
// palettes (fade_from_black shows them), so nothing shows before the fade in. fade_from_black
// clears it.
uint8_t fade_hold = 0;

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
        if (!fade_hold) set_bkg_palette(first, count, data);
    }
}

void fade_buffer_bkg_palette(uint8_t first, uint8_t count, const palette_color_t *data) BANKED {
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
        fade_palettes_dirty = 1;
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
        if (!fade_hold) set_sprite_palette(first, count, data);
    }
}

void fade_set_dmg_palettes(uint8_t bgp, uint8_t obp0, uint8_t obp1) BANKED {
    shadow_bgp = bgp;
    shadow_obp0 = obp0;
    shadow_obp1 = obp1;
    if (fade_hold) return;
    BGP_REG = bgp;
    OBP0_REG = obp0;
    OBP1_REG = obp1;
}

void fade_apply_dirty_palettes(void) BANKED {
    if (fade_palettes_dirty) {
        if (_cpu == CGB_TYPE) {
            if (active_bkg_count > 0) set_bkg_palette(0, active_bkg_count, shadow_bkg_palettes);
            if (active_spr_count > 0) set_sprite_palette(0, active_spr_count, shadow_spr_palettes);
        } else {
            BGP_REG = shadow_bgp;
            OBP0_REG = shadow_obp0;
            OBP1_REG = shadow_obp1;
        }
        fade_palettes_dirty = 0;
    }
}

// Takes the palettes a state set directly (set_*_palette, BGP/OBP registers) as the fade target,
// so fade_to_black / fade_from_black work for it. CGB palette RAM only reads outside mode 3: each
// read waits for mode 0 / 1 (the 128 reads used to start at VBlank and ran past it, reading 0xFF).
void fade_capture_current(void) BANKED {
    if (_cpu == CGB_TYPE) {
        uint8_t i;
        for (i = 0; i < 64u; i++) {
            disable_interrupts();
            while (STAT_REG & 2u) { enable_interrupts(); disable_interrupts(); }
            BCPS_REG = i;   // reads do not auto-increment
            ((uint8_t *)shadow_bkg_palettes)[i] = BCPD_REG;
            enable_interrupts();
            disable_interrupts();
            while (STAT_REG & 2u) { enable_interrupts(); disable_interrupts(); }
            OCPS_REG = i;
            ((uint8_t *)shadow_spr_palettes)[i] = OCPD_REG;
            enable_interrupts();
        }
        active_bkg_count = 8;
        active_spr_count = 8;
    } else {
        shadow_bgp = BGP_REG;
        shadow_obp0 = OBP0_REG;
        shadow_obp1 = OBP1_REG;
    }
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

// A fade step: the dimmed palettes are worked out first (that takes a good part of a frame), then
// written from the next VBlank, so that the whole screen changes at once.
static palette_color_t step_bkg[32];
static palette_color_t step_spr[32];

static void compute_cgb_fade_step(uint8_t step) {
    uint8_t i;
    uint8_t bkg_colors = active_bkg_count << 2;
    uint8_t spr_colors = active_spr_count << 2;
    for (i = 0; i < bkg_colors; i++) step_bkg[i] = dim_color(shadow_bkg_palettes[i], step);
    for (i = 0; i < spr_colors; i++) step_spr[i] = dim_color(shadow_spr_palettes[i], step);
}

static void write_cgb_fade_step(void) {
    if (active_bkg_count > 0) set_bkg_palette(0, active_bkg_count, step_bkg);
    if (active_spr_count > 0) set_sprite_palette(0, active_spr_count, step_spr);
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

void fade_apply_pause_box_palettes(void) BANKED {
    if (_cpu == CGB_TYPE) {
        palette_color_t temp_bkg[16];
        uint8_t i;
        for (i = 0; i < 16; i++) {
            temp_bkg[i] = dim_color(shadow_bkg_palettes[i], 1);
        }
        set_bkg_palette(4, 4, temp_bkg);
    }
}

void fade_restore_pause_box_palettes(void) BANKED {
    if (_cpu == CGB_TYPE) {
        set_bkg_palette(0, 8, shadow_bkg_palettes);
    }
}

void fade_to_black(uint8_t delay_frames) BANKED {
    int8_t step;
    uint8_t f;

    if (_cpu == CGB_TYPE) {
        for (step = 3; step >= 0; step--) {
            compute_cgb_fade_step((uint8_t)step);
            wait_vbl_done();
            write_cgb_fade_step();
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
            compute_cgb_fade_step(step);
            wait_vbl_done();
            write_cgb_fade_step();
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
    fade_hold = 0;
}
