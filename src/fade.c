#pragma bank 221

#include "fade.h"
#include "bg_parallax.h"
#include "famidash_bg.h"
#include "collision.h"

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

// Set while a menu loads with the display on and the screen black: fade_set_* only store the
// palettes (fade_from_black shows them), so nothing shows before the fade in. fade_from_black
// clears it.
uint8_t fade_hold = 0;

static uint8_t shadow_bgp = 0xE4;
static uint8_t shadow_obp0 = 0xE4;
static uint8_t shadow_obp1 = 0xE4;

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

void fade_apply_pause_box_palettes(void) BANKED {
    if (_cpu == CGB_TYPE) {
        // All eight palettes are in use in a level, so the whole screen dims, as on DMG
        uint8_t i;
        for (i = 0; i < 32; i++) {
            step_bkg[i] = dim_color(shadow_bkg_palettes[i], 1);
        }
        set_bkg_palette(0, 8, step_bkg);
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

// ---- Mirror portal transition (CGB levels with the seamless switch, gameplay.c) ----
// Closing: the map shown is covered with sky from both edges of the screen to its middle, a tile
// column at a time (the VBlank handler writes them: mirror_fx_vbl), and the sprites outside the
// opening are hidden. Then gameplay switches to the mirrored map, with every colour set to the
// sky's, and they fade to their own in 16 frames.
// No variables of its own but the state: the closing uses step_bkg, the fade both step buffers
// (the pause menu, the other user of step_bkg, does not open meanwhile).
uint8_t mirror_fx_state;
typedef struct {
    uint8_t rows[32];       // first parallax tile of each tile row of the map (column 0)
    uint8_t x[3];           // tile columns for the VBlank handler (bg_fx_pending & 3 of them)
    uint8_t s;              // first tile column on screen (+64, and so on: no wrap in the sums)
    uint8_t open_l, open_r; // the opening: [open_l, open_r)
    uint8_t cov_l, cov_r;   // covered so far: [cov_l, open_l) and [open_r, cov_r)
    uint8_t band;           // mt_band of rows
} curtain_t;
#define CU (*(curtain_t *)step_bkg)
// the fade's own, in the place of the sprite palette it leaves alone (the HUD's)
#define DZ_F (((uint8_t *)step_spr)[56])    // frames so far
#define DZ_T (((uint8_t *)step_spr)[57])    // eighths of the step being worked out
#define DZ_SKY (step_spr[29])               // the sky's share of it

// A sky column: tile column x (A) of the map shown, all 32 rows
static void cu_col(uint8_t x) __naked {
    (void)x;
    __asm
        ld      e, a
        and     a, #7
        ld      b, a                    ; b = x & 7: the parallax tile column
        ldh     a, (_LCDC_REG + 0)
        and     a, #0x08
        rrca
        or      a, #0x98
        ld      d, a                    ; de = the map shown + x
        push    de
        xor     a, a
        ldh     (_VBK_REG + 0), a
        ld      hl, #_step_bkg          ; CU.rows
        ld      c, #32
        .rept 8
        ld      a, (hl+)
        add     a, b
        ld      (de), a
        ld      a, e
        add     a, c
        ld      e, a
        .endm
        inc     d
        .rept 8
        ld      a, (hl+)
        add     a, b
        ld      (de), a
        ld      a, e
        add     a, c
        ld      e, a
        .endm
        inc     d
        .rept 8
        ld      a, (hl+)
        add     a, b
        ld      (de), a
        ld      a, e
        add     a, c
        ld      e, a
        .endm
        inc     d
        .rept 8
        ld      a, (hl+)
        add     a, b
        ld      (de), a
        ld      a, e
        add     a, c
        ld      e, a
        .endm
        pop     hl
        ld      a, #1
        ldh     (_VBK_REG + 0), a
        ld      bc, #32
        ld      a, #0x0B                ; VRAM bank 1, palette 3: the sky
        .rept 32
        ld      (hl), a
        add     hl, bc
        .endm
        ret
    __endasm;
}

static void fx_pal_bkg(void) __naked {
    __asm
        ld      hl, #_step_bkg
        ld      a, #0x80
        ldh     (0x68), a
        ld      c, #0x69
        ld      b, #8
    1$:
        .rept 8
        ld      a, (hl+)
        ldh     (c), a
        .endm
        dec     b
        jr      NZ, 1$
        ret
    __endasm;
}

static void fx_pal_spr(void) __naked {
    __asm
        ld      hl, #_step_spr
        ld      a, #0x80
        ldh     (0x6A), a
        ld      c, #0x6B
        ld      b, #7
    1$:
        .rept 8
        ld      a, (hl+)
        ldh     (c), a
        .endm
        dec     b
        jr      NZ, 1$
        ret
    __endasm;
}

// The VBlank handler (this bank switched in): what still fits in VBlank, the rest next time
BANKREF(mirror_fx_vbl)
void mirror_fx_vbl(void) {
    uint8_t p = bg_fx_pending;
    while (p & 3u) {
        uint8_t ly = LY_REG;
        if (ly < 144u || ly > 151u) break;      // (a column: ~2 lines)
        cu_col(CU.x[(uint8_t)((p & 3u) - 1u)]);
        p--;
    }
    if (p & 0x40u) {
        uint8_t ly = LY_REG;
        if (ly >= 144u && ly <= 151u) { fx_pal_bkg(); p &= (uint8_t)~0x40u; }
    }
    if (p & 0x80u) {
        uint8_t ly = LY_REG;
        if (ly >= 144u && ly <= 151u) { fx_pal_spr(); p &= (uint8_t)~0x80u; }
    }
    bg_fx_pending = p;
}

// Hides the sprites whose middle is outside the opening: (x - off) >= w. Not the HUD's (palette 7).
static void cu_clip(uint8_t off, uint8_t w) __naked {
    (void)off; (void)w;
    __asm
        ld      d, a                    ; d = off, e = w
        ld      hl, #_shadow_OAM
        ld      b, #40
    1$:
        inc     hl
        ld      a, (hl)                 ; x
        sub     a, d
        cp      a, e
        jr      C, 2$
        inc     hl
        inc     hl
        ld      a, (hl-)                ; attributes
        dec     hl
        and     a, #7
        cp      a, #7
        jr      Z, 2$
        dec     hl
        ld      (hl), #0
        inc     hl
    2$:
        inc     hl
        inc     hl
        inc     hl
        dec     b
        jr      NZ, 1$
        ret
    __endasm;
}

// The parallax pattern repeats every 3 map rows (mt_renderer.c, row_ty0): 16 tiles a map row
static void cu_rows(void) {
    uint8_t m = mt_band;
    uint8_t b = (uint8_t)((uint8_t)((uint8_t)(m + 1u) % 3u) << 4);
    for (uint8_t k = 0; k < 16u; k++) {
        uint8_t r = (uint8_t)(((uint8_t)(m + k) & 15u) << 1);
        CU.rows[r] = b;
        CU.rows[(uint8_t)(r + 1u)] = (uint8_t)(b + 8u);
        b += 16u;
        if (b == 48u) b = 0;
    }
    CU.band = m;
}

static void cu_frame(uint8_t scx) {
    uint8_t s = CU.s;
    uint8_t d = (uint8_t)((uint8_t)(scx >> 3) - s) & 31u;
    if (d & 16u) d |= 0xE0u;
    s += d;
    CU.s = s;
    if (CU.band != mt_band) cu_rows();
    // columns that left the screen need no cover
    if ((int8_t)(CU.open_l - s) < 0) CU.open_l = s;
    if ((int8_t)(CU.open_r - (uint8_t)(s + 21u)) > 0) CU.open_r = (uint8_t)(s + 21u);
    if ((int8_t)(CU.open_r - CU.open_l) < 0) CU.open_r = CU.open_l;
    if (!(bg_fx_pending & 3u)) {
        uint8_t n = 0;
        // the columns the scroll brings in first, a column before they show
        while (n < 3u && (int8_t)(CU.cov_r - (uint8_t)(s + 22u)) < 0) CU.x[n++] = (uint8_t)(CU.cov_r++ & 31u);
        while (n < 3u && (int8_t)(CU.cov_l - (uint8_t)(s - 1u)) > 0) CU.x[n++] = (uint8_t)(--CU.cov_l & 31u);
        // then the side of the opening that is further from the middle of the screen
        while (n < 3u && CU.open_l != CU.open_r) {
            if ((uint8_t)(CU.open_l - s) <= (uint8_t)(s + 21u - CU.open_r)) CU.x[n++] = (uint8_t)(CU.open_l++ & 31u);
            else CU.x[n++] = (uint8_t)(--CU.open_r & 31u);
        }
        if (n) bg_fx_pending = n;
        else if (CU.open_l == CU.open_r) mirror_fx_state = MIRROR_FX_CLOSED;
    }
    cu_clip((uint8_t)((uint8_t)((uint8_t)(CU.open_l - s) << 3) - (scx & 7u) + 4u),
            (uint8_t)((uint8_t)(CU.open_r - CU.open_l) << 3));
}

// A row the row job is about to write (bg_rj_*): sky outside the opening
void mirror_fx_row(uint8_t maprow) BANKED {
    uint8_t b = (uint8_t)((uint8_t)((uint8_t)(maprow + 1u) % 3u) << 4);
    uint8_t w = (uint8_t)(CU.open_r - CU.open_l);
    uint8_t x = bg_rj_x;
    for (uint8_t i = 0; i < 2u * BG_RJ_SLOTS; i++, x++) {
        if (((uint8_t)(x - CU.open_l) & 31u) >= w) {
            uint8_t t = (uint8_t)(b + (x & 7u));
            bg_rj_tiles[i] = t;
            bg_rj_tiles[(uint8_t)(i + 2u * BG_RJ_SLOTS)] = (uint8_t)(t + 8u);
            bg_rj_attrs[i] = 0x0B;
            bg_rj_attrs[(uint8_t)(i + 2u * BG_RJ_SLOTS)] = 0x0B;
        }
    }
    bg_rj_pending = 1;   // (written while this ran: once more)
}

// k eighths of a colour, all three channels at once
static uint16_t dz_part(uint16_t c, uint8_t k) {
    uint16_t m = 0;
    if (k & 8u) return c;
    if (k & 4u) m = (c >> 1) & 0x3DEFu;
    if (k & 2u) m += (c >> 2) & 0x1CE7u;
    if (k & 1u) m += (c >> 3) & 0x0C63u;
    return m;
}
static void dz_mix(palette_color_t *dst, const palette_color_t *src, uint8_t n) {
    uint8_t t = DZ_T;
    uint16_t sky = DZ_SKY;
    do { *dst++ = (uint16_t)(sky + dz_part(*src++, t)); } while (--n);
}
// The palettes at t eighths between the sky's colour and their own, into the step buffers
static void dz_bkg(uint8_t t) {
    DZ_T = t;
    DZ_SKY = dz_part(famidash_bg_palettes[12], (uint8_t)(8u - t));
    dz_mix(step_bkg, famidash_bg_palettes, 12);
    step_bkg[12] = famidash_bg_palettes[12]; step_bkg[13] = famidash_bg_palettes[13];
    step_bkg[14] = famidash_bg_palettes[14]; step_bkg[15] = famidash_bg_palettes[15];
    dz_mix(step_bkg + 16, famidash_bg_palettes + 16, 4);
    dz_mix(step_bkg + 20, famidash_bg_palettes, 2);      // palette 5 (upload_palette)
    step_bkg[22] = step_bkg[23] = DZ_SKY;
    dz_mix(step_bkg + 24, shadow_bkg_palettes + 24, 8);
}
static void dz_spr(uint8_t t) {
    DZ_T = t;
    DZ_SKY = dz_part(famidash_bg_palettes[12], (uint8_t)(8u - t));
    dz_mix(step_spr, shadow_spr_palettes, 28);
}

// Gameplay switched to the mirrored map: it shows with every colour at the sky's
void mirror_fx_open(void) BANKED {
    dz_bkg(0);
    dz_spr(0);
    DZ_F = 0;
    mirror_fx_state = MIRROR_FX_OPEN;
    bg_fx_pending = 0xC0u;   // (a column still waiting was for the map going out)
}

// Once a frame while a mirror portal's change of view is due or under way. scx: the scroll of
// the frame being prepared.
void mirror_fx_frame(uint8_t scx) BANKED {
    if (mirror_fx_state == MIRROR_FX_OPEN) {
        uint8_t f = DZ_F;
        uint8_t t = (uint8_t)((f >> 1) + 1u);
        if (f & 1u) { dz_spr(t); bg_fx_pending |= 0x80u; }
        else { dz_bkg(t); bg_fx_pending |= 0x40u; }
        DZ_F = ++f;
        if (f == 16u) mirror_fx_state = MIRROR_FX_IDLE;
        return;
    }
    if (mirror_fx_state == MIRROR_FX_IDLE) {
        uint8_t s = (uint8_t)((scx >> 3) + 64u);
        CU.s = s;
        CU.open_l = CU.cov_l = s;
        CU.open_r = CU.cov_r = (uint8_t)(s + 21u);
        cu_rows();
        mirror_fx_state = MIRROR_FX_CLOSING;
    }
    cu_frame(scx);
}

// Death, reload: no transition, the palettes as they are meant
void mirror_fx_cancel(void) BANKED {
    if (!mirror_fx_state) return;
    bg_fx_pending = 0;
    if (mirror_fx_state == MIRROR_FX_OPEN) {
        set_sprite_palette(0, 7, shadow_spr_palettes);
        bg_pal_request = 1;
    }
    mirror_fx_state = MIRROR_FX_IDLE;
}
