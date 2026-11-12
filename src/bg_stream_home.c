// Background tile streaming (src/graphics/bg_stream.c): the part that reads the level's records in
// their ROM bank, so in bank 0.
#include <gb/gb.h>
#include <stdint.h>
#include "bg_stream.h"

uint8_t bsu_n, bsu_bank, bsu_skip, bsu_m;
uint8_t bsu_copy;            // display off: copy the tiles to their slots here (bsu_base, bsu_tbank)
uint16_t bsu_base;           // the level's first tile in the tile space (+ its pool when mirrored)
uint8_t bsu_tbank;           // the tile space's first bank
static uint8_t bsu_slot;
const uint8_t *bsu_src;
uint8_t bsu_out[BSU_MAX * 3];

// Applies bsu_n records (at most BSU_MAX) at bsu_src in bank bsu_bank. A record: slot, tile (16
// bits; 0xFFFF: none), k, k x (metatile, quarter). The quarters are pointed at the slot in the
// normal and in the mirrored table (there the left and right quarters are swapped), unless
// bsu_skip (the tables are the level's already). The tiles to load are copied to their slots
// (bsu_copy: display off) or come back in bsu_out (slot, tile lo, hi), bsu_m of them; bsu_src is
// where the records end.
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
        ld      a, (_bsu_copy)
        or      a, a
        jr      Z, 6$
        call    7$
        jr      2$
    6$:
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

    7$:
        ; copy tile (c, hi at hl - 1) to slot bsu_slot: g = tile + bsu_base, from bank
        ; bsu_tbank + g / 1024, address 0x4000 + (g % 1024) * 16
        push    hl
        push    de
        push    bc
        dec     hl
        ld      a, (hl)
        ld      b, a                    ; bc = tile
        ld      hl, #_bsu_base
        ld      a, (hl+)
        add     a, c
        ld      c, a
        ld      a, (hl)
        adc     a, b
        ld      b, a                    ; bc = g
        rrca
        rrca
        and     a, #0x3F
        ld      hl, #_bsu_tbank
        add     a, (hl)
        ld      (__current_bank), a
        ld      (0x2000), a
        ld      a, b
        and     a, #3
        ld      h, a
        ld      l, c
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      a, h
        or      a, #0x40
        ld      h, a                    ; hl = source
        ld      a, (_bsu_slot)
        ld      e, a
        swap    a
        and     a, #0xF0
        ld      c, a
        ld      a, e
        swap    a
        and     a, #0x0F                ; slot >> 4
        bit     7, e
        jr      NZ, 8$
        or      a, #0x90                ; 0..127: 0x9000 + slot * 16
        jr      9$
    8$:
        or      a, #0x80                ; 128..255: 0x8000 + slot * 16
    9$:
        ld      d, a
        ld      e, c
        .rept 16
        ld      a, (hl+)
        ld      (de), a
        inc     de
        .endm
        ld      a, (_bsu_bank)
        ld      (__current_bank), a
        ld      (0x2000), a
        pop     bc
        pop     de
        pop     hl
        ret
    __endasm;
}

// Mirrors bsf_n tiles (16 bytes each) at bsf_dst in place, left to right (display off). The DMG
// mirror portal flips the tiles in VRAM instead of loading every tile again.
uint8_t *bsf_dst;
uint8_t bsf_n;
void bs_flip_tiles(void) NONBANKED __naked {
    __asm
        ld      hl, #_bsf_dst
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      a, (_bsf_n)
        ld      b, a
        ld      d, #0x3F                ; bit_rev
    1$:
        .rept 16
        ld      a, (hl)
        ld      e, a
        ld      a, (de)
        ld      (hl+), a
        .endm
        dec     b
        jr      NZ, 1$
        ret
    __endasm;
}
