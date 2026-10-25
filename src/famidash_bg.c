#pragma bank 13

#include <gb/gb.h>

#include "famidash_bg.h"
#include "fade.h"
#include "bg_parallax.h"
#include "assets.h"

// Sky/ground colours per NES colour id live in famidash_bg_tables.h, generated
// by tools/gen_famidash_bg_tables.py from Famidash's own NES palette.

// the palettes are local to this file, which keeps DMG fast
#include <string.h>

static const uint16_t vibrant_palette_default[20] = {
    RGB( 1,  9, 24), RGB( 0,  4, 14), RGB( 0,  0,  0), RGB(31, 31, 31), // palette 0
    RGB( 1,  9, 24), RGB( 0,  4, 14), RGB( 1,  9, 24), RGB(31, 31, 31), // palette 1
    RGB( 1,  9, 24), RGB( 0,  4, 14), RGB( 0,  0,  0), RGB( 9, 25,  4), // palette 2: bg decoration accent (the cube's colour 1, famidash_set_bg_accent)
    RGB( 1,  9, 24), RGB( 0,  4, 14), RGB( 0,  0,  0), RGB( 0,  0,  0), // palette 3
    RGB(31, 31, 31), RGB( 9, 25,  4), RGB( 5, 14,  0), RGB( 1,  7,  0)  // palette 4: ground (white line, ground, grid1, grid2)
};

// famidash_bg_palettes: what is on screen (the VBlank handler uploads it). The colour triggers
// set famidash_bg_target and the screen fades to it in BG_FADE_FRAMES frames (0.3 s): every
// colour channel moves by a fixed 8.8 step, computed once per trigger.
// All byte arithmetic with lookup tables: the first version (int16 channels, variable shifts)
// took 1.4 frames to start a fade and a quarter of every frame while it ran, so every colour
// trigger dropped frames.
#define BG_FADE_FRAMES 18
palette_color_t famidash_bg_palettes[20];
palette_color_t famidash_bg_target[20];
// Only the colours that change are faded: fade_col[k] (k < fade_n) is a colour index, and
// fade_ch[3k .. 3k+2] its r, g, b channels, each { value 0..31, 8.8 fraction, step lo, step hi }
typedef struct { uint8_t val, frac, step_lo, step_hi; } FadeCh;
static FadeCh fade_ch[60];
static uint8_t fade_col[20];
static uint8_t fade_n;
static uint8_t fade_nch;            // 3 * fade_n, read by fade_channels
static uint8_t fade_left;           // frames to go, 0: not fading
static uint8_t fade_restart;        // a trigger changed famidash_bg_target this frame

// 8.8 step for a channel change of d = index - 31: d * 256 / 18 ~= d * 57 / 4
static const uint8_t step_tab_lo[63] = { 0x46, 0x54, 0x62, 0x71, 0x7F, 0x8D, 0x9B, 0xAA, 0xB8, 0xC6, 0xD4, 0xE3, 0xF1, 0xFF, 0x0D, 0x1C, 0x2A, 0x38, 0x46, 0x55, 0x63, 0x71, 0x7F, 0x8E, 0x9C, 0xAA, 0xB8, 0xC7, 0xD5, 0xE3, 0xF1, 0x00, 0x0E, 0x1C, 0x2A, 0x39, 0x47, 0x55, 0x63, 0x72, 0x80, 0x8E, 0x9C, 0xAB, 0xB9, 0xC7, 0xD5, 0xE4, 0xF2, 0x00, 0x0E, 0x1D, 0x2B, 0x39, 0x47, 0x56, 0x64, 0x72, 0x80, 0x8F, 0x9D, 0xAB, 0xB9 };
static const uint8_t step_tab_hi[63] = { 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
// Green channel g (0..31) in the two bytes of an RGB555 colour
static const uint8_t g_to_lo[32] = { 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0 };
static const uint8_t g_to_hi[32] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 };

static FadeCh *ch_p;
static void setup_ch(uint8_t from, uint8_t to) {
    uint8_t k = (uint8_t)(to + 31u - from);
    ch_p->val = from;
    ch_p->frac = 0x80;              // rounds to the nearest step
    ch_p->step_lo = step_tab_lo[k];
    ch_p->step_hi = step_tab_hi[k];
    ch_p++;
}

// The fade starts from the colours on screen (a trigger during a fade continues smoothly)
static void start_fade(void) {
    const uint8_t *a = (const uint8_t *)famidash_bg_palettes;
    const uint8_t *b = (const uint8_t *)famidash_bg_target;
    uint8_t n = 0;
    ch_p = fade_ch;
    for (uint8_t i = 0; i < 20; i++, a += 2, b += 2) {
        uint8_t alo = a[0], ahi = a[1], blo = b[0], bhi = b[1];
        if (alo == blo && ahi == bhi) continue;
        fade_col[n++] = i;
        setup_ch(alo & 31u, blo & 31u);
        setup_ch((uint8_t)((alo >> 5) | (ahi << 3)) & 31u, (uint8_t)((blo >> 5) | (bhi << 3)) & 31u);
        setup_ch((ahi >> 2) & 31u, (bhi >> 2) & 31u);
    }
    fade_n = n;
    fade_nch = (uint8_t)(n * 3u);
    fade_left = n ? BG_FADE_FRAMES : 0;
}

// Adds every channel's 8.8 step (fade_nch > 0 channels)
static void fade_channels(void) __naked {
    __asm
        ld      a, (_fade_nch)
        ld      b, a
        ld      hl, #_fade_ch
    1$:
        ld      c, (hl)         ; value
        inc     hl
        ld      a, (hl+)        ; fraction
        add     a, (hl)         ; + step lo
        inc     hl
        ld      e, a
        ld      a, c
        adc     a, (hl)         ; value + step hi + carry
        dec     hl
        dec     hl
        ld      (hl), e
        dec     hl
        ld      (hl+), a
        inc     hl
        inc     hl
        inc     hl
        dec     b
        jr      NZ, 1$
        ret
    __endasm;
}

// shadow_bkg_palettes' palette 5 (fake spikes, Famidash PAL_3): sky, darker sky, black, black,
// from the level palettes (not stored there, see upload_palette)
static void shadow_fake_spike_palette(void) {
    shadow_bkg_palettes[20] = famidash_bg_palettes[0];
    shadow_bkg_palettes[21] = famidash_bg_palettes[1];
    shadow_bkg_palettes[22] = 0;
    shadow_bkg_palettes[23] = 0;
}

// shadow_bkg_palettes' palettes 6 and 7, for the fading objects' in-between frames (mt_renderer.c):
// palette 0 a third / two thirds of the way from the sky (its colour 0) to its colours
extern uint8_t fade_count;   // the level has fading objects (mt_renderer.c)
static const uint8_t div3[94] = { 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 19, 20, 20, 20, 21, 21, 21, 22, 22, 22, 23, 23, 23, 24, 24, 24, 25, 25, 25, 26, 26, 26, 27, 27, 27, 28, 28, 28, 29, 29, 29, 30, 30, 30, 31, 31 };
static void shadow_fade_palettes(void) {
    const uint8_t *s = (const uint8_t *)famidash_bg_palettes;
    uint8_t sr = s[0] & 31u, sg = (uint8_t)((s[0] >> 5) | (s[1] << 3)) & 31u, sb = (s[1] >> 2) & 31u;
    palette_color_t *d = &shadow_bkg_palettes[24];
    for (uint8_t i = 0; i < 4; i++, s += 2, d++) {
        uint8_t r = s[0] & 31u, g = (uint8_t)((s[0] >> 5) | (s[1] << 3)) & 31u, b = (s[1] >> 2) & 31u;
        d[0] = RGB(div3[sr + sr + r], div3[sg + sg + g], div3[sb + sb + b]);
        d[4] = RGB(div3[sr + r + r], div3[sg + g + g], div3[sb + b + b]);
    }
}

// Once per frame (CGB): next step of a running fade
void famidash_bg_fade_step(void) BANKED {
    if (fade_restart) {
        // set up this frame, first step on the next one (spreads the work)
        fade_restart = 0;
        start_fade();
        return;
    }
    if (!fade_left) return;
    if (--fade_left == 0) {
        memcpy(famidash_bg_palettes, famidash_bg_target, sizeof(famidash_bg_target));
        // the pause box and the death fade start from shadow_bkg_palettes
        memcpy(shadow_bkg_palettes, famidash_bg_palettes, 20 * sizeof(palette_color_t));
        shadow_fake_spike_palette();
    } else {
        fade_channels();
        const FadeCh *c = fade_ch;
        for (uint8_t k = 0; k < fade_n; k++, c += 3) {
            uint8_t *p = (uint8_t *)&famidash_bg_palettes[fade_col[k]];
            uint8_t g = c[1].val;
            p[0] = c[0].val | g_to_lo[g];
            p[1] = g_to_hi[g] | (uint8_t)(c[2].val << 2);
        }
    }
    if (fade_count) shadow_fade_palettes();
    famidash_bkg_palettes_dirty = 1;
}

// Shows pal (20 colours) right away, no fade (practice checkpoint restore)
void famidash_bg_set_now(const palette_color_t *pal) BANKED {
    memcpy(famidash_bg_target, pal, sizeof(famidash_bg_target));
    memcpy(famidash_bg_palettes, pal, sizeof(famidash_bg_target));
    memcpy(shadow_bkg_palettes, pal, 20 * sizeof(palette_color_t));
    shadow_fake_spike_palette();
    shadow_fade_palettes();
    fade_left = 0;
    fade_restart = 0;
}
static palette_color_t current_sky_color = RGB( 1,  9, 24);
// Colour 3 of palette 2 (the background decorations, BG spikes): the cube's colour 1, like
// Famidash's player colour in its background palette
static palette_color_t bg_accent = RGB(15, 31, 0);

void famidash_set_bg_accent(palette_color_t c) BANKED {
    bg_accent = c;
    famidash_bg_target[11] = c;
    famidash_bg_palettes[11] = c;
    shadow_bkg_palettes[11] = c;
    famidash_bkg_palettes_dirty = 1;
}
static palette_color_t current_g_color = RGB( 9, 25,  4);

uint8_t famidash_bkg_palettes_dirty = 0;

#include "famidash_bg_tables.h"

void famidash_apply_bg_trigger(uint8_t color_id) BANKED {
    uint8_t idx = (color_id == 31u) ? 0x2Cu : (color_id & 0x3Fu);
    const famidash_sky_shades_t *shades = &famidash_sky_table[idx];
    if (shades->color == current_sky_color) return;

    current_sky_color = shades->color;

    // Palette 0: Normal level tiles
    famidash_bg_target[0] = shades->color;
    famidash_bg_target[1] = shades->darker;
    famidash_bg_target[2] = RGB(0, 0, 0);

    // Palette 1: Accent / Ground (preserve ground color in 5 and 6)
    famidash_bg_target[4] = shades->color;

    // Palette 2: BG Spikes / Hazards
    famidash_bg_target[8] = shades->color;
    famidash_bg_target[9] = shades->darker;
    famidash_bg_target[10] = RGB(0, 0, 0);
    famidash_bg_target[11] = bg_accent;

    // Palette 3: Parallax BG
    famidash_bg_target[12] = shades->color;
    famidash_bg_target[13] = shades->border;
    famidash_bg_target[14] = shades->body;
    famidash_bg_target[15] = shades->shadow;

    // Palette 4: Ground Grid (Color 0 is the top white line, always pure white)
    famidash_bg_target[16] = RGB(31, 31, 31);

    fade_restart = 1;
}

// Famidash's OUTLINES: the NES colour of each outline trigger ($B0..$BF)
static const uint8_t outline_nes_color[16] = {
    0x30, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x21, 0x17, 0x0F
};

// Block outlines: colour 3 of palettes 0 and 1 (Famidash pal_col 3 and 7), right away (no
// fade, like Famidash). The sky trigger leaves them alone, so a checkpoint's palette copy keeps
// the outline it had, and a running sky fade never touches them.
void famidash_apply_outline_trigger(uint8_t n) BANKED {
    palette_color_t c = famidash_sky_table[outline_nes_color[n & 15u]].color;
    if (famidash_bg_target[3] == c && famidash_bg_target[7] == c) return;
    famidash_bg_target[3] = c;
    famidash_bg_target[7] = c;
    famidash_bg_palettes[3] = c;
    famidash_bg_palettes[7] = c;
    shadow_bkg_palettes[3] = c;
    shadow_bkg_palettes[7] = c;
    if (fade_count) shadow_fade_palettes();
    famidash_bkg_palettes_dirty = 1;
}

void famidash_apply_g_trigger(uint8_t color_id) BANKED {
    uint8_t idx = (color_id == 31u) ? 0x2Cu : (color_id & 0x3Fu);

    const famidash_sky_shades_t *sky_entry = &famidash_sky_table[idx];
    palette_color_t color = sky_entry->color;
    if (color == current_g_color) return;

    current_g_color = color;
    const famidash_ground_shades_t *shades = &famidash_ground_table[idx];

    // Palette 1: Ground color
    famidash_bg_target[5] = shades->darker;
    famidash_bg_target[6] = color;

    // Palette 4: Ground Grid
    famidash_bg_target[16] = RGB(31, 31, 31);
    famidash_bg_target[17] = color;
    famidash_bg_target[18] = shades->grid_18;
    famidash_bg_target[19] = shades->grid_9;

    fade_restart = 1;
}

void famidash_reset_bg_palettes(uint8_t idx) BANKED {
    uint8_t i;
    uint8_t bg_col = 17;
    uint8_t g_col = 17;
    if (idx < MAX_LEVELS && game_levels[idx]) {
        bg_col = game_levels[idx]->bg_color;
        g_col = game_levels[idx]->g_color;
    }
    for (i = 0; i < 20; i++) famidash_bg_target[i] = vibrant_palette_default[i];
    current_sky_color = 0xFFFF;
    current_g_color = 0xFFFF;
    famidash_apply_bg_trigger(bg_col);
    famidash_apply_g_trigger(g_col);
    famidash_bg_set_now(famidash_bg_target);   // the level's colours from the start, no fade
    if (_cpu == CGB_TYPE) {
        // through the fade module: it fades what it knows (it only dimmed the sprites leaving a
        // level, and the level popped in at full brightness after the fade in)
        fade_set_bkg_palette(0, 5, famidash_bg_palettes);
        fade_set_bkg_palette(5, 1, &shadow_bkg_palettes[20]);
        fade_set_bkg_palette(6, 2, &shadow_bkg_palettes[24]);   // the fading objects' palettes
    }
    famidash_bkg_palettes_dirty = 0;
}
