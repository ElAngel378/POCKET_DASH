#ifndef FAMIDASH_SPRITES_H
#define FAMIDASH_SPRITES_H

#include <stdint.h>
#include <gbdk/metasprites.h>
#include "assets.h"

#define FAMIDASH_SPRITE_TILE_BASE 160
#define FAMIDASH_SPRITE_TILE_COUNT 88
#define MIRROR_PORTAL_ENTER_TILE 48
#define MIRROR_PORTAL_EXIT_TILE 64
#define CHAIN_BLOCK_TILE 80

extern const uint8_t famidash_sprites_tiles[FAMIDASH_SPRITE_TILE_COUNT * 16];

#define FAMIDASH_DECO_TILE_COUNT 36
extern const uint8_t famidash_deco_tiles[FAMIDASH_DECO_TILE_COUNT * 16];

void load_famidash_sprite_tiles(void) BANKED;

extern const metasprite_t famidash_cube_portal[];
extern const metasprite_t famidash_ship_portal[];
extern const metasprite_t famidash_ball_portal[];
extern const metasprite_t famidash_gravity_down[];
extern const metasprite_t famidash_gravity_up[];
extern const metasprite_t famidash_yellow_pad[];
extern const metasprite_t famidash_yellow_pad_up[];
extern const metasprite_t famidash_blue_pad[];
extern const metasprite_t famidash_blue_pad_up[];
extern const metasprite_t famidash_yellow_orb[];
extern const metasprite_t famidash_blue_orb[];
extern const metasprite_t famidash_pink_orb[];
extern const metasprite_t famidash_pink_pad[];
extern const metasprite_t * const famidash_sprite_table[38];

#define DP (S_PAL(1) | S_BANK)
/* Decoration tile pairs in VRAM Bank 1 (CGB only) */
#define D_CF 0
#define D_C9 2
#define D_CB 4
#define D_CD 6
#define D_D5 8
#define D_D7 10
#define D_D9 12
#define D_DB 14
#define D_DD 16
#define D_DF 18
#define D_E1 20
#define D_E3 22
#define D_E5 24
#define D_E7 26
#define D_ED 28
#define D_F5 30
#define D_F1 32
#define D_F7 34

extern const metasprite_t famidash_deco_45[];

typedef struct {
    uint8_t count;
    uint8_t width;
    int8_t x[3];
    int8_t y[3];
    uint8_t tile[3];
    uint8_t props[3];
} FamidashDeco;

extern const FamidashDeco * const famidash_deco_table[64];

#endif
