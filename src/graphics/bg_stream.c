#pragma bank 255

// Background tile streaming (docs/PLAN_1.6.md, phase 1). Each level has its own tiles; every map
// column has records "tile T into BG slot S" (tools/bg_stream.py), applied before the column is
// drawn: the metatile quarters showing T are pointed at S in mt_ram, and T is uploaded (display
// off: at once; else queued, and the VBlank handler copies it). The build assigns the slots so
// that a tile is never replaced while a column of the VRAM ring shows it.

#include <gb/gb.h>
#include <stdint.h>
#include "bg_stream.h"
#include "furplayer.h"
#include "bg_stream_tables.h"
#include "bg_parallax.h"
#include "collision.h"
#define BKG_MT_H 16

#define BS_QUEUE 16                     // a power of 2
static uint8_t bs_q_slot[BS_QUEUE];
static uint16_t bs_q_tile[BS_QUEUE];
static uint8_t bs_q_head;
uint8_t bs_q_n;                         // queued tiles (gameplay.c calls bg_stream_feed while any)

// Spinning objects (CGB): their tiles have slots of their own from BS_SPIN_BASE; each step uploads
// the next frame by GDMA in VBlank, in chunks (bg_parallax_phases.c), so all copies turn together
#define BS_SPIN_CHUNK 13                // tiles a VBlank
#define BS_SPIN_PERIOD 6                // frames from one step to the next, at the fastest
uint8_t bs_spin_n;                      // the level's spinning tiles (0: none, DMG)
static uint16_t bs_spin_tile;
static uint8_t bs_spin_frame, bs_spin_pos, bs_spin_timer, bs_spin_issued;

uint8_t fade_n;                     // 0: a level without them, nothing here runs
static uint8_t fade_first;
static uint16_t fade_on;            // ring slots (map column & 15) drawn faded, a bit each
static uint16_t fade_any;           // ring slots that may hold a fading cell
static uint16_t fade_unknown;       // ring slots whose fading cells may be at either look
volatile uint8_t bg_fj_n;           // the cell job (fade_cells_copy): cells left
uint8_t bg_fj_x, bg_fj_start;       // its map column; its first entry in mj_out

volatile uint8_t bs_up_i, bs_up_n;
uint8_t bs_up_slot[BS_UP_MAX];
uint8_t bs_up_buf[BS_UP_MAX * 16];

static uint8_t bs_level = 0xFF;         // BS_LEVEL_COUNT and up: no stream
static uint8_t bs_bank, bs_dmg, bs_rev;
static uint16_t bs_pool;                // the level's tiles
static uint8_t bs_tables_ok;            // the metatile tables already point at this level's slots
static uint16_t bs_tile_base;           // the level's first tile in the tile space (see tile_of)
static const uint8_t *bs_rp;            // next record bytes to fetch from ROM
static uint8_t bs_buf[32], bs_bi, bs_bn;
uint16_t bs_col = 0xFFFF;               // the next column with records (0xFFFF: none)
static uint16_t bs_reached;             // the last column asked for (+1; 0: none)
static uint16_t bs_resume;              // bs_reached before the last reset

// VRAM address of BG tile slot (BG tiles 0..127 are at 0x9000, 128..255 at 0x8800)
#define BG_TILE_ADDR(slot) ((uint8_t *)((((slot) < 128u) ? 0x9000u : 0x8000u) + ((uint16_t)(slot) << 4)))

static uint8_t rd(void) {
    if (bs_bi == bs_bn) {
        fur_copy_banked(bs_buf, bs_rp, sizeof(bs_buf), bs_bank);
        bs_rp += sizeof(bs_buf);
        bs_bi = 0;
        bs_bn = sizeof(bs_buf);
    }
    return bs_buf[bs_bi++];
}

// All levels' tiles are one space of 16 byte tiles over consecutive banks from BS_TILE_BANK (1024
// tiles a bank), so a level can have any number of them. DMG: the tiles, then the same tiles
// mirrored; CGB mirrors with the X flip attribute
static uint16_t tile_of(uint16_t t) {
    return bs_tile_base + (bs_rev ? bs_pool + t : t);
}

#define TILE_BANK(g) ((uint8_t)(BS_TILE_BANK + ((g) >> 10)))
#define TILE_ADDR(g) ((const uint8_t *)(0x4000u + (((g) & 1023u) << 4)))

// Display off: runs of tiles going to consecutive slots, copied in one go
static uint8_t run_slot, run_n;
static uint16_t run_tile;
static void run_flush(void) {
    uint8_t s = run_slot;
    uint16_t g = tile_of(run_tile);
    while (run_n) {
        uint16_t k = 1024u - (g & 1023u);     // up to the end of the bank
        if (k > run_n) k = run_n;
        fur_copy_banked(BG_TILE_ADDR(s), TILE_ADDR(g), k << 4, TILE_BANK(g));
        s += (uint8_t)k;
        g += k;
        run_n -= (uint8_t)k;
    }
}
static void run_add(uint8_t s, uint16_t t) {
    // (slots 127 and 128 are not next to each other in VRAM: 0x97F0, 0x8800)
    if (run_n && s == (uint8_t)(run_slot + run_n) && t == run_tile + run_n && s != 128u && run_n < 255u) {
        run_n++;
        return;
    }
    run_flush();
    run_slot = s;
    run_tile = t;
    run_n = 1;
}

void bg_stream_reset(uint8_t level, uint8_t reversed, uint8_t tables_kept) BANKED {
    bs_resume = (level == bs_level) ? bs_reached : 0;
    // the tables only hold all of this level's slots once every record was applied
    tables_kept = (uint8_t)(tables_kept && level == bs_level && bs_col == 0xFFFFu);
    bs_level = level;
    bs_up_n = 0;
    bs_up_i = 0;
    bs_q_n = 0;
    bs_col = 0xFFFF;
    bs_reached = 0;
    bs_bi = bs_bn = 0;
    bs_spin_n = 0;
    fade_n = 0;
    fade_on = 0;
    fade_reset_view();
    if (level >= BS_LEVEL_COUNT) return;
    fade_first = bs_fade[level << 1];
    fade_n = bs_fade[(level << 1) + 1u];
    bs_dmg = (_cpu != CGB_TYPE);
    if (!bs_dmg) {
        // starts idle at frame 0, the frame the records load
        bs_spin_n = bs_spin_count[level];
        bs_spin_tile = bs_spin_first[level];
        bs_spin_frame = 0;
        bs_spin_pos = bs_spin_n;
        bs_spin_timer = 0;
        bs_spin_issued = 0;
    }
    bs_rev = bs_dmg ? reversed : 0;
    uint8_t k = (uint8_t)((level << 1) | bs_dmg);
    bs_bank = bs_banks[level];
    bs_tile_base = bs_tile_first[k];
    bs_rp = bs_recs[k];
    bs_pool = rd();
    bs_pool |= (uint16_t)rd() << 8;
    // the same level again (respawn, mirror portal) with tiles that are each loaded once: the
    // tables are this level's already, only the tiles are uploaded again
    uint8_t flags = rd();   // (read whatever tables_kept is)
    bs_tables_ok = (uint8_t)(tables_kept && (flags & 1u));
    bs_col = rd();
    bs_col |= (uint16_t)rd() << 8;
}

static void enqueue(uint8_t s, uint16_t t) {
    // full (never with the levels so far): let the VBlank handler drain it
    while (bs_q_n == BS_QUEUE) {
        bg_stream_feed();
        while (bs_up_i < bs_up_n) {}
    }
    uint8_t i = (uint8_t)((bs_q_head + bs_q_n) & (BS_QUEUE - 1));
    bs_q_slot[i] = s;
    bs_q_tile[i] = t;
    bs_q_n++;
}

void bg_stream_to(uint16_t col) BANKED {
    if (bs_level >= BS_LEVEL_COUNT) return;
    if (col >= bs_reached) bs_reached = col + 1u;
    uint8_t off = !(LCDC_REG & LCDCF_ON);
    uint8_t vbk = VBK_REG;   // CGB: the column flush leaves VRAM bank 1 (attributes) selected
    if (off && !bs_dmg) VBK_REG = 0;
    while (bs_col <= col) {
        for (uint8_t n = rd(); n; n--) {
            uint8_t s = rd();
            uint16_t t = rd();
            t |= (uint16_t)rd() << 8;
            uint8_t k = rd();
            if (bs_tables_ok) {
                // skip the quarters: inside the buffer, or refetch from where they end
                uint16_t skip = (uint16_t)k << 1;
                if ((uint16_t)bs_bi + skip <= bs_bn) {
                    bs_bi += (uint8_t)skip;
                } else {
                    bs_rp = bs_rp - bs_bn + bs_bi + skip;
                    bs_bi = bs_bn = 0;
                }
            } else {
                for (; k; k--) {
                    uint8_t m = rd(), q = rd();
                    mt_ram[0][m][q] = s;
                    mt_ram[1][m][q ^ 1u] = s;   // mirrored: left and right quarters swapped
                }
            }
            if (t == 0xFFFFu) continue;     // no tile: only the metatile quarters
            if (off) run_add(s, t);
            else enqueue(s, t);
        }
        bs_col = rd();
        bs_col |= (uint16_t)rd() << 8;
    }
    run_flush();
    if (off && !bs_dmg) VBK_REG = vbk;
}

void bg_stream_resume(void) BANKED {
    if (bs_resume) bg_stream_to(bs_resume - 1u);
}

// (gameplay.c)
extern uint16_t loaded_r, level_map_w, col_job_col;
extern const uint8_t *level_map;
extern uint8_t level_map_bank, view_rev;

// Fading objects, as Geometry Dash Advance draws them (level_routines.c handle_fading_blocks): by
// the cell's place on the screen, full at the edges, faded in the middle, back to full behind the
// player. No state to go wrong: a checkpoint, a respawn or a mirror portal only asks where a column
// is now. GDA's fade_frame_table on our screen (its 240 px over our 160): faded 43 .. 127 px.
// The cells are metatiles fade_first .. + fade_n - 1, each faded look fade_n ids further
// (tools/compose.py): on CGB the same tiles in palette 7, on DMG, which has no shade to spare,
// without the object. Collision comes from the map: they stay solid and deadly.
#define FADE_FROM 43u
#define FADE_TO 128u
#define IS_FADE(id) ((uint8_t)((id) - fade_first) < fade_n)

// Every fading cell's look unknown: fade_update looks at them all
void fade_reset_view(void) BANKED {
    bg_fj_n = 0;
    fade_unknown = fade_any = 0xFFFFu;
}

// Column col's ids go to its ring slot, drawn full (a column comes in off the screen)
void fade_hide(uint16_t col, uint8_t *ids, uint8_t n) BANKED {
    uint16_t bit = (uint16_t)1 << ((uint8_t)col & 15u);
    fade_on &= ~bit;
    fade_unknown &= ~bit;
    fade_any &= ~bit;
    do {
        if (IS_FADE(*ids)) fade_any |= bit;
        ids++;
    } while (--n);
}

// A row of ring slots: each cell at its slot's look
void fade_row(uint8_t first, uint8_t n, uint16_t loaded_r, uint8_t reversed, uint8_t *ids) BANKED {
    (void)loaded_r;
    for (uint8_t i = 0; i < n; i++) {
        if (!IS_FADE(ids[i])) continue;
        uint8_t s = (uint8_t)(first + i);
        if (reversed) s = (uint8_t)-(int8_t)s;
        uint16_t bit = (uint16_t)1 << (s & 15u);
        fade_any |= bit;
        if (fade_on & bit) ids[i] += fade_n;
    }
}

// The cell job's list from the cells built in mj_out (row r: tiles at 4r, attributes at 64 + 4r) of
// the rows set in `rows`: per cell its row and the 4 bytes the handler writes (CGB attributes, the
// tiles are the same; DMG tiles), packed over bytes already read: CGB from mj_out up, DMG from
// mj_out + 128 down.
static void fade_pack(uint16_t rows) {
    uint8_t n = 0;
    if (bg_gdma_isr_on) {
        uint8_t *e = mj_out;
        for (uint8_t r = 0; r < 16u; r++, rows >>= 1) {
            if (!(rows & 1u)) continue;
            const uint8_t *a = mj_out + 64u + (uint8_t)(r << 2);
            e[0] = r; e[1] = a[0]; e[2] = a[1]; e[3] = a[2]; e[4] = a[3];
            e += 5; n++;
        }
        bg_fj_start = 0;
    } else {
        uint8_t *e = mj_out + 128u;
        for (uint8_t r = 16u; r--; rows <<= 1) {
            if (!(rows & 0x8000u)) continue;
            const uint8_t *t = mj_out + (uint8_t)(r << 2);
            e -= 5;
            e[1] = t[0]; e[2] = t[1]; e[3] = t[2]; e[4] = t[3]; e[0] = r;
            n++;
        }
        bg_fj_start = (uint8_t)(e - mj_out);
    }
    bg_fj_n = n;
}

// Once a frame in levels with fading objects: the first ring slot whose cells are not at the look
// for where it is now gets them rewritten, by the VBlank handler. One slot at a time.
void fade_update(uint8_t scx) BANKED {
    if (bg_fj_n) return;
    uint8_t reversed = view_rev;
    uint16_t want = 0, bit = 1;
    for (uint8_t s = 0; s < 16u; s++, bit <<= 1) {
        uint8_t sx = (uint8_t)((uint8_t)((reversed ? (uint8_t)-(int8_t)s : s) << 4) - scx);
        if (sx >= FADE_FROM && sx < FADE_TO) want |= bit;
    }
    // (a slot without fading cells just follows)
    fade_on = (fade_on & fade_any) | (want & ~fade_any);
    uint16_t todo = ((want ^ fade_on) | fade_unknown) & fade_any;
    if (!todo) return;
    uint8_t s = 0;
    for (bit = 1; !(todo & bit); bit <<= 1) s++;
    uint16_t c = loaded_r - (uint8_t)((uint8_t)((uint8_t)loaded_r - s) & 15u);
    uint8_t on = (want & bit) != 0;
    if (on) fade_on |= bit; else fade_on &= ~bit;
    fade_unknown &= ~bit;
    uint16_t rows = 0, rb = 1;
    uint8_t lo = 0, hi = 0;
    if (c < level_map_w) {
        get_map_column(c, level_map, level_map_bank, mj_col, mt_band);
        for (uint8_t r = 0; r < BKG_MT_H; r++, rb <<= 1) {
            if (!IS_FADE(mj_col[r])) continue;
            if (on) mj_col[r] += fade_n;
            if (!rows) lo = r;
            hi = r;
            rows |= rb;
        }
    }
    if (!rows) {
        fade_any &= ~bit;
        return;
    }
    fade_build(c, reversed, lo, (uint8_t)(hi + 1u));
    bg_fj_x = (uint8_t)(((reversed ? (uint8_t)-(int8_t)s : s) & 15u) << 1);
    fade_pack(rows);
}

// The VBlank handler (bank switched in): the list's cells, from the last one, to map column bg_fj_x
// of the BG map shown while LY is 144 .. ly_end - 1 (a cell takes well under a line); leaves bg_fj_n
// at the cells not written. VRAM bank: CGB 1 (attributes), DMG 0 (tiles).
BANKREF(fade_cells_copy)
void fade_cells_copy(uint8_t ly_end) __naked {
    (void)ly_end;
    __asm
        ld      e, a                    ; e = ly_end
        ld      a, (_bg_fj_n)
        or      a, a
        ret     Z
        ld      d, a                    ; d = cells left: entries 0 .. d - 1
        ld      a, (_bg_gdma_isr_on)
        ldh     (_VBK_REG + 0), a
        ldh     a, (_LCDC_REG + 0)
        and     a, #0x08                ; the map shown: 0x9800, or 0x9C00 when set
        rrca
        or      a, #0x98
        ld      c, a                    ; c = its high byte
        ld      a, (_bg_fj_x)
        ld      b, a                    ; b = tile x
        ld      a, d
        dec     a
        ld      l, a
        add     a, a
        add     a, a
        add     a, l                    ; 5 (d - 1)
        ld      hl, #_bg_fj_start
        add     a, (hl)                 ; the last entry (< 128)
        add     a, #<_mj_out
        ld      l, a
        ld      a, #>_mj_out
        adc     a, #0
        ld      h, a                    ; hl = the last entry
    1$:
        ldh     a, (_LY_REG + 0)
        cp      a, #144
        jr      C, 9$                   ; past VBlank: the rest next time
        cp      a, e
        jr      NC, 9$
        push    de
        ld      a, (hl+)                ; r
        rrca
        rrca
        ld      d, a
        and     a, #0xC0
        add     a, b                    ; (64r + x: no carry, x < 32)
        ld      e, a
        ld      a, d
        and     a, #0x03
        or      a, c
        ld      d, a                    ; de = map + 64r + x: tile row 2r
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        ld      a, e
        add     a, #31
        ld      e, a
        jr      NC, 2$
        inc     d
    2$:
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        ld      a, l
        sub     a, #10                  ; the entry before
        ld      l, a
        jr      NC, 4$
        dec     h
    4$:
        pop     de
        dec     d
        jr      NZ, 1$
    9$:
        ld      a, d
        ld      (_bg_fj_n), a
        ret
    __endasm;
}

// The column job takes the next column? Tiles backing up (heavy frames leave VBlank no time for
// them): it can wait a little, columns are drawn ~3 ahead of the screen. The queue must not fill:
// the game would stop until it drains, a freeze.
uint8_t col_job_next(void) BANKED {
    if (col_job_col < loaded_r && (bs_q_n < 8u || (uint16_t)(loaded_r - col_job_col) >= 2u)) {
        col_job_col++;
        return 1;
    }
    return 0;
}

void bg_stream_spin(void) BANKED {
    if (bs_spin_timer < 255u) bs_spin_timer++;
    if (bs_spin_issued) {
        if (bg_saw_pending) return;
        bs_spin_issued = 0;
        bs_spin_pos += BS_SPIN_CHUNK;
    }
    if (bs_spin_pos >= bs_spin_n) {
        if (bs_spin_timer < BS_SPIN_PERIOD) return;
        bs_spin_timer = 0;
        bs_spin_frame = (uint8_t)((bs_spin_frame + 1u) % BS_SPIN_FRAMES);
        bs_spin_pos = 0;
    }
    uint8_t k = (uint8_t)(bs_spin_n - bs_spin_pos);
    if (k > BS_SPIN_CHUNK) k = BS_SPIN_CHUNK;
    uint16_t g = bs_spin_tile + (uint16_t)bs_spin_frame * bs_spin_n + bs_spin_pos;
    bg_saw_src = TILE_ADDR(g);
    bg_saw_bank = TILE_BANK(g);
    bg_saw_dst = (uint16_t)0x1000 + ((uint16_t)(BS_SPIN_BASE + bs_spin_pos) << 4);
    bg_saw_blocks = k;
    bg_saw_vbk = 0;
    bg_saw_pending = 1;
    bs_spin_issued = 1;
}

void bg_stream_feed(void) BANKED {
    if (bs_up_i < bs_up_n || !bs_q_n) return;
    // DMG: 2 a frame (the copies here and in VBlank take time from a full frame), CGB: BS_UP_MAX
    uint8_t k = 0, most = bs_dmg ? 2u : BS_UP_MAX;
    while (bs_q_n && k < most) {
        uint8_t i = bs_q_head;
        bs_up_slot[k] = bs_q_slot[i];
        uint16_t g = tile_of(bs_q_tile[i]);
        fur_copy_banked(bs_up_buf + (k << 4), TILE_ADDR(g), 16, TILE_BANK(g));
        bs_q_head = (uint8_t)((i + 1u) & (BS_QUEUE - 1));
        bs_q_n--;
        k++;
    }
    // the handler only uploads while bs_up_i < bs_up_n: never true half way through this
    bs_up_n = 0;
    bs_up_i = 0;
    bs_up_n = k;
}
