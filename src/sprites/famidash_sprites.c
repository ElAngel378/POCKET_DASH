#pragma bank 10

#include <gb/gb.h>
#include <gbdk/incbin.h>
#include <gbdk/metasprites.h>

#include "famidash_sprites.h"

#define P0 S_PAL(0)
#define P1 S_PAL(1)
#define P2 S_PAL(2)
#define P3 S_PAL(3)
#define P4 S_PAL(4)
#define P5 S_PAL(5)
#define PT_41 0
#define PT_43 2
#define PT_45 4
#define PT_47 6
#define PT_49 8
#define PT_4B 10
#define PT_6D 12
#define PT_6F 14
#define PT_73 16
#define PT_75 18
#define PT_79 20
#define PT_7B 22
#define MT_99 24
#define MT_9B 26
#define MT_B9 28
#define MT_BB 30
#define PT_59 32
#define PT_5B 34
#define PT_4D 36
#define PT_4F 38
#define PT_51 40
#define PT_53 42
#define PT_55 44

// 3 columns wide (24px) x 3 rows tall.
// Carriage return goes down 16 (dy=16) and back left 16 (dx=-16) to hit column 1.
        const metasprite_t famidash_cube_portal[] = {
                METASPR_ITEM(0, 0, PT_41, P1),   METASPR_ITEM(0, 8, PT_43, P1),   METASPR_ITEM(0, 8, PT_45, P1),
                METASPR_ITEM(16, -16, PT_47, P1), METASPR_ITEM(0, 8, PT_49, P1),   METASPR_ITEM(0, 8, PT_4B, P1),
                METASPR_ITEM(16, -16, PT_41, P1 | S_FLIPY), METASPR_ITEM(0, 8, PT_43, P1 | S_FLIPY), METASPR_ITEM(0, 8, PT_45, P1 | S_FLIPY),
                METASPR_TERM
        };

        const metasprite_t famidash_ship_portal[] = {
                METASPR_ITEM(0, 0, PT_41, P4),   METASPR_ITEM(0, 8, PT_43, P4),   METASPR_ITEM(0, 8, PT_45, P4),
                METASPR_ITEM(16, -16, PT_47, P4), METASPR_ITEM(0, 8, PT_49, P4),   METASPR_ITEM(0, 8, PT_4B, P4),
                METASPR_ITEM(16, -16, PT_41, P4 | S_FLIPY), METASPR_ITEM(0, 8, PT_43, P4 | S_FLIPY), METASPR_ITEM(0, 8, PT_45, P4 | S_FLIPY),
                METASPR_TERM
        };

        const metasprite_t famidash_ball_portal[] = {
                METASPR_ITEM(0, 0, PT_41, P5),   METASPR_ITEM(0, 8, PT_43, P5),   METASPR_ITEM(0, 8, PT_45, P5),
                METASPR_ITEM(16, -16, PT_47, P5), METASPR_ITEM(0, 8, PT_49, P5),   METASPR_ITEM(0, 8, PT_4B, P5),
                METASPR_ITEM(16, -16, PT_41, P5 | S_FLIPY), METASPR_ITEM(0, 8, PT_43, P5 | S_FLIPY), METASPR_ITEM(0, 8, PT_45, P5 | S_FLIPY),
                METASPR_TERM
        };

#define HORIZ_PORTAL(name, palette, flip_v) \
const metasprite_t name[] = { \
    METASPR_ITEM(0, 0, PT_4D, palette | flip_v), METASPR_ITEM(0, 8, PT_4F, palette | flip_v), METASPR_ITEM(0, 8, PT_51, palette | flip_v), \
    METASPR_ITEM(0, 8, PT_51, palette | flip_v | S_FLIPX), METASPR_ITEM(0, 8, PT_4F, palette | flip_v | S_FLIPX), METASPR_ITEM(0, 8, PT_4D, palette | flip_v | S_FLIPX), \
    METASPR_TERM \
}

        HORIZ_PORTAL(famidash_portal_dn_horiz_dn, P2, 0);
        HORIZ_PORTAL(famidash_portal_dn_horiz_up, P2, S_FLIPY);
        HORIZ_PORTAL(famidash_portal_up_horiz_dn, P3, 0);
        HORIZ_PORTAL(famidash_portal_up_horiz_up, P3, S_FLIPY);

// 2 columns wide (16px) x 3 rows tall.
// Carriage return goes down 16 (dy=16) and back left 8 (dx=-8) to hit column 1.
#define VERTICAL_PORTAL(name, palette) \
const metasprite_t name[] = { \
    METASPR_ITEM(0, 0, PT_6D, palette),             METASPR_ITEM(0, 8, PT_6F, palette), \
    METASPR_ITEM(16, -8, PT_73, palette),           METASPR_ITEM(0, 8, PT_75, palette), \
    METASPR_ITEM(16, -8, PT_6D, palette | S_FLIPY), METASPR_ITEM(0, 8, PT_6F, palette | S_FLIPY), \
    METASPR_TERM \
}

        VERTICAL_PORTAL(famidash_gravity_down, P2);
        VERTICAL_PORTAL(famidash_gravity_up, P3);

// Redefined to apply props to BOTH tiles uniformly, and not force S_FLIPX.
// Redefined to allow independent properties for the left and right tiles
#define TWO_TILE(name, left, right, props_left, props_right) \
const metasprite_t name[] = { \
    METASPR_ITEM(0, 0, left, props_left), \
    METASPR_ITEM(0, 8, right, props_right), \
    METASPR_TERM \
}

// Pads
        TWO_TILE(famidash_yellow_pad,    PT_79, PT_7B, P3,           P3 | S_FLIPX);
        TWO_TILE(famidash_yellow_pad_up, PT_79, PT_7B, P3 | S_FLIPY, P3 | S_FLIPX | S_FLIPY);
        TWO_TILE(famidash_blue_pad,      PT_79, PT_7B, P2,           P2 | S_FLIPX);
        TWO_TILE(famidash_blue_pad_up,   PT_79, PT_7B, P2 | S_FLIPY, P2 | S_FLIPX | S_FLIPY);
        TWO_TILE(famidash_pink_pad,      PT_59, PT_5B, P4,           P4 | S_FLIPX);

// Orbs (Restored the S_FLIPX on the right tile so the circle completes)
        TWO_TILE(famidash_yellow_orb,    MT_99, MT_9B, P3,           P3 | S_FLIPX);
        TWO_TILE(famidash_blue_orb,      MT_99, MT_9B, P2,           P2 | S_FLIPX);
        TWO_TILE(famidash_pink_orb,      MT_B9, MT_BB, P4,           P4 | S_FLIPX);

const metasprite_t famidash_deco_45[] = {
    METASPR_ITEM(0, 0, CHAIN_BLOCK_TILE, DP),
    METASPR_ITEM(0, 8, CHAIN_BLOCK_TILE + 2, DP),
    METASPR_ITEM(16, -16, CHAIN_BLOCK_TILE + 4, DP),
    METASPR_ITEM(0, 8, CHAIN_BLOCK_TILE + 6, DP),
    METASPR_TERM
};

const metasprite_t * const famidash_sprite_table[38] = {
    famidash_cube_portal, famidash_ship_portal, famidash_ball_portal, 0, 0,
    famidash_blue_orb, famidash_pink_orb, 0,
    famidash_gravity_down, famidash_gravity_up,
    famidash_yellow_pad, famidash_yellow_orb,
    famidash_yellow_pad_up, famidash_blue_pad, famidash_blue_pad_up,
    0,
    famidash_portal_dn_horiz_dn, famidash_portal_dn_horiz_up,
    famidash_portal_up_horiz_dn, famidash_portal_up_horiz_up,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    famidash_pink_pad
};


static const FamidashDeco deco_long_light = {2, 16, {4,4,0}, {0,-16,0}, {D_CF,D_C9,0}, {DP,DP,0}};
static const FamidashDeco deco_medium_light = {2, 16, {4,4,0}, {0,-16,0}, {D_CF,D_CB,0}, {DP,DP,0}};
static const FamidashDeco deco_short_light = {1, 16, {4,0,0}, {0,0,0}, {D_CD,0,0}, {DP,0,0}};
static const FamidashDeco deco_chain = {2, 16, {4,4,0}, {0,-16,0}, {D_D5,D_D7,0}, {DP,DP,0}};
static const FamidashDeco deco_spike_g1 = {2, 16, {0,8,0}, {-4,-4,0}, {D_D9,D_DB,0}, {DP,DP,0}};
static const FamidashDeco deco_spike_g2 = {2, 16, {0,8,0}, {4,4,0}, {D_D9,D_DB,0}, {DP|S_FLIPY,DP|S_FLIPY,0}};
static const FamidashDeco deco_spike_g3 = {2, 16, {0,8,0}, {-4,-4,0}, {D_DD,D_DF,0}, {DP,DP,0}};
static const FamidashDeco deco_spike_g4 = {2, 16, {0,8,0}, {4,4,0}, {D_DD,D_DF,0}, {DP|S_FLIPY,DP|S_FLIPY,0}};
static const FamidashDeco deco_diamond = {2, 16, {0,8,0}, {0,0,0}, {D_E1,D_E1,0}, {DP,DP|S_FLIPX,0}};
static const FamidashDeco deco_diamond_half = {1, 16, {8,0,0}, {0,0,0}, {D_E1,0,0}, {DP|S_FLIPX,0,0}};
static const FamidashDeco deco_question = {1, 16, {4,0,0}, {0,0,0}, {D_E3,0,0}, {DP,0,0}};
static const FamidashDeco deco_exclamation = {1, 16, {4,0,0}, {0,0,0}, {D_E5,0,0}, {DP,0,0}};
static const FamidashDeco deco_arrow = {2, 16, {0,8,0}, {0,0,0}, {D_E7,D_E7,0}, {DP,DP|S_FLIPX,0}};
static const FamidashDeco deco_x = {2, 16, {0,8,0}, {0,0,0}, {D_ED,D_ED,0}, {DP,DP|S_FLIPX,0}};
static const FamidashDeco deco_short_right = {2, 16, {8,0,0}, {-4,-4,0}, {D_F5,D_F1,0}, {DP|S_FLIPX,DP|S_FLIPX,0}};
static const FamidashDeco deco_short_left = {2, 16, {0,8,0}, {-4,-4,0}, {D_F5,D_F1,0}, {DP,DP,0}};
static const FamidashDeco deco_long_up = {2, 16, {4,4,0}, {0,16,0}, {D_CF,D_C9,0}, {DP|S_FLIPY,DP|S_FLIPY,0}};
static const FamidashDeco deco_medium_up = {2, 16, {4,4,0}, {0,16,0}, {D_CF,D_CB,0}, {DP|S_FLIPY,DP|S_FLIPY,0}};
static const FamidashDeco deco_short_up = {1, 16, {4,0,0}, {0,0,0}, {D_CD,0,0}, {DP|S_FLIPY,0,0}};
static const FamidashDeco deco_chain_up = {2, 16, {4,4,0}, {16,0,0}, {D_D7,D_D5,0}, {DP|S_FLIPY,DP|S_FLIPY,0}};
static const FamidashDeco deco_medium_right = {3, 24, {0,8,16}, {-4,-4,-4}, {D_F1,D_F7,D_F5}, {DP|S_FLIPX,DP|S_FLIPX,DP|S_FLIPX}};
static const FamidashDeco deco_medium_left = {3, 24, {0,8,16}, {-4,-4,-4}, {D_F5,D_F7,D_F1}, {DP,DP,DP}};

const FamidashDeco * const famidash_deco_table[64] = {
    [42] = &deco_long_light, [43] = &deco_medium_light,
    [44] = &deco_short_light, [45] = &deco_chain,
    [46] = &deco_spike_g1, [47] = &deco_spike_g2,
    [48] = &deco_spike_g3, [49] = &deco_spike_g4,
    [50] = &deco_diamond, [51] = &deco_diamond_half,
    [52] = &deco_question, [53] = &deco_exclamation,
    [54] = &deco_arrow, [55] = &deco_x,
    [56] = &deco_short_right, [57] = &deco_short_left,
    [58] = &deco_long_up, [59] = &deco_medium_up,
    [60] = &deco_short_up, [61] = &deco_chain_up,
    [62] = &deco_medium_right, [63] = &deco_medium_left
};
