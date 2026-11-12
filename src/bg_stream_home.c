// Background tile streaming (src/graphics/bg_stream.c): the part that reads the level's records in
// their ROM bank, so in bank 0.
#include <gb/gb.h>
#include <stdint.h>
#include "bg_stream.h"

uint8_t bsu_n, bsu_bank, bsu_skip, bsu_m;
static uint8_t bsu_slot;
const uint8_t *bsu_src;
uint8_t bsu_out[BSU_MAX * 3];

// Applies bsu_n records (at most BSU_MAX) at bsu_src in bank bsu_bank. A record: slot, tile (16
// bits; 0xFFFF: none), k, k x (metatile, quarter). The quarters are pointed at the slot in the
// normal and in the mirrored table (there the left and right quarters are swapped), unless
// bsu_skip (the tables are the level's already). The tiles to load come back in bsu_out (slot,
// tile lo, hi), bsu_m of them, and bsu_src is where the records end.
// In C, a byte at a time through a buffer, a column with a dozen records took half a DMG frame.
void bs_apply(void) NONBANKED __naked {
    __asm
        ld      a, (__current_bank)
        push    af
        ld      a, (_bsu_bank)
        ld      (__current_bank), a
        ld      (0x2000), a
        xor     a, a
        ld      (_bsu_m), a
        ld      hl, #_bsu_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      de, #_bsu_out
        ld      a, (_bsu_n)
        ld      b, a
    1$:
        ld      a, (hl+)
        ld      (_bsu_slot), a
        ld      a, (hl+)
        ld      c, a                    ; tile lo
        ld      a, (hl+)                ; tile hi
        and     a, c
        inc     a
        jr      Z, 2$                   ; 0xFFFF: no tile to load
        dec     hl
        ld      a, (_bsu_slot)
        ld      (de), a
        inc     de
        ld      a, c
        ld      (de), a
        inc     de
        ld      a, (hl+)
        ld      (de), a
        inc     de
        push    hl
        ld      hl, #_bsu_m
        inc     (hl)
        pop     hl
    2$:
        ld      a, (hl+)                ; k
        or      a, a
        jr      Z, 5$
        ld      c, a
        ld      a, (_bsu_skip)
        or      a, a
        jr      Z, 3$
        push    de
        ld      e, c
        ld      d, #0
        add     hl, de
        add     hl, de
        pop     de
        jr      5$
    3$:
        push    de
        push    bc
        ld      b, c
        ld      a, (_bsu_slot)
        ld      c, a
    4$:
        ld      a, (hl+)                ; metatile
        ld      e, a
        ld      a, (hl+)                ; quarter
        push    hl
        ld      l, e
        ld      h, #0
        add     hl, hl
        add     hl, hl
        or      a, l
        ld      l, a                    ; hl = metatile * 4 + quarter
        push    hl
        ld      de, #_mt_ram
        add     hl, de
        ld      (hl), c
        pop     hl
        ld      a, l
        xor     a, #1
        ld      l, a
        ld      de, #(_mt_ram + 1024)
        add     hl, de
        ld      (hl), c
        pop     hl
        dec     b
        jr      NZ, 4$
        pop     bc
        pop     de
    5$:
        dec     b
        jp      NZ, 1$
        ld      a, l
        ld      (_bsu_src), a
        ld      a, h
        ld      (_bsu_src + 1), a
        pop     af
        ld      (__current_bank), a
        ld      (0x2000), a
        ret
    __endasm;
}
