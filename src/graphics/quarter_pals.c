#pragma bank 16   // bg_level_info.h LEVEL_TABLES_BANK: the renderer's bank is full

#include <gb/gb.h>
#include <stdint.h>
#include "bg_level_info.h"

// A cell of a new level whose objects fit no one palette (tools/compose.py QUARTER_PAIRS): its
// palette byte is 0x81 | a bit per quarter << 1 | pair << 5 (saws: 0x80 and an even palette), and its
// 8x8 quarters choose between the two palettes. Empty quarters keep what they have (the parallax,
// attribute 0x0B).
static const uint8_t mq_pairs[4][2] = { {0, 2}, {0, 7}, {2, 7}, {0, 6} };

static void put_pals(uint8_t *top, uint8_t *bottom, uint8_t id, uint8_t reversed) {
    uint8_t v = bm_info[id];
    if ((v & 0x81u) != 0x81u) return;
    const uint8_t *tiles = (reversed ? &mt_tab_rev[0][0] : &mt_tab[0][0]) + ((uint16_t)id << 2);
    uint8_t flip = (reversed && mt_cgb_flip) ? 0x20u : 0u;
    uint8_t m = (v >> 1) & 15u;
    if (reversed) m = (uint8_t)(((m & 5u) << 1) | ((m >> 1) & 5u));
    const uint8_t *p = mq_pairs[(v >> 5) & 3u];
    for (uint8_t q = 0; q < 4u; q++) {
        if (tiles[q] != 12u) {
            uint8_t *a = (q < 2u) ? top + q : bottom + q - 2u;
            *a = (uint8_t)(p[m & 1u] | flip);
        }
        m >>= 1;
    }
}

void quarter_pals_column(uint8_t *attrs, const uint8_t *ids, uint8_t reversed, uint8_t r_start, uint8_t r_end, uint8_t skip) BANKED {
    for (uint8_t r = r_start; r < r_end; r++) {
        if (r == skip) continue;
        uint8_t *a = attrs + (r << 2);
        put_pals(a, a + 2, ids[r], reversed);
    }
}

void quarter_pals_row(uint8_t n, uint8_t reversed) BANKED {
    for (uint8_t i = 0; i < n; i++) {
        uint8_t k = (uint8_t)(i << 1);
        put_pals(&row_attrs[0][k], &row_attrs[1][k], rt_ids[i], reversed);
    }
}
