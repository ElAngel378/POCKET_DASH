#pragma bank 70
// The title menu icon on DMG: which of its pixels are behind the big buttons (state_menu.c). In the
// bank of its table (tools/gen_title_buttons.py: the button pixels pre-shifted for the 8 pixel
// offsets, so a tile row's mask is a single read). The menu frame on DMG has little time to spare
// (the side buttons' HBlank copies), so the row loop is assembly: SDCC's took ~120 cycles a row.

#include <gb/gb.h>
#include <stdint.h>
#include <string.h>
#include "title_buttons.h"
#include "menu_icon_cut.h"

#define MASK_END (TB_DMG_MASK_Y0 + TB_DMG_MASK_ROWS)
#define PLANE (TB_DMG_MASK_ROWS * 21u)
// (no multiplications: a 16-bit one is a library call)
static const uint16_t plane_of[8] = { 0, PLANE, 2 * PLANE, 3 * PLANE, 4 * PLANE, 5 * PLANE, 6 * PLANE, 7 * PLANE };

static const uint8_t *cut_src;   // the first row's mask in tb_dmg_cut (rows 21 bytes apart)
static uint8_t *cut_dst;         // where it goes; the next rows after it (cut_back: before it)
static uint8_t cut_n;            // rows, >= 1
static uint8_t cut_back;         // flipped vertically: the rows go backwards
static uint8_t cut_rev;          // flipped across: each mask bit reversed (bit_rev.c, 0x3F00)

// Returns non-zero if any row had pixels. Rows with none are left as they are (0). A loop for
// each of the 4 flip cases (no flag tests a row).
static uint8_t cut_rows(void) __naked {
    __asm
        ld      hl, #_cut_dst
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)                 ; de = dst
        ld      a, (_cut_n)
        ld      b, a                    ; b = rows
        ld      c, #0                   ; c = any
        ld      a, (_cut_back)
        or      a, a
        jr      NZ, 20$
        ld      a, (_cut_rev)
        or      a, a
        ld      hl, #_cut_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a                    ; hl = mask
        jr      NZ, 15$
    ; forward
    11$:
        ld      a, (hl)
        or      a, a
        jr      Z, 12$
        ld      c, a
        ld      (de), a
    12$:
        inc     de
        ld      a, l
        add     a, #21
        ld      l, a
        jr      NC, 13$
        inc     h
    13$:
        dec     b
        jr      NZ, 11$
        ld      a, c
        ret
    ; forward, bits reversed
    15$:
        ld      a, (hl)
        or      a, a
        jr      Z, 16$
        ld      c, a
        push    hl
        ld      h, #0x3F
        ld      l, a
        ld      a, (hl)
        pop     hl
        ld      (de), a
    16$:
        inc     de
        ld      a, l
        add     a, #21
        ld      l, a
        jr      NC, 17$
        inc     h
    17$:
        dec     b
        jr      NZ, 15$
        ld      a, c
        ret
    20$:
        ld      a, (_cut_rev)
        or      a, a
        ld      hl, #_cut_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        jr      NZ, 25$
    ; backwards
    21$:
        ld      a, (hl)
        or      a, a
        jr      Z, 22$
        ld      c, a
        ld      (de), a
    22$:
        dec     de
        ld      a, l
        add     a, #21
        ld      l, a
        jr      NC, 23$
        inc     h
    23$:
        dec     b
        jr      NZ, 21$
        ld      a, c
        ret
    ; backwards, bits reversed
    25$:
        ld      a, (hl)
        or      a, a
        jr      Z, 26$
        ld      c, a
        push    hl
        ld      h, #0x3F
        ld      l, a
        ld      a, (hl)
        pop     hl
        ld      (de), a
    26$:
        dec     de
        ld      a, l
        add     a, #21
        ld      l, a
        jr      NC, 27$
        inc     h
    27$:
        dec     b
        jr      NZ, 25$
        ld      a, c
        ret
    __endasm;
}

uint8_t menu_icon_cut(int16_t sx, uint8_t sy, uint8_t flags, uint8_t n, uint8_t *out) BANKED {
    memset(out, 0, 32);
    uint8_t h = (n == 4u) ? 16u : 8u;            // rows of a column
    uint8_t r0 = 0, r1 = h;                      // the ones with button pixels
    if (sy < TB_DMG_MASK_Y0) {
        if ((uint8_t)(sy + h) <= TB_DMG_MASK_Y0) return 0;
        r0 = (uint8_t)(TB_DMG_MASK_Y0 - sy);
    }
    if ((uint8_t)(sy + h) > MASK_END) {
        if (sy >= MASK_END) return 0;
        r1 = (uint8_t)(MASK_END - sy);
    }
    cut_n = (uint8_t)(r1 - r0);
    cut_rev = flags & S_FLIPX;
    cut_back = flags & S_FLIPY;
    uint16_t ry = (uint8_t)(sy + r0 - TB_DMG_MASK_Y0);
    ry = (ry << 4) + (ry << 2) + ry;             // * 21
    uint8_t cols = (n == 4u) ? 2u : 1u;
    uint8_t any = 0;
    for (uint8_t i = 0; i < cols; i++) {
        int16_t x = sx + (int16_t)(i << 3);
        if (x <= -8 || x >= 160) continue;
        uint8_t xs = (uint8_t)(x + 8);           // 1..167
        uint8_t c = (uint8_t)(xs >> 3);          // window column 0..20
        if (!tb_dmg_cut_cols[c]) continue;
        cut_src = tb_dmg_cut + plane_of[xs & 7u] + ry + c;
        // its 2 tiles (top, bottom) are 16 bytes in VRAM order; a big icon flipped (both ways)
        // shows the other column's, bottom up
        uint8_t col = (cols == 2u && flags) ? (uint8_t)(1u - i) : i;
        uint8_t *base = out + (uint8_t)(col << 4);
        cut_dst = cut_back ? base + (uint8_t)(h - 1u - r0) : base + r0;
        any |= cut_rows();
    }
    return any != 0;
}
