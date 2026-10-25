#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "bg_parallax.h"
#include "famidash_bg.h"
#include "music.h"

volatile uint8_t bg_gdma_pending;
volatile uint8_t bg_gdma_phase;
volatile uint8_t bg_gdma_isr_on;
volatile uint8_t bg_vbl_on;
volatile uint8_t bg_vbl_seen;
volatile uint8_t bg_vbl_frames;   // VBlanks seen (wraps)
volatile uint8_t bg_pal_request;
volatile uint8_t bg_cj_pending;
uint8_t bg_cj_x, bg_cj_y;
const uint8_t *bg_cj_tiles;
const uint8_t *bg_cj_attrs;
volatile uint8_t bg_rj_pending;
uint8_t bg_rj_x, bg_rj_y;
uint8_t bg_rj_tiles[2 * 2 * BG_RJ_SLOTS];
uint8_t bg_rj_attrs[2 * 2 * BG_RJ_SLOTS];
volatile uint16_t bg_fj_rows;
uint8_t bg_fj_x;
volatile uint8_t bg_saw_pending;
uint8_t bg_saw_bank, bg_saw_blocks;
const uint8_t *bg_saw_src;
uint16_t bg_saw_dst;
volatile uint8_t bg_saw_dmg_n;
uint8_t * const *bg_saw_dmg_dsts;
volatile uint8_t bg_cube_pending;
uint8_t bg_cube_bank;
const uint8_t *bg_cube_src;
uint8_t *bg_cube_dst;
volatile uint8_t bg_scroll_pending;
volatile uint8_t bg_dmg_pal_pending;
uint8_t bg_dmg_bgp, bg_dmg_obp0, bg_dmg_obp1;
volatile uint8_t bg_lcdc_map;   // 0xFF: none, else LCDC bit 3 to set with the next scroll latch
volatile uint8_t bg_scx;
volatile uint8_t bg_scy;

static void parallax_gdma(uint8_t phase);
static void saw_gdma(void);

void set_bg_parallax_phase(uint8_t phase) {
    if (_cpu == CGB_TYPE) {
        bg_gdma_pending = 0;
        parallax_gdma(phase);
    }
}

void init_bg_parallax(void) {
    set_bg_parallax_phase(0);
}

// Hand-written copy loops for the VBlank handler (C compiles to ~3x slower code
// and the whole upload has to fit in what is left of VBlank).
static const uint8_t *up_src;
static uint8_t *up_dst;

// 8 rows of 2 bytes from up_src (contiguous) to up_dst, up_dst advancing by 32 per row.
static void copy_rows2(void) __naked {
    __asm
        ld      hl, #_up_src
        ld      a, (hl+)
        ld      e, a
        ld      a, (hl)
        ld      d, a
        ld      hl, #_up_dst
        ld      a, (hl+)
        ld      c, a
        ld      a, (hl)
        ld      h, a
        ld      l, c
        ld      bc, #31
        .rept 8
        ld      a, (de)
        inc     de
        ld      (hl+), a
        ld      a, (de)
        inc     de
        ld      (hl), a
        add     hl, bc
        .endm
        ret
    __endasm;
}

// 8 contiguous bytes from up_src to up_dst.
static void copy8(void) __naked {
    __asm
        ld      hl, #_up_src
        ld      a, (hl+)
        ld      e, a
        ld      a, (hl)
        ld      d, a
        ld      hl, #_up_dst
        ld      a, (hl+)
        ld      c, a
        ld      a, (hl)
        ld      h, a
        ld      l, c
        .rept 8
        ld      a, (de)
        inc     de
        ld      (hl+), a
        .endm
        ret
    __endasm;
}

// Row job upload: 16 bytes from up_src to up_dst, then the next 16 to up_dst + 32 (the two tile
// rows of a map row). Unrolled: as 4 copy8 calls it took ~4 scanlines and could run past VBlank,
// where the DMG drops VRAM writes (a stale tile when the camera climbs fast).
static void copy_row16x2(void) __naked {
    __asm
        ld      hl, #_up_src
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)
        ld      hl, #_up_dst
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        .rept 16
        ld      a, (de)
        inc     de
        ld      (hl+), a
        .endm
        ld      bc, #16
        add     hl, bc
        .rept 16
        ld      a, (de)
        inc     de
        ld      (hl+), a
        .endm
        ret
    __endasm;
}

// Fading cells (bg_fj_*): the rows of bg_fj_rows, from mj_out (mt_renderer.c) to map column
// bg_fj_x of the BG map shown (CGB: their attributes too) while LY is 144..152 (a cell takes well
// under a line), and leaves bg_fj_rows at the rows not written. No variables of its own: the
// stack is short of RAM.
static void fade_cells_copy(void) __naked {
    __asm
        ld      hl, #_bg_fj_rows
        ld      a, (hl+)
        ld      c, a
        ld      b, (hl)                 ; bc = rows left, bit 0: row d
        ld      d, #0
    1$:
        ld      a, b
        or      a, c
        jr      Z, 9$
        ldh     a, (_LY_REG + 0)
        cp      a, #144
        jr      C, 8$                   ; past VBlank: the rest next time
        cp      a, #153
        jr      NC, 8$
        srl     b
        rr      c
        jr      NC, 5$
        push    bc
        push    de
        ld      a, d
        add     a, a
        add     a, a
        add     a, #<_mj_out
        ld      l, a
        ld      a, #>_mj_out
        adc     a, #0
        ld      h, a                    ; hl = the 4 tiles of the row
        ld      a, d
        rrca
        rrca
        ld      d, a
        and     a, #0xC0
        ld      e, a
        ld      a, d
        and     a, #0x03
        ld      d, a                    ; de = 64r: tile row 2r
        ld      a, (_bg_fj_x)
        add     a, e
        ld      e, a                    ; (64r + x: no carry, x < 32)
        ldh     a, (_LCDC_REG + 0)
        and     a, #0x08                ; the map shown: 0x9800, or 0x9C00 when set
        rrca
        or      a, #0x98
        add     a, d
        ld      d, a                    ; de = map + 64r + x
        xor     a, a
        ldh     (_VBK_REG + 0), a
        push    de
        push    hl
        call    6$
        pop     hl
        pop     de
        ld      a, (_bg_gdma_isr_on)    ; CGB: the attributes too
        or      a, a
        jr      Z, 4$
        ld      a, #1
        ldh     (_VBK_REG + 0), a
        ld      bc, #64
        add     hl, bc                  ; the 4 attributes of the row
        call    6$
    4$:
        pop     de
        pop     bc
    5$:
        inc     d
        jr      1$
    6$:
        ; 2 bytes from hl to de, 2 more to de + 32
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        ld      a, e
        add     a, #31
        ld      e, a
        jr      NC, 7$
        inc     d
    7$:
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        ret
    8$:
        ; out of time: the rows left are bc << d
        ld      a, d
        or      a, a
        jr      Z, 9$
    10$:
        sla     c
        rl      b
        dec     a
        jr      NZ, 10$
    9$:
        ld      hl, #_bg_fj_rows
        ld      a, c
        ld      (hl+), a
        ld      (hl), b
        ret
    __endasm;
}

// Display off: sc_count tiles from sc_src (every other 16 bytes) to consecutive tile slots at sc_dst
static const uint8_t *sc_src;
static uint8_t *sc_dst;
static uint8_t sc_count;
static void copy_tiles_strided(void) __naked {
    __asm
        ld      hl, #_sc_dst
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)                 ; de = VRAM
        ld      hl, #_sc_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a                    ; hl = source
        ld      a, (_sc_count)
        ld      b, a
    1$:
        .rept 15
        ld      a, (hl+)
        ld      (de), a
        inc     e
        .endm
        ld      a, (hl+)
        ld      (de), a
        inc     de
        ld      a, l
        add     a, #16                  ; skip the other orientation
        ld      l, a
        jr      NC, 2$
        inc     h
    2$:
        dec     b
        jr      NZ, 1$
        ret
    __endasm;
}

// In bank 0: bg_extra_tiles is in its own bank
void bg_extra_copy(uint8_t dst, uint8_t count, const uint8_t *src) {
    uint8_t prev_b = _current_bank;
    SWITCH_ROM(BANK(bg_extra_tiles));
    if (!(LCDC_REG & LCDCF_ON)) {
        while (count) {
            // BG tiles 0..127 are at 0x9000, 128..255 at 0x8800: a run is cut where it crosses
            uint8_t n = count;
            if (dst < 128u && (uint8_t)(dst + n) > 128u) n = (uint8_t)(128u - dst);
            sc_src = src;
            sc_dst = (uint8_t *)(((dst < 128u) ? 0x9000u : 0x8000u) + ((uint16_t)dst << 4));
            sc_count = n;
            copy_tiles_strided();
            src += (uint16_t)n << 5;
            dst += n;
            count -= n;
        }
    } else {
        while (count--) {
            set_bkg_data(dst++, 1, src);
            src += 32;
        }
    }
    SWITCH_ROM(prev_b);
}

// Writes the 40 bytes of famidash_bg_palettes to CGB background palette RAM 0.., then palette 5
// (fake spikes, Famidash's PAL_3): colours 0 and 1 of palette 0 (sky, darker sky), black, black.
// Not stored: 8 more bytes in the palette, fade and checkpoint arrays overflowed the stack.
// Then palettes 6 and 7 (the fading objects, famidash_bg.c) from shadow_bkg_palettes.
static void upload_palette(void) __naked {
    __asm
        ld      hl, #_famidash_bg_palettes
        ld      a, #0x80
        ldh     (0x68), a
        ld      c, #0x69
        .rept 40
        ld      a, (hl+)
        ldh     (c), a
        .endm
        ld      hl, #_famidash_bg_palettes
        .rept 4
        ld      a, (hl+)
        ldh     (c), a
        .endm
        xor     a, a
        .rept 4
        ldh     (c), a
        .endm
        ld      hl, #(_shadow_bkg_palettes + 48)
        .rept 16
        ld      a, (hl+)
        ldh     (c), a
        .endm
        ret
    __endasm;
}

// DMG saw tiles: copies tiles from bg_saw_src (32 bytes apart: normal + mirrored) to
// bg_saw_dmg_dsts[i] (0: not loaded, skipped) while LY is 144..151 (a tile takes ~1 line), and
// leaves bg_saw_src / bg_saw_dmg_dsts / bg_saw_dmg_n at the first tile not copied. The C loop
// took ~1.1k dots per tile: 6 tiles were ~1/10 of every frame in levels with saws, and ran past
// VBlank (DMG drops VRAM writes outside it).
static void saw_dmg_copy(void) __naked {
    __asm
        ld      hl, #_bg_saw_src
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)                 ; de = source tile
        ld      a, (_bg_saw_dmg_n)
        ld      b, a                    ; b = tiles left
        ld      hl, #_bg_saw_dmg_dsts
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a                    ; hl = destination table entry
    1$:
        ldh     a, (_LY_REG + 0)
        cp      a, #144
        jr      C, 4$                   ; past VBlank: the rest next time
        cp      a, #152
        jr      NC, 4$
        ld      a, (hl+)
        ld      c, a
        ld      a, (hl+)
        push    hl
        ld      h, a
        ld      l, c                    ; hl = VRAM address
        or      a, c
        jr      Z, 2$                   ; tile not loaded in this level
        .rept 16
        ld      a, (de)
        inc     de
        ld      (hl+), a
        .endm
        ld      a, e                    ; de += 16: the mirrored copy
        add     a, #16
        ld      e, a
        jr      NC, 3$
        inc     d
        jr      3$
    2$:
        ld      a, e                    ; de += 32: next tile
        add     a, #32
        ld      e, a
        jr      NC, 3$
        inc     d
    3$:
        pop     hl
        dec     b
        jr      NZ, 1$
    4$:
        ld      a, b
        ld      (_bg_saw_dmg_n), a
        ld      a, e
        ld      (_bg_saw_src), a
        ld      a, d
        ld      (_bg_saw_src + 1), a
        ld      a, l
        ld      (_bg_saw_dmg_dsts), a
        ld      a, h
        ld      (_bg_saw_dmg_dsts + 1), a
        ret
    __endasm;
}

// DMG custom cube icon frame: copies tiles from bg_cube_src to bg_cube_dst while LY is 144..152,
// and leaves bg_cube_src / bg_cube_dst / bg_cube_pending (tiles left) at the first tile not copied.
// DMG VBlank (4560 dots): ~1080 go to GBDK's dispatch and OAM DMA before the handler, ~750 to its
// checks, a column upload ~1040; what is left (~1700) has to hold the whole image, or the spin
// skips images (a new one only every few frames). Copied with pop (SP on the source, 18 dots a
// byte instead of 24): a tile takes ~330 dots (0.72 line), so one started on line 152 still ends
// inside VBlank (line 153), and the image fits after a column (Mesen, release ROM).
// Interrupts off while SP points into ROM: a push there would write the MBC (the handler's reti
// turns them back on). Only called from the VBlank handler.
static uint16_t cube_sp_save;
static void cube_dmg_copy(void) __naked {
    __asm
        di
        ld      (_cube_sp_save), sp
        ld      hl, #_bg_cube_dst
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)                 ; de = VRAM tile
        ld      a, (_bg_cube_pending)
        ld      b, a                    ; b = tiles left
        ld      hl, #_bg_cube_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      sp, hl                  ; sp = source tile
        ld      h, d
        ld      l, e                    ; hl = VRAM tile
    1$:
        ldh     a, (_LY_REG + 0)
        cp      a, #144
        jr      C, 2$                   ; past VBlank: the rest next time
        cp      a, #153
        jr      NC, 2$
        .rept 8
        pop     de
        ld      a, e
        ld      (hl+), a
        ld      a, d
        ld      (hl+), a
        .endm
        dec     b
        jr      NZ, 1$
    2$:
        ld      (_bg_cube_src), sp
        ld      a, l
        ld      (_bg_cube_dst), a
        ld      a, h
        ld      (_bg_cube_dst + 1), a
        ld      a, b
        ld      (_bg_cube_pending), a
        ld      hl, #_cube_sp_save
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      sp, hl
        ret
    __endasm;
}

// Display off / VRAM-safe: 4 sprite tiles from ROM bank `bank` to sprite tiles first_tile..
// (home bank code, so it can switch to the data's bank)
void cube_tiles_load_now(const uint8_t *src, uint8_t bank, uint8_t first_tile) {
    uint8_t prev_b = _current_bank;
    SWITCH_ROM(bank);
    set_sprite_data(first_tile, 4, src);
    SWITCH_ROM(prev_b);
}

// Fading cells (bg_fj_*) as far as VBlank allows, never before a column slice still pending (it
// may be the same column)
#ifdef DEBUG_PROFILE
volatile uint8_t gp_fj_late_ly, gp_fj_late_cj, gp_fj_done;   // tools: fading cell jobs put off / done
#endif
static void fade_cells_try(void) {
    uint8_t ly = LY_REG;
    if (bg_fj_rows && !bg_cj_pending && ly >= 144u && ly <= 152u) {
        fade_cells_copy();
#ifdef DEBUG_PROFILE
        gp_fj_done++;
#endif
    }
#ifdef DEBUG_PROFILE
    else if (bg_fj_rows && bg_cj_pending) gp_fj_late_cj++;
    else if (bg_fj_rows) gp_fj_late_ly++;
#endif
}

// VBlank interrupt handler: latches scroll, runs the requested parallax GDMA and
// the requested map uploads, in that order.
#ifdef DEBUG_PROFILE
extern volatile uint8_t gpmark;
#endif
#ifdef DEBUG_PROFILE
// tools: VBlank uploads put off to the next VBlank for lack of time
volatile uint16_t gp_gdma_late, gp_rj_late;
#endif

// Parallax GDMA (CGB, ~3.4 lines) if it can still finish inside VBlank, else it stays pending and
// is retried on the next VBlank (the sky stands still for a frame)
static void parallax_try(void) {
    if (!bg_gdma_pending) return;
    uint8_t ly = LY_REG;
    if (ly >= 144u && ly <= 149u) {
        bg_gdma_pending = 0;
        parallax_gdma(bg_gdma_phase);
    }
#ifdef DEBUG_PROFILE
    else gp_gdma_late++;
#endif
}
void bg_parallax_vbl_isr(void) {
#ifdef DEBUG_PROFILE
    uint8_t prof_prev = gpmark;
    gpmark = 21;   // tools/profile.py: "VBlank handler"
#endif
    bg_vbl_seen = 1;
    bg_vbl_frames++;

    // Latch this frame's scroll first thing, so it can never land in the
    // visible frame however late the main thread wakes up.
    if (bg_scroll_pending) {
        bg_scroll_pending = 0;
        SCX_REG = bg_scx;
        SCY_REG = bg_scy;
        if (bg_lcdc_map != 0xFF) {
            // seamless mirror portal: the other BG map, in the same VBlank as its scroll
            LCDC_REG = (uint8_t)((LCDC_REG & (uint8_t)~LCDCF_BG9C00) | bg_lcdc_map);
            bg_lcdc_map = 0xFF;
        }
    }
    // the BG map shown (and streamed)
    uint8_t *map = (LCDC_REG & LCDCF_BG9C00) ? (uint8_t *)0x9C00 : (uint8_t *)0x9800;
    if (bg_dmg_pal_pending) {
        bg_dmg_pal_pending = 0;
        BGP_REG = bg_dmg_bgp;
        OBP0_REG = bg_dmg_obp0;
        OBP1_REG = bg_dmg_obp1;
    }
    uint8_t ly = LY_REG;
    // The interrupted code may be in the middle of a VRAM bank-1 write.
    uint8_t vbk = VBK_REG;
    // CGB: the player frame first (64 byte GDMA, ~1/4 line): after the map uploads it was often put
    // off for several frames in a row and the cube's spin stalled. DMG: after them, below.
    if (bg_cube_pending && bg_gdma_isr_on) {
        uint8_t prev_b = _current_bank;
        SWITCH_ROM(bg_cube_bank);
        VBK_REG = 0;
        HDMA1_REG = (uint8_t)((uint16_t)bg_cube_src >> 8);
        HDMA2_REG = (uint8_t)((uint16_t)bg_cube_src & 0xF0);
        HDMA3_REG = (uint8_t)(((uint16_t)bg_cube_dst >> 8) & 0x1F);
        HDMA4_REG = (uint8_t)((uint16_t)bg_cube_dst & 0xF0);
        HDMA5_REG = 3;   // 4 blocks of 16 bytes
        SWITCH_ROM(prev_b);
        bg_cube_pending = 0;
    }
    if (bg_pal_request && ly >= 144u && ly <= 150u) {
        bg_pal_request = 0;
        upload_palette();
        famidash_bkg_palettes_dirty = 0;
        ly = LY_REG;
    }
    // Map uploads. Each one only starts if it is certain to finish inside VBlank
    // (column slice ~0.6k dots, top rows ~0.5k dots); otherwise it stays pending
    // and is retried on the next VBlank. LY is re-read before each item.
    {
        ly = LY_REG;
        if (bg_cj_pending && ly >= 144u && ly <= 150u) {
            up_dst = map + ((uint16_t)bg_cj_y << 5) + bg_cj_x;
            up_src = bg_cj_tiles;
            VBK_REG = 0;
            copy_rows2();
            if (bg_gdma_isr_on) {   // CGB attributes (DMG: no VRAM bank 1, it would overwrite the tiles)
                up_dst = map + ((uint16_t)bg_cj_y << 5) + bg_cj_x;
                up_src = bg_cj_attrs;
                VBK_REG = 1;
                copy_rows2();
            }
            bg_cj_pending = 0;
            // Fading cells (a few cells): after the parallax they found VBlank used up and fell
            // behind for frames
            fade_cells_try();
            // A column and a row in one VBlank leave no time for the parallax after them: the
            // parallax first, unless a row is waiting (climbing: rows that fall behind show stale
            // tiles for seconds, a parallax step put off by a frame is barely visible)
            if (!bg_rj_pending) parallax_try();
        }
        ly = LY_REG;
        // never before a column slice still pending: built for the band before it moved, it
        // would overwrite the new row afterwards (a stale tile while climbing fast)
        // DMG: from line 150 on it could run past VBlank (CGB, double speed: 151)
        if (bg_rj_pending && !bg_cj_pending && ly >= 144u && ly <= (bg_gdma_isr_on ? 151u : 150u)) {
            uint8_t *row0 = map + ((uint16_t)bg_rj_y << 5) + bg_rj_x;
            VBK_REG = 0;
            up_dst = row0; up_src = bg_rj_tiles; copy_row16x2();
            if (bg_gdma_isr_on) {
                VBK_REG = 1;
                up_dst = row0; up_src = bg_rj_attrs; copy_row16x2();
            }
            bg_rj_pending = 0;
        }
#ifdef DEBUG_PROFILE
        else if (bg_rj_pending && !bg_cj_pending) gp_rj_late++;
#endif
    }
    // Fading cells (when no column slice came first), before the player frame (DMG: it takes what
    // is left of VBlank) and the parallax
    fade_cells_try();
    // DMG player frame (4 sprite tiles, double buffered by gameplay): as many tiles as still fit,
    // the rest on the next VBlank
    if (bg_cube_pending && !bg_gdma_isr_on) {
        uint8_t prev_b = _current_bank;
        SWITCH_ROM(bg_cube_bank);
        cube_dmg_copy();
        SWITCH_ROM(prev_b);
    }
    // Parallax GDMA after the map uploads (unless a column took the time, see above): first, it
    // often left the row job too little of VBlank, and in a fast climb the rows fell behind
    // (stale rows on screen)
    parallax_try();
    // Saw animation chunk (~0.5k dots), last: the map streaming above matters more
    ly = LY_REG;
    if (bg_saw_pending && ly >= 144u && ly <= 151u) {
        bg_saw_pending = 0;
        saw_gdma();
    }
    // DMG saw tiles: as many as fit in this VBlank, the rest on the next one
    if (bg_saw_dmg_n && ly >= 144u && ly <= 151u) {
        uint8_t prev_b = _current_bank;
        SWITCH_ROM(bg_saw_bank);
        saw_dmg_copy();
        SWITCH_ROM(prev_b);
    }
    VBK_REG = vbk;
    // last: the music ticks put off so as not to delay this handler (main.c)
    if (music_vbl_ticks) music_vbl_run();
#ifdef DEBUG_PROFILE
    gpmark = prof_prev;
#endif
}

static void saw_gdma(void) {
    uint8_t prev_b = _current_bank;
    SWITCH_ROM(bg_saw_bank);
    VBK_REG = 1;
    HDMA1_REG = (uint8_t)((uint16_t)bg_saw_src >> 8);
    HDMA2_REG = (uint8_t)((uint16_t)bg_saw_src & 0xF0);
    HDMA3_REG = (uint8_t)((bg_saw_dst >> 8) & 0x1F);
    HDMA4_REG = (uint8_t)(bg_saw_dst & 0xF0);
    HDMA5_REG = (uint8_t)(bg_saw_blocks - 1u);
    VBK_REG = 0;
    SWITCH_ROM(prev_b);
}

static void parallax_gdma(uint8_t phase) {
    uint8_t bank = 41 + (phase >> 4);
    uint8_t p_in_bank = phase & 15u;
    const uint8_t *src;
    if (bank == 41) src = bg_parallax_phases_0[p_in_bank];
    else if (bank == 42) src = bg_parallax_phases_1[p_in_bank];
    else if (bank == 43) src = bg_parallax_phases_2[p_in_bank];
    else src = bg_parallax_phases_3[p_in_bank];

    uint8_t prev_b = _current_bank;
    SWITCH_ROM(bank);

    VBK_REG = 1;
    // Fast CGB GDMA transfer: 48 blocks of 16 bytes = 768 bytes
    // Destination: VRAM Bank 1, 0x9000 (tiles 0..47 in LCDC signed mode)
    HDMA1_REG = (uint8_t)((uint16_t)src >> 8);
    HDMA2_REG = (uint8_t)((uint16_t)src & 0xF0);
    HDMA3_REG = 0x90;
    HDMA4_REG = 0x00;
    HDMA5_REG = 47; // 48 blocks (768 bytes)
    VBK_REG = 0;

    SWITCH_ROM(prev_b);
}
