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

#define BS_QUEUE 32                     // a power of 2
static uint8_t bs_q_slot[BS_QUEUE], bs_q_tile[BS_QUEUE];
static uint8_t bs_q_head;
uint8_t bs_q_n;                         // queued tiles (gameplay.c calls bg_stream_feed while any)

volatile uint8_t bs_up_i, bs_up_n;
uint8_t bs_up_slot[BS_UP_MAX];
uint8_t bs_up_buf[BS_UP_MAX * 16];

static uint8_t bs_level = 0xFF;         // BS_LEVEL_COUNT and up: no stream
static uint8_t bs_bank, bs_dmg, bs_rev;
static uint8_t bs_pool;                 // the level's tiles
static uint8_t bs_tables_ok;            // the metatile tables already point at this level's slots
static const uint8_t *bs_tile_base;
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

// DMG: the tiles, then the same tiles mirrored; CGB mirrors with the X flip attribute
static const uint8_t *tile_src(uint8_t t) {
    return bs_tile_base + ((uint16_t)(bs_rev ? (uint16_t)bs_pool + t : t) << 4);
}

// Display off: runs of tiles going to consecutive slots, copied in one go
static uint8_t run_slot, run_tile, run_n;
static void run_flush(void) {
    if (run_n) fur_copy_banked(BG_TILE_ADDR(run_slot), tile_src(run_tile), (uint16_t)run_n << 4, bs_bank);
    run_n = 0;
}
static void run_add(uint8_t s, uint8_t t) {
    // (slots 127 and 128 are not next to each other in VRAM: 0x97F0, 0x8800)
    if (run_n && s == (uint8_t)(run_slot + run_n) && t == (uint8_t)(run_tile + run_n) && s != 128u && run_n < 255u) {
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
    if (level >= BS_LEVEL_COUNT) return;
    bs_dmg = (_cpu != CGB_TYPE);
    bs_rev = bs_dmg ? reversed : 0;
    uint8_t k = (uint8_t)((level << 1) | bs_dmg);
    bs_bank = bs_banks[level];
    bs_tile_base = bs_tiles[k];
    bs_rp = bs_recs[k];
    bs_pool = rd();
    // the same level again (respawn, mirror portal) with tiles that are each loaded once: the
    // tables are this level's already, only the tiles are uploaded again
    uint8_t flags = rd();   // (read whatever tables_kept is)
    bs_tables_ok = (uint8_t)(tables_kept && (flags & 1u));
    bs_col = rd();
    bs_col |= (uint16_t)rd() << 8;
}

static void enqueue(uint8_t s, uint8_t t) {
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
            uint8_t s = rd(), t = rd();
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

void bg_stream_feed(void) BANKED {
    if (bs_up_i < bs_up_n || !bs_q_n) return;
    // DMG: 2 a frame (the copies here and in VBlank take time from a full frame), CGB: BS_UP_MAX
    uint8_t k = 0, most = bs_dmg ? 2u : BS_UP_MAX;
    while (bs_q_n && k < most) {
        uint8_t i = bs_q_head;
        bs_up_slot[k] = bs_q_slot[i];
        fur_copy_banked(bs_up_buf + (k << 4), tile_src(bs_q_tile[i]), 16, bs_bank);
        bs_q_head = (uint8_t)((i + 1u) & (BS_QUEUE - 1));
        bs_q_n--;
        k++;
    }
    // the handler only uploads while bs_up_i < bs_up_n: never true half way through this
    bs_up_n = 0;
    bs_up_i = 0;
    bs_up_n = k;
}
