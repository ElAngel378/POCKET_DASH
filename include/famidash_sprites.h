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


#define FAMIDASH_DECO_TILE_COUNT 40

// 1.4 objects. VRAM bank 0 (DMG + CGB):
#define UFO_TILE              16   // old UFO (1.5), unused: the UFO is streamed like the ship (gameplay.c PL_UFO)
#define MINI_UFO_TILE         18   // mini UFO: image in the top half, like MINI_PLAYER_TILE_BASE
#define MINI_PLAYER_TILE_BASE 116  // 5 8x16 pairs: cube 0/22/45 deg, ship, ball
#define MINI_PORTAL_TILE_A    126  // mini/growth portal pair 0 ...
#define MINI_PORTAL_TILE_B    248  // ... pairs 1..4 (sprite tiles 248..255 are unused otherwise)
// Coin (tools/make_coin_tiles.py): 11 tile pairs. CGB: VRAM bank 1 tiles 200..221. DMG: pairs
// 0..7 at 144..159 (BG slots 144..159 unused, see DMG_BG_SLOTS), 8..9 at 192..195 (pink pad art,
// an icon on DMG), 10 at 246..247 (chain block art, not drawn on DMG)
#define COIN_TILE_BASE        200
#define DMG_COIN_TILE_BASE    144
#define DMG_COIN_TILE_B       192
#define DMG_COIN_TILE_C       246
extern const uint8_t famidash_deco_tiles[FAMIDASH_DECO_TILE_COUNT * 16];

// deco_cloud: use the DECOCLOUD decoration art (Famidash picks the deco type per level)
// All gameplay sprite tiles from levels/chr_data/sprite_tiles.png (tools/build_sprite_tiles.py)
void load_gameplay_sprite_tiles(uint8_t deco_cloud) BANKED;
// Only the sprite tiles that share VRAM with background tiles 128..159
void reload_bg_shared_sprite_tiles(void) BANKED;

extern const metasprite_t famidash_cube_portal[];
extern const metasprite_t famidash_ship_portal[];
extern const metasprite_t famidash_ball_portal[];
extern const metasprite_t famidash_ufo_portal[];
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
extern const metasprite_t famidash_pink_pad_up[];
extern const metasprite_t * const famidash_sprite_table[38];

// Decorations: the player's palette 0 (colour 2 = the cube's colour 1, like Famidash draws them
// in its player palette), their colour 1 loaded as 3 (black there): tools/build_sprite_tiles.py
#define DP (S_PAL(0) | S_BANK)
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
#define D_E9 36
#define D_EB 38
#define D_PB 40   // pulsing ball: CGB bank 1 tiles 250..255 (7 / 5 / 3 px), see deco_tile_sets in sp_draw.c
#define D_GC 42   // GDA's pulsing circle, heart, square (objects 39..41): CGB bank 1 tiles 82..99
#define D_GH 44   // (tools/make_gda_pulse_tiles.py), 16 / 12 / 8 px with the rods
#define D_GS 46

extern const metasprite_t famidash_deco_45[];

typedef struct {
    uint8_t count;
    uint8_t width;
    int8_t x[3];
    int8_t y[3];
    uint8_t tile[3];
    uint8_t props[3];
} FamidashDeco;

#define FAMIDASH_DECO_TABLE_SIZE 75
extern const FamidashDeco * const famidash_deco_table[FAMIDASH_DECO_TABLE_SIZE];

#endif
