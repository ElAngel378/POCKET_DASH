#pragma bank 15

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
// set famidash_bg_target and the screen fades to it in the trigger's fade time (BG_FADE_FRAMES,
// 0.3 s, unless a fade time marker sits in its column: tools/tmx2sprites.py): every colour
// channel moves by a fixed 8.8 step, computed once per trigger.
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
uint8_t famidash_fade_left;                  // steps to go, 0: not fading (DMG: band steps)
static uint8_t fade_div, fade_wait; // frames a step (10 s: 5, else 1), frames to the next step
static uint8_t fade_restart;        // a trigger changed famidash_bg_target this frame
static uint8_t fade_time;           // that trigger's fade time (FAMIDASH_FADE_*)

// 8.8 step for a channel change of d = index - 31: d * 256 / frames, for 18 frames (~= d * 57 / 4)
// and 30. 1, 2 and 4 s halve the 30 frame step (one table each did not fit the bank).
// (FAMIDASH_FADE_INSTANT: 1 frame, the last one copies the target, the steps are never seen)
// 10 s: the 2 s steps, one every 5 frames (600 frames do not fit famidash_fade_left, and a 600 frame step
// rounds small changes to nothing)
static const uint8_t bg_fade_frames[FAMIDASH_FADE_10S + 1] = { BG_FADE_FRAMES, 1, 30, 60, 120, 240, 120 };
static const uint8_t step_tab_lo[2][63] = {
    { 0x46, 0x54, 0x62, 0x71, 0x7F, 0x8D, 0x9B, 0xAA, 0xB8, 0xC6, 0xD4, 0xE3, 0xF1, 0xFF, 0x0D, 0x1C, 0x2A, 0x38, 0x46, 0x55, 0x63, 0x71, 0x7F, 0x8E, 0x9C, 0xAA, 0xB8, 0xC7, 0xD5, 0xE3, 0xF1, 0x00, 0x0E, 0x1C, 0x2A, 0x39, 0x47, 0x55, 0x63, 0x72, 0x80, 0x8E, 0x9C, 0xAB, 0xB9, 0xC7, 0xD5, 0xE4, 0xF2, 0x00, 0x0E, 0x1D, 0x2B, 0x39, 0x47, 0x56, 0x64, 0x72, 0x80, 0x8F, 0x9D, 0xAB, 0xB9 },
    { 0xF7, 0x00, 0x09, 0x11, 0x1A, 0x22, 0x2B, 0x33, 0x3C, 0x44, 0x4D, 0x55, 0x5E, 0x66, 0x6F, 0x77, 0x80, 0x89, 0x91, 0x9A, 0xA2, 0xAB, 0xB3, 0xBC, 0xC4, 0xCD, 0xD5, 0xDE, 0xE6, 0xEF, 0xF7, 0x00, 0x09, 0x11, 0x1A, 0x22, 0x2B, 0x33, 0x3C, 0x44, 0x4D, 0x55, 0x5E, 0x66, 0x6F, 0x77, 0x80, 0x89, 0x91, 0x9A, 0xA2, 0xAB, 0xB3, 0xBC, 0xC4, 0xCD, 0xD5, 0xDE, 0xE6, 0xEF, 0xF7, 0x00, 0x09 },
};
static const uint8_t step_tab_hi[2][63] = {
    { 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
};
// Green channel g (0..31) in the two bytes of an RGB555 colour
static const uint8_t g_to_lo[32] = { 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0 };
static const uint8_t g_to_hi[32] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 };

static FadeCh *ch_p;
static const uint8_t *step_lo, *step_hi;   // the fade time's row of step_tab_lo / _hi
static uint8_t step_shift;                 // then halved this many times
// One channel of *ch_p from b to c, ch_p moved on (called from fade_scan). Assembly: 60 of these in C on the frame
// a trigger fires took over a third of a CGB frame and dropped it.
// st = the table's step for to + 31 - from, halved step_shift times towards 0: rounded down, a
// falling channel overshot past 0 in a 4 s fade and wrapped (0 -> 255: garbage colours)
static void setup_ch(void) __naked {
    __asm
        push    bc
        ld      a, c
        add     a, #31
        sub     a, b
        ld      e, a                    ; k
        ld      d, #0
        ld      hl, #_step_lo           ; c = step_lo[k]
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        add     hl, de
        ld      c, (hl)
        ld      hl, #_step_hi           ; b = step_hi[k]
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        add     hl, de
        ld      b, (hl)
        ld      a, (_step_shift)
        or      a, a
        jr      Z, 3$
        ld      e, a
        bit     7, b
        jr      Z, 2$
        xor     a, a                    ; negative: halve the size
        sub     a, c
        ld      c, a
        ld      a, #0
        sbc     a, b
        ld      b, a
    1$:
        sra     b
        rr      c
        dec     e
        jr      NZ, 1$
        xor     a, a
        sub     a, c
        ld      c, a
        ld      a, #0
        sbc     a, b
        ld      b, a
        jr      3$
    2$:
        sra     b
        rr      c
        dec     e
        jr      NZ, 2$
    3$:
        pop     de                      ; d = from
        ld      hl, #_ch_p
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      a, d
        ld      (hl+), a                ; val
        ld      a, #0x80                ; frac: rounds to the nearest step
        ld      (hl+), a
        ld      a, c
        ld      (hl+), a                ; step lo
        ld      a, b
        ld      (hl+), a                ; step hi
        ld      a, l
        ld      (_ch_p), a
        ld      a, h
        ld      (_ch_p + 1), a
        ret
    __endasm;
}

// Colours that differ between the screen and the target: index into fade_col, their 3 channels set up
// (fade_n = how many). In assembly: in C, this loop made the frame a trigger fires drop on CGB.
static void fade_scan(void) __naked {
    __asm
        ld      hl, #_famidash_bg_palettes
        ld      de, #_famidash_bg_target
        ld      bc, #0                  ; b = colour, c = n
    1$:
        ld      a, (de)
        cp      a, (hl)
        jr      NZ, 2$
        inc     de
        inc     hl
        ld      a, (de)
        cp      a, (hl)
        dec     de
        dec     hl
        jr      Z, 3$
    2$:
        push    de
        push    hl
        ld      hl, #_fade_col          ; fade_col[n++] = colour
        ld      e, c
        ld      d, #0
        add     hl, de
        ld      (hl), b
        inc     c
        pop     hl
        pop     de
        push    bc
        push    de
        push    hl
        ld      a, (hl)                 ; red: low byte & 31
        and     a, #31
        ld      b, a
        ld      a, (de)
        and     a, #31
        ld      c, a
        call    _setup_ch
        pop     hl
        pop     de
        push    de
        push    hl
        ld      a, (hl+)                ; green: (lo >> 5) | (hi << 3) & 31
        swap    a
        rrca
        and     a, #7
        ld      b, a
        ld      a, (hl)
        add     a, a
        add     a, a
        add     a, a
        or      a, b
        and     a, #31
        ld      b, a
        ld      a, (de)
        inc     de
        swap    a
        rrca
        and     a, #7
        ld      c, a
        ld      a, (de)
        add     a, a
        add     a, a
        add     a, a
        or      a, c
        and     a, #31
        ld      c, a
        call    _setup_ch
        pop     hl
        pop     de
        push    de
        push    hl
        inc     hl                      ; blue: hi >> 2 & 31
        inc     de
        ld      a, (hl)
        rrca
        rrca
        and     a, #31
        ld      b, a
        ld      a, (de)
        rrca
        rrca
        and     a, #31
        ld      c, a
        call    _setup_ch
        pop     hl
        pop     de
        pop     bc
    3$:
        inc     hl
        inc     hl
        inc     de
        inc     de
        inc     b
        ld      a, b
        cp      a, #20
        jr      NZ, 1$
        ld      a, c
        ld      (_fade_n), a
        ret
    __endasm;
}

// The fade starts from the colours on screen (a trigger during a fade continues smoothly)
static void start_fade(void) {
    uint8_t n;
    uint8_t row = (fade_time > FAMIDASH_FADE_INSTANT);
    step_lo = step_tab_lo[row];
    step_hi = step_tab_hi[row];
    step_shift = (fade_time == FAMIDASH_FADE_10S) ? 2 : row ? (uint8_t)(fade_time - FAMIDASH_FADE_HALF_S) : 0;
    fade_div = fade_wait = (fade_time == FAMIDASH_FADE_10S) ? 5 : 1;
    ch_p = fade_ch;
    fade_scan();
    n = fade_n;
    fade_nch = (uint8_t)(n * 3u);
    famidash_fade_left = n ? bg_fade_frames[fade_time] : 0;
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

// The cube's colour 2: colour 1 of palette 7 (decorations GDA draws in the player's colour 2)
palette_color_t famidash_bg_accent2 = RGB(0, 31, 31);

// shadow_bkg_palettes' palettes 5..7, from the level palettes (not stored there, see upload_palette):
// 5 (fake spikes, Famidash PAL_3): sky, darker sky, black, black
// 6 (GDA's background shades: 1.6 blocks, wave hills): sky, outline colour, sky at a quarter, 3/8 of
//   the way from the sky to the outline colour (GDA: 30..50 %, tools/gd_objects.py SHADES)
// 7 (the fading objects' faded look, bg_stream.c: colours 2, 3 of palette 0 most of the way to the
//   sky): sky, accent 2 (the decorations in the player's colour 2: tools/compose.py), 3/4 sky, and
//   the outline colour a quarter of the way from the sky
static void shadow_fake_spike_palette(void) {
    shadow_bkg_palettes[20] = shadow_bkg_palettes[24] = shadow_bkg_palettes[28] = famidash_bg_palettes[0];
    shadow_bkg_palettes[21] = famidash_bg_palettes[1];
    shadow_bkg_palettes[22] = shadow_bkg_palettes[23] = 0;
    shadow_bkg_palettes[25] = famidash_bg_palettes[3];
    shadow_bkg_palettes[29] = famidash_bg_accent2;
    const uint8_t *s = (const uint8_t *)famidash_bg_palettes;
    uint8_t sr = s[0] & 31u, sg = (uint8_t)((s[0] >> 5) | (s[1] << 3)) & 31u, sb = (s[1] >> 2) & 31u;
    uint8_t r = s[6] & 31u, g = (uint8_t)((s[6] >> 5) | (s[7] << 3)) & 31u, b = (s[7] >> 2) & 31u;
    uint8_t r3 = (uint8_t)(sr + sr + sr), g3 = (uint8_t)(sg + sg + sg), b3 = (uint8_t)(sb + sb + sb);
    shadow_bkg_palettes[26] = RGB((uint8_t)(sr >> 2), (uint8_t)(sg >> 2), (uint8_t)(sb >> 2));
    shadow_bkg_palettes[30] = RGB((uint8_t)(r3 >> 2), (uint8_t)(g3 >> 2), (uint8_t)(b3 >> 2));
    shadow_bkg_palettes[31] = RGB((uint8_t)((r3 + r) >> 2), (uint8_t)((g3 + g) >> 2), (uint8_t)((b3 + b) >> 2));
    r = (uint8_t)(r + r + r); g = (uint8_t)(g + g + g); b = (uint8_t)(b + b + b);
    shadow_bkg_palettes[27] = RGB((uint8_t)((r3 + sr + sr + r) >> 3), (uint8_t)((g3 + sg + sg + g) >> 3), (uint8_t)((b3 + sb + sb + b) >> 3));
}

// Once per frame (CGB): next step of a running fade
void famidash_bg_fade_step(void) BANKED {
    if (fade_restart) {
        // set up this frame, first step on the next one (spreads the work)
        fade_restart = 0;
        start_fade();
        return;
    }
    if (!famidash_fade_left) return;
    if (--fade_wait) return;
    fade_wait = fade_div;
    if (--famidash_fade_left == 0) {
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
        shadow_fake_spike_palette();   // (palette 7 follows the sky: it is uploaded from there)
    }
    famidash_bkg_palettes_dirty = 1;
}

// Shows pal (20 colours) right away, no fade (practice checkpoint restore)
void famidash_bg_set_now(const palette_color_t *pal) BANKED {
    memcpy(famidash_bg_target, pal, sizeof(famidash_bg_target));
    memcpy(famidash_bg_palettes, pal, sizeof(famidash_bg_target));
    memcpy(shadow_bkg_palettes, pal, 20 * sizeof(palette_color_t));
    shadow_fake_spike_palette();
    famidash_fade_left = 0;
    fade_restart = 0;
}
static palette_color_t current_sky_color = RGB( 1,  9, 24);
// Colour 3 of palette 2 (the background decorations, BG spikes): the cube's colour 1, like
// Famidash's player colour in its background palette
static palette_color_t bg_accent = RGB(15, 31, 0);

void famidash_set_bg_accent(palette_color_t c, palette_color_t c2) BANKED {
    famidash_bg_accent2 = c2;
    shadow_bkg_palettes[29] = c2;
    bg_accent = c;
    famidash_bg_target[11] = c;
    famidash_bg_palettes[11] = c;
    shadow_bkg_palettes[11] = c;
    famidash_bkg_palettes_dirty = 1;
}
static palette_color_t current_g_color = RGB( 9, 25,  4);

uint8_t famidash_bkg_palettes_dirty = 0;

#include "famidash_bg_tables.h"   // Famidash's NES colours: the block outline triggers
#include "level_colours.h"
#include "gameplay.h"             // selected: the level being played
#include "settings.h"

// The shades of a level colour as fractions of it (x / 256, like GDA's fades to black;
// tools/level_colours.py SHADES), per 5-bit channel, rounded like Famidash's NES tables were.
// Tables, not multiplications: a colour trigger costs about what the NES table lookups did
#define SH_DARKER       0
#define SH_BORDER       1
#define SH_BODY         2
#define SH_SHADOW       3
#define SH_OLD_BORDER   4
#define SH_OLD_BODY     5
#define SH_OLD_SHADOW   6
#define SH_GRID_18      7
#define SH_GRID_9       8
#define SH_GRID_9_DARK  9
static const uint8_t shade_tab[10][32] = {
    { 0, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 16, 17, 17, 18, 19, 20, 20, 21, 22, 23, 23 },   // SH_DARKER: x 192 / 256
    { 0, 1, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 8, 9, 10, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 17, 18, 19, 19, 20, 21, 21 },   // SH_BORDER: x 177 / 256
    { 0, 1, 2, 3, 3, 4, 5, 6, 7, 8, 8, 9, 10, 11, 12, 13, 13, 14, 15, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 24, 25, 26 },   // SH_BODY: x 215 / 256
    { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16 },   // SH_SHADOW: x 136 / 256
    { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 14, 15, 15, 16, 16 },   // SH_OLD_BORDER: x 133 / 256
    { 0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 9, 10, 10, 11, 12, 12, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 20, 21 },   // SH_OLD_BODY: x 174 / 256
    { 0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 6, 7, 7, 8, 8, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12, 12 },   // SH_OLD_SHADOW: x 102 / 256
    { 0, 1, 1, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 9, 9, 10, 11, 11, 12, 12, 13, 14, 14, 15, 16, 16, 17, 17, 18, 19, 19 },   // SH_GRID_18: x 159 / 256
    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5 },   // SH_GRID_9: x 41 / 256
    { 0, 0, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11 },   // SH_GRID_9_DARK: x 92 / 256
};

static palette_color_t shade(palette_color_t c, uint8_t k) {
    const uint8_t *t = shade_tab[k];
    uint8_t lo = (uint8_t)c, hi = (uint8_t)(c >> 8);
    uint8_t r = t[lo & 31u];
    uint8_t g = t[(uint8_t)((lo >> 5) | ((hi & 3u) << 3))];
    uint8_t b = t[(uint8_t)((hi >> 2) & 31u)];
    return (palette_color_t)(r | ((uint16_t)g << 5) | ((uint16_t)b << 10));
}

static const level_colour_t *level_colour(uint8_t color_id) {
    return &level_colours[selected][color_id];
}

// The sky colour and its darker shade of a background colour id (colours 0 and 1 of palette 0 after
// famidash_apply_bg_trigger), for the SGB palette
void famidash_sky_colors(uint8_t color_id, palette_color_t *out) BANKED {
    palette_color_t c = level_colour(color_id)->color;
    out[0] = c;
    out[1] = shade(c, SH_DARKER);
}

// The DMG theme (bg_pals index) of a background colour id: from its nearest NES colour
static uint8_t colour_band(uint8_t color_id) {
    uint8_t c = level_colour(color_id)->nes;
    return (c < 16u) ? ((c == 15u) ? 3u : 2u) : ((c < 32u) ? 1u : 0u);
}

uint8_t famidash_colour_band(uint8_t color_id) BANKED {
    return colour_band(color_id);
}

// DMG (not SGB): the theme goes to a background trigger's band over the trigger's fade time, one
// band at a time, each when the fade is halfway between two bands (about where CGB's colour
// crosses): a 10 s fade to black stays coloured a while (xStep's ship part) instead of going
// black at once. It uses the CGB fade's variables, idle on DMG: famidash_fade_left the steps to
// go, fade_n the band it goes to, fade_wait / fade_div 4-frame ticks to the next step / a step,
// fade_nch the frames of a tick.
static const uint8_t dmg_fade_ticks[FAMIDASH_FADE_10S + 1] = { 5, 0, 8, 15, 30, 60, 150 };

void famidash_dmg_band_trigger(uint8_t color_id, uint8_t time, uint8_t *idx) BANKED {
    uint8_t to = colour_band(color_id);
    uint8_t from = *idx;
    uint8_t steps = (to > from) ? (uint8_t)(to - from) : (uint8_t)(from - to);
    uint8_t tick = (uint8_t)(dmg_fade_ticks[(time <= FAMIDASH_FADE_10S) ? time : 0] / (uint8_t)(steps ? steps : 1u));
    famidash_fade_left = 0;
    if (!steps) return;
    if (tick < 2u) { *idx = to; return; }   // instant (or too short to show the bands between)
    fade_n = to;
    fade_div = tick;
    fade_wait = (uint8_t)(tick >> 1);
    fade_nch = 0;
    famidash_fade_left = steps;
}

void famidash_dmg_band_step(uint8_t *idx) BANKED {
    if (++fade_nch & 3u) return;
    if (--fade_wait) return;
    fade_wait = fade_div;
    if (fade_n > *idx) ++*idx; else --*idx;
    --famidash_fade_left;
}

void famidash_apply_bg_trigger(uint8_t color_id, uint8_t time) BANKED {
    palette_color_t c = level_colour(color_id)->color;
    if (c == current_sky_color) return;

    current_sky_color = c;
    palette_color_t darker = shade(c, SH_DARKER);

    // Palette 0: Normal level tiles
    famidash_bg_target[0] = c;
    famidash_bg_target[1] = darker;
    famidash_bg_target[2] = RGB(0, 0, 0);

    // Palette 1: Accent / Ground (preserve ground color in 5 and 6)
    famidash_bg_target[4] = c;

    // Palette 2: BG Spikes / Hazards
    famidash_bg_target[8] = c;
    famidash_bg_target[9] = darker;
    famidash_bg_target[10] = RGB(0, 0, 0);
    famidash_bg_target[11] = bg_accent;

    // Palette 3: Parallax BG. BG SHADES: the old ones (more contrast) or GDA's (brighter)
    famidash_bg_target[12] = c;
    uint8_t k = setting_bg_shades ? SH_BORDER : SH_OLD_BORDER;   // border, body, shadow in a row
    famidash_bg_target[13] = shade(c, k);
    famidash_bg_target[14] = shade(c, (uint8_t)(k + 1u));
    famidash_bg_target[15] = shade(c, (uint8_t)(k + 2u));

    // Palette 4: Ground Grid (Color 0 is the top white line, always pure white)
    famidash_bg_target[16] = RGB(31, 31, 31);

    fade_time = (time <= FAMIDASH_FADE_10S) ? time : FAMIDASH_FADE_DEFAULT;
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
    shadow_fake_spike_palette();
    famidash_bkg_palettes_dirty = 1;
}

void famidash_apply_g_trigger(uint8_t color_id, uint8_t time) BANKED {
    const level_colour_t *e = level_colour(color_id);
    palette_color_t color = e->color;
    if (color == current_g_color) return;

    current_g_color = color;

    // Palette 1: Ground color
    famidash_bg_target[5] = shade(color, SH_DARKER);
    famidash_bg_target[6] = color;

    // Palette 4: Ground Grid (dark colours: a lighter darkest shade, still visible)
    famidash_bg_target[16] = RGB(31, 31, 31);
    famidash_bg_target[17] = color;
    famidash_bg_target[18] = shade(color, SH_GRID_18);
    famidash_bg_target[19] = shade(color, (e->nes < 16u) ? SH_GRID_9_DARK : SH_GRID_9);

    fade_time = (time <= FAMIDASH_FADE_10S) ? time : FAMIDASH_FADE_DEFAULT;
    fade_restart = 1;
}

void famidash_reset_bg_palettes(uint8_t idx) BANKED {
    uint8_t i;
    for (i = 0; i < 20; i++) famidash_bg_target[i] = vibrant_palette_default[i];
    current_sky_color = 0xFFFF;
    current_g_color = 0xFFFF;
    // (start colours: positions in the level's colour table, which the triggers read by `selected`)
    if (idx < MAX_LEVELS && game_levels[idx]) {
        famidash_apply_bg_trigger(game_levels[idx]->bg_color, FAMIDASH_FADE_DEFAULT);
        famidash_apply_g_trigger(game_levels[idx]->g_color, FAMIDASH_FADE_DEFAULT);
    }
    famidash_bg_set_now(famidash_bg_target);   // the level's colours from the start, no fade
    if (_cpu == CGB_TYPE) {
        // through the fade module: it fades what it knows (it only dimmed the sprites leaving a
        // level, and the level popped in at full brightness after the fade in)
        fade_set_bkg_palette(0, 5, famidash_bg_palettes);
        fade_set_bkg_palette(5, 3, &shadow_bkg_palettes[20]);
    }
    famidash_bkg_palettes_dirty = 0;
}
