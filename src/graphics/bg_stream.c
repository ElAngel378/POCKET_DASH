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
static uint16_t fade_upto;
static uint16_t col_back;           // the column job's column to go back to after a redrawn one

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
    fade_upto = 0;
    col_back = 0;
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

// Fading objects: a column reached by the player shows its cells' hidden looks. The cells are
// metatiles fade_first .. + fade_n - 1, each hidden look fade_n ids further (tools/compose.py);
// columns up to fade_upto are hidden. Collision comes from the map: they stay solid and deadly.
#define FADE_AHEAD 40               // px ahead of the player

void fade_hide(uint16_t col, uint8_t *ids, uint8_t n) BANKED {
    if (col > fade_upto) return;
    do {
        if ((uint8_t)(*ids - fade_first) < fade_n) *ids += fade_n;
        ids++;
    } while (--n);
}

// A row of ring slots: each slot's own column decides (get_map_row_slots)
void fade_row(uint8_t first, uint8_t n, uint16_t loaded_r, uint8_t reversed, uint8_t *ids) BANKED {
    for (uint8_t i = 0; i < n; i++) {
        uint8_t s = (uint8_t)(first + i);
        if (reversed) s = (uint8_t)(-(int8_t)s & 15u);
        fade_hide(loaded_r - (uint8_t)((uint8_t)((uint8_t)loaded_r - s) & 15u), ids + i, 1);
    }
}

// The next column to draw (0: none), in *cur. Tiles backing up (heavy frames leave VBlank no time
// for them): the next column can wait a little, columns are drawn ~3 ahead of the screen. The
// queue must not fill: the game would stop until it drains, a freeze. With nothing else to do, the
// next column with fading objects the player got to is drawn once more.
uint16_t col_job_next(uint16_t *cur, uint16_t loaded_r, uint16_t px, const uint8_t *map, uint8_t map_bank) BANKED {
    if (col_back) { *cur = col_back; col_back = 0; }
    if (*cur < loaded_r && (bs_q_n < 8u || (uint16_t)(loaded_r - *cur) >= 2u)) return ++*cur;
    if (!fade_n || bs_q_n >= 4u) return 0;      // (tiles first)
    uint16_t t = (uint16_t)(px + FADE_AHEAD) >> 4;
    if (fade_upto >= t) return 0;
    if (fade_upto + 16u < t) fade_upto = t - 16u;       // a checkpoint further on
    fade_upto++;
    uint8_t ids[BKG_MT_H];
    get_map_column(fade_upto, map, map_bank, ids, mt_band);
    for (uint8_t r = 0; r < BKG_MT_H; r++) {
        if ((uint8_t)(ids[r] - fade_first) < fade_n) {
            col_back = *cur;
            *cur = fade_upto;
            return fade_upto;
        }
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
