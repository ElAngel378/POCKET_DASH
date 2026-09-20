#pragma bank 10

#include <gb/gb.h>
#include <gbdk/font.h>
#include <gbdk/console.h>
#include <stdio.h>

#include "gameplay.h"
#include "player.h"
#include "assets.h"
#include "icon1.h"
#include "ship1.h"
#include "ball.h"
#include "famidash_sprites.h"
#include "gbc_palettes.h"
#include "../levels/chr_data/chr_gb.h"

#define DEBUG_MODE
#include "famidash_metatiles.h"
#include "hUGEDriver.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "level_complete_sfx.h"
#include "fade.h"
#include "death_effect.h"
#include "save_manager.h"
#include "pause_buttons.h"

extern const uint8_t chr_gb_cgb_tiles[];
extern const uint8_t chr_gb_cgb_tiles_rev[];

extern const unsigned char FontPusab[];
#define FONT_PUSAB_START 0xD0

#define BKG_MT_W 16
#define BKG_MT_H 16
#define VIEW_MT_W 10
#define VIEW_MT_H 9

// ID Mappings for SP Layer Logic
#define OBJ_CUBE_PORTAL   0
#define OBJ_SHIP_PORTAL   1
#define OBJ_BALL_PORTAL   2
#define OBJ_ORB_BLUE      5
#define OBJ_ORB_PINK      6
#define OBJ_GRAVITY_DOWN  8
#define OBJ_GRAVITY_UP    9
#define OBJ_PAD_YELLOW    10
#define OBJ_ORB_YELLOW    11
#define OBJ_PAD_YELLOW_UP 12
#define OBJ_PAD_BLUE      13
#define OBJ_PAD_BLUE_UP   14
#define OBJ_PAD_PINK      37
#define OBJ_LEVEL_END     15
#define OBJ_MIRROR_PORTAL 126
#define OBJ_MIRROR_EXIT   121
#define OBJ_PORTAL_DN_HORIZ_DN  16
#define OBJ_PORTAL_DN_HORIZ_UP  17
#define OBJ_PORTAL_UP_HORIZ_DN  18
#define OBJ_PORTAL_UP_HORIZ_UP  19

// Level end animation timings
#define LEVEL_END_SHAKE_FRAMES 120  // Screen shake duration (2s at 60fps)
#define LEVEL_END_PULL_FRAMES   72  // Magnetic pull towards end trigger (~1.2s)
#define LEVEL_END_OVERSHOOT_PX  14  // Y overshoot amplitude in pixels

#define END_ANIM_INACTIVE 0
#define END_ANIM_PULL     1
#define END_ANIM_SHAKE    2

// Precomputed reverse-easing (quadratic ease-in: 256 * (t/72)^2)
static const uint16_t level_end_ease_in[73] = {
      0,   0,   0,   0,   1,   1,   2,   2,   3,   4,
      5,   6,   7,   8,  10,  11,  13,  14,  16,  18,
     20,  22,  24,  26,  28,  31,  33,  36,  39,  42,
     44,  47,  51,  54,  57,  60,  64,  68,  71,  75,
     79,  83,  87,  91,  96, 100, 104, 109, 114, 119,
    123, 128, 134, 139, 144, 149, 155, 160, 166, 172,
    178, 184, 190, 196, 202, 209, 215, 222, 228, 235,
    242, 249, 256
};

// Precomputed Y overshoot arc (parabola: 255 * 4 * (t/72) * (1 - t/72))
static const uint8_t level_end_arc[73] = {
      0,  14,  28,  41,  54,  66,  78,  90, 101, 112,
    122, 132, 142, 151, 160, 168, 176, 184, 191, 198,
    205, 211, 216, 222, 227, 231, 235, 239, 242, 245,
    248, 250, 252, 253, 254, 255, 255, 255, 254, 253,
    252, 250, 248, 245, 242, 239, 235, 231, 227, 222,
    216, 211, 205, 198, 191, 184, 176, 168, 160, 151,
    142, 132, 122, 112, 101,  90,  78,  66,  54,  41,
     28,  14,   0
};

static uint8_t end_anim_state;
static uint8_t end_anim_frame;
static uint8_t end_shake_timer;
static uint8_t end_trigger_requested;
static uint16_t end_trigger_obj_x;
static uint16_t end_trigger_obj_y;
static uint16_t locked_scroll_px;
static uint16_t locked_cam_py;
static int16_t end_start_x;
static int16_t end_start_y;
static int16_t end_target_x;
static int16_t end_target_y;

// Scroll speed in 8.8 fixed point (pixels per frame)
// Example: 3.0 = 768, 3.5 = 896, 4.0 = 1024
// 714 = 708 x 714/708 = famidash 60fps speed corrected for the Game Boy's
// ~59.7fps refresh. Must match the physics rescale factor in include/player.h
// so vertical trajectories stay aligned with scroll (see "Better ship").
#define SCROLL_SPEED_FP 714

#define CAM_Y_TOP_ZONE 20
#define CAM_Y_BOTTOM_ZONE 100

#define BG_TRIGGER_LEAD_TILES 10
#define BG_TRIGGER_LEAD_PX    ((BG_TRIGGER_LEAD_TILES) << 4)

// Index (0-3) of the default theme in bg_pals:
// bottom row (light), column 0 (gray). This is used at level start and after death.
extern uint8_t music_ready;

/**
 * NES Master Palette mapped to GBC 15-bit RGB.
 * 64 colors (4 rows of 16).
 */
static const uint16_t nes_master_palette[64] = {
    // Row 0 (0x00 - 0x0F): Dark
    RGB(10, 10, 10), RGB(0, 0, 17), RGB(1, 2, 18), RGB(6, 0, 17),
    RGB(8, 0, 12), RGB(11, 0, 6), RGB(20, 0, 0), RGB(7, 3, 0),
    RGB(4, 5, 0), RGB(1, 7, 0), RGB(0, 8, 0), RGB(0, 7, 0),
    RGB(0, 6, 7), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0),

    // Row 1 (0x10 - 0x1F): Medium/Dark
    RGB(22, 22, 22), RGB(0, 7, 19), RGB(6, 6, 29), RGB(11, 3, 28),
    RGB(27, 0, 25), RGB(20, 2, 12), RGB(27, 0, 0), RGB(15, 7, 0),
    RGB(10, 11, 0), RGB(5, 14, 0), RGB(1, 15, 0), RGB(0, 14, 5),
    RGB(0, 12, 15), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0),

    // Row 2 (0x20 - 0x2F): Bright
    RGB(31, 31, 31), RGB(7, 23, 31), RGB(15, 15, 31), RGB(22, 12, 31),
    RGB(31, 7, 21), RGB(31, 11, 22), RGB(31, 13, 12), RGB(31, 15, 0),
    RGB(27, 20, 0), RGB(14, 24, 0), RGB(0, 28, 0), RGB(7, 25, 13),
    RGB(0, 29, 27), RGB(7, 7, 7), RGB(0, 0, 0), RGB(0, 0, 0),

    // Row 3 (0x30 - 0x3F): Pale
    RGB(31, 31, 31), RGB(21, 25, 31), RGB(23, 23, 31), RGB(26, 22, 31),
    RGB(31, 21, 31), RGB(31, 21, 26), RGB(31, 22, 22), RGB(31, 27, 15),
    RGB(25, 26, 15), RGB(22, 27, 15), RGB(21, 28, 18), RGB(19, 28, 22),
    RGB(20, 26, 28), RGB(20, 20, 20), RGB(0, 0, 0), RGB(0, 0, 0)
};

/**
 * Vibrant GBC Palettes moved local for maximum DMG performance
 */
static const uint16_t vibrant_palette_default[16] = {
    RGB(0, 7, 19), RGB(0, 0, 17), RGB(0, 0, 0), RGB(31, 31, 31), // palette 0
    RGB(0, 7, 19), RGB(0, 0, 17), RGB(0, 7, 19), RGB(31, 31, 31), // palette 1
    RGB(0, 7, 19), RGB(0, 0, 17), RGB(0, 0, 0), RGB(15, 31, 0), // palette 2: bg spikes accent (lime, matches cube)
    RGB(0, 7, 19), RGB(0, 0, 17), RGB(0, 0, 0), RGB(0, 0, 0)  // palette 3
};

static palette_color_t famidash_bg_palettes[16];

static palette_color_t famidash_darker(palette_color_t color) {
    return RGB((color & 0x1Fu) * 3u / 4u,
               ((color >> 5) & 0x1Fu) * 3u / 4u,
               ((color >> 10) & 0x1Fu) * 3u / 4u);
}

static void famidash_reset_bg_palettes(void) {
    uint8_t i;
    for (i = 0; i != 16; i++) famidash_bg_palettes[i] = vibrant_palette_default[i];
    fade_set_bkg_palette(0, 4, famidash_bg_palettes);
}

static void famidash_apply_bg_trigger(uint8_t color_id) {
    palette_color_t color;

    if (color_id == 31u) color = RGB(0, 29, 27); /* FamiDash $9F: Use Aqua as default player color */
    else if (color_id == 46u) {                   /* FamiDash $AE: Ground Color 2 Trigger */
        color = RGB(0, 28, 0); /* Neon Green */
        famidash_bg_palettes[6] = color;
        famidash_bg_palettes[5] = famidash_darker(color);
        fade_set_bkg_palette(1, 1, &famidash_bg_palettes[4]);
        return;
    } else {
        // Fast local lookup
        color = nes_master_palette[color_id & 0x3Fu];
    }

    famidash_bg_palettes[0] = color;
    famidash_bg_palettes[4] = color;
    famidash_bg_palettes[8] = color;
    famidash_bg_palettes[12] = color;
    color = famidash_darker(color);
    famidash_bg_palettes[1] = color;
    // famidash_bg_palettes[5] is preserved for ground darker color
    famidash_bg_palettes[9] = color;
    famidash_bg_palettes[13] = color;
    fade_set_bkg_palette(0, 4, famidash_bg_palettes);
}

static void famidash_apply_g_trigger(uint8_t color_id) {
    palette_color_t color;

    if (color_id == 31u) color = RGB(0, 29, 27); /* FamiDash $9F: Aqua */
    else color = nes_master_palette[color_id & 0x3Fu];

    famidash_bg_palettes[6] = color;
    famidash_bg_palettes[5] = famidash_darker(color);
    fade_set_bkg_palette(1, 1, &famidash_bg_palettes[4]);
}

static inline uint8_t is_dmg_portal(uint8_t o) {
    return (o <= 2) || (o == 8) || (o == 9) || (o >= 16 && o <= 19) || (o == 121) || (o == 126);
}
static uint8_t sp_has_portals = 0;

void sp_cache_reset(SpCache *cache, uint16_t *stream_idx) {
    uint8_t i;
    *stream_idx = 0;
    sp_has_portals = 0;
    for (i = 0; i < MAX_ACTIVE_SP_OBJECTS; i++) cache->active[i] = 0;
}

void sp_cache_update(const Level *l, uint16_t cam_px,
                     SpCache *cache, uint16_t *stream_idx) {
    uint8_t i;
    uint8_t count = 0;
    uint8_t sp_bank = l->sp_bank;
    const SpDef *sp_list = (_cpu == CGB_TYPE || !l->sp_list_dmg) ? l->sp_list : l->sp_list_dmg;

    /* Retire old entries and compact in a single pass */
    for (i = 0; i < MAX_ACTIVE_SP_OBJECTS; i++) {
        if (cache->active[i]) {
            // DMG: If already activated and not a portal, prune immediately so it frees cache space
            if (_cpu != CGB_TYPE && cache->activated[i]) {
                uint8_t o = cache->obj[i];
                if (o >= 128 || !is_dmg_portal(o)) continue;
            }
            if (cache->px[i] + 48u >= cam_px) {
                if (count != i) {
                    cache->obj[count] = cache->obj[i];
                    cache->px[count] = cache->px[i];
                    cache->py[count] = cache->py[i];
                    cache->active[count] = 1;
                    cache->activated[count] = cache->activated[i];
                }
                count++;
            }
        }
    }
    for (i = count; i < MAX_ACTIVE_SP_OBJECTS; i++) cache->active[i] = 0;

    sp_cache_load(sp_bank, sp_list, cam_px, cache, stream_idx, l->map_height);

    sp_has_portals = 0;
    for (i = 0; i < MAX_ACTIVE_SP_OBJECTS; i++) {
        if (!cache->active[i]) break;
        uint8_t o = cache->obj[i];
        if (o < 128 && is_dmg_portal(o)) {
            sp_has_portals = 1;
            break;
        }
    }
}

// OAM sprite drawing routines
// 2x1 metasprite (orbs, pads)
static uint8_t draw_oam_2x1(const metasprite_t* meta, uint8_t tile_base, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];

    if (!reversed) {
        *oam++ = sy; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy; *oam++ = sx + 8; *oam++ = meta->dtile + tile_base; *oam++ = meta->props;
    } else {
        *oam++ = sy; *oam++ = sx + 8; *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX;
    }
    return 2;
}

// 2x3 metasprite (gravity portals)
static uint8_t draw_oam_2x3(const metasprite_t* meta, uint8_t tile_base, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];

    if (!reversed) {
        *oam++ = sy;    *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy;    *oam++ = sx + 8; *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy+16; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy+16; *oam++ = sx + 8; *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy+32; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy+32; *oam++ = sx + 8; *oam++ = meta->dtile + tile_base; *oam++ = meta->props;
    } else {
        *oam++ = sy;    *oam++ = sx + 8; *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy;    *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy+16; *oam++ = sx + 8; *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy+16; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy+32; *oam++ = sx + 8; *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy+32; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX;
    }
    return 6;
}

// 3x3 metasprite (cube/ship portals)
static uint8_t draw_oam_3x3(const metasprite_t* meta, uint8_t tile_base, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];

    if (!reversed) {
        *oam++ = sy;    *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy;    *oam++ = sx+8;   *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy;    *oam++ = sx+16;  *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;

        *oam++ = sy+16; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy+16; *oam++ = sx+8;   *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy+16; *oam++ = sx+16;  *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;

        *oam++ = sy+32; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy+32; *oam++ = sx+8;   *oam++ = meta->dtile + tile_base; *oam++ = meta->props; meta++;
        *oam++ = sy+32; *oam++ = sx+16;  *oam++ = meta->dtile + tile_base; *oam++ = meta->props;
    } else {
        *oam++ = sy;    *oam++ = sx+16;  *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy;    *oam++ = sx+8;   *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy;    *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;

        *oam++ = sy+16; *oam++ = sx+16;  *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy+16; *oam++ = sx+8;   *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy+16; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;

        *oam++ = sy+32; *oam++ = sx+16;  *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy+32; *oam++ = sx+8;   *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX; meta++;
        *oam++ = sy+32; *oam++ = sx;     *oam++ = meta->dtile + tile_base; *oam++ = meta->props ^ S_FLIPX;
    }
    return 9;
}

// Horizontal gravity portal (48px wide ring)
static uint8_t draw_oam_horizontal_portal(uint8_t obj, uint8_t tile_base, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t pal = (obj >= 18) ? S_PAL(3) : S_PAL(2);
    uint8_t flip_v = (obj == 17 || obj == 19) ? S_FLIPY : 0;
    uint8_t base_props = pal | flip_v;

    // PT_4D = 36, PT_4F = 38, PT_51 = 40
    uint8_t t0 = 36 + tile_base;
    uint8_t t1 = 38 + tile_base;
    uint8_t t2 = 40 + tile_base;

    if (!reversed) {
        *oam++ = sy; *oam++ = sx;      *oam++ = t0; *oam++ = base_props;
        *oam++ = sy; *oam++ = sx + 8;  *oam++ = t1; *oam++ = base_props;
        *oam++ = sy; *oam++ = sx + 16; *oam++ = t2; *oam++ = base_props;
        *oam++ = sy; *oam++ = sx + 24; *oam++ = t2; *oam++ = base_props | S_FLIPX;
        *oam++ = sy; *oam++ = sx + 32; *oam++ = t1; *oam++ = base_props | S_FLIPX;
        *oam++ = sy; *oam++ = sx + 40; *oam++ = t0; *oam++ = base_props | S_FLIPX;
    } else {
        // In mirror mode, the 48px portal extends to the left of sx: [sx - 40 .. sx]
        *oam++ = sy; *oam++ = sx - 40; *oam++ = t0; *oam++ = base_props;
        *oam++ = sy; *oam++ = sx - 32; *oam++ = t1; *oam++ = base_props;
        *oam++ = sy; *oam++ = sx - 24; *oam++ = t2; *oam++ = base_props;
        *oam++ = sy; *oam++ = sx - 16; *oam++ = t2; *oam++ = base_props | S_FLIPX;
        *oam++ = sy; *oam++ = sx - 8;  *oam++ = t1; *oam++ = base_props | S_FLIPX;
        *oam++ = sy; *oam++ = sx;      *oam++ = t0; *oam++ = base_props | S_FLIPX;
    }
    return 6;
}

inline static uint8_t draw_oam_deco(const FamidashDeco *deco, uint8_t tile_base,
                             uint8_t oam_idx, uint8_t sx, uint8_t sy,
                             uint8_t reversed) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t count = deco->count;
    const int8_t *dx = deco->x;
    const int8_t *dy = deco->y;
    const uint8_t *dt = deco->tile;
    const uint8_t *dp = deco->props;

    if (!reversed) {
        *oam++ = sy + dy[0]; *oam++ = sx + dx[0]; *oam++ = dt[0] + tile_base; *oam++ = dp[0];
        if (count > 1) {
            *oam++ = sy + dy[1]; *oam++ = sx + dx[1]; *oam++ = dt[1] + tile_base; *oam++ = dp[1];
            if (count > 2) {
                *oam++ = sy + dy[2]; *oam++ = sx + dx[2]; *oam++ = dt[2] + tile_base; *oam++ = dp[2];
            }
        }
    } else {
        uint8_t rx = sx + deco->width - 8;
        *oam++ = sy + dy[0]; *oam++ = rx - dx[0]; *oam++ = dt[0] + tile_base; *oam++ = dp[0] ^ S_FLIPX;
        if (count > 1) {
            *oam++ = sy + dy[1]; *oam++ = rx - dx[1]; *oam++ = dt[1] + tile_base; *oam++ = dp[1] ^ S_FLIPX;
            if (count > 2) {
                *oam++ = sy + dy[2]; *oam++ = rx - dx[2]; *oam++ = dt[2] + tile_base; *oam++ = dp[2] ^ S_FLIPX;
            }
        }
    }
    return count;
}

// 4 columns x 2 rows (8 8x16 hardware sprites = 32x32 pixels)
static uint8_t draw_oam_mirror_portal(uint8_t obj, uint8_t tile_base, uint8_t oam_idx,
                                      uint8_t sx, uint8_t sy, uint8_t reversed) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t t_base = (obj == OBJ_MIRROR_PORTAL) ? (tile_base + MIRROR_PORTAL_ENTER_TILE)
                                                : (tile_base + MIRROR_PORTAL_EXIT_TILE);
    uint8_t pal = (obj == OBJ_MIRROR_PORTAL) ? S_PAL(6) : S_PAL(7);
    uint8_t flip = (obj == OBJ_MIRROR_PORTAL) ? reversed : (!reversed);

    if (!flip) {
        // Row 0 (Top 16px)
        *oam++ = sy;      *oam++ = sx;      *oam++ = t_base + 0;  *oam++ = pal;
        *oam++ = sy;      *oam++ = sx + 8;  *oam++ = t_base + 2;  *oam++ = pal;
        *oam++ = sy;      *oam++ = sx + 16; *oam++ = t_base + 4;  *oam++ = pal;
        *oam++ = sy;      *oam++ = sx + 24; *oam++ = t_base + 6;  *oam++ = pal;
        // Row 1 (Bottom 16px)
        *oam++ = sy + 16; *oam++ = sx;      *oam++ = t_base + 8;  *oam++ = pal;
        *oam++ = sy + 16; *oam++ = sx + 8;  *oam++ = t_base + 10; *oam++ = pal;
        *oam++ = sy + 16; *oam++ = sx + 16; *oam++ = t_base + 12; *oam++ = pal;
        *oam++ = sy + 16; *oam++ = sx + 24; *oam++ = t_base + 14; *oam++ = pal;
    } else {
        uint8_t props = pal | S_FLIPX;
        // Row 0 (Top 16px, columns reversed)
        *oam++ = sy;      *oam++ = sx + 24; *oam++ = t_base + 0;  *oam++ = props;
        *oam++ = sy;      *oam++ = sx + 16; *oam++ = t_base + 2;  *oam++ = props;
        *oam++ = sy;      *oam++ = sx + 8;  *oam++ = t_base + 4;  *oam++ = props;
        *oam++ = sy;      *oam++ = sx;      *oam++ = t_base + 6;  *oam++ = props;
        // Row 1 (Bottom 16px, columns reversed)
        *oam++ = sy + 16; *oam++ = sx + 24; *oam++ = t_base + 8;  *oam++ = props;
        *oam++ = sy + 16; *oam++ = sx + 16; *oam++ = t_base + 10; *oam++ = props;
        *oam++ = sy + 16; *oam++ = sx + 8;  *oam++ = t_base + 12; *oam++ = props;
        *oam++ = sy + 16; *oam++ = sx;      *oam++ = t_base + 14; *oam++ = props;
    }
    return 8;
}

static void process_sprite_logic(
        SpCache *cache, uint16_t cam_px,
        Player* p, uint8_t joy, uint8_t* target_bg_idx
) {
    uint8_t i;
    uint16_t px = p->world_x;
    uint16_t py = p->world_y.b.h;

    uint16_t p_front = px + 15u;
    uint16_t p_bottom = py + PLAYER_SIZE;
    uint16_t p_feet = py + PLAYER_SIZE;

    for (i = 0; i < MAX_ACTIVE_SP_OBJECTS; i++) {
        if (!cache->active[i]) break;
        if (cache->activated[i]) continue;

        uint16_t obj_x = cache->px[i];
        if (obj_x > cam_px + 176u) break;

        uint8_t obj = cache->obj[i];

        if (obj == OBJ_LEVEL_END) {
            if (end_anim_state == END_ANIM_INACTIVE && px >= (obj_x - 180u)) {
                end_trigger_requested = 1;
                end_trigger_obj_x = obj_x;
                end_trigger_obj_y = cache->py[i];
                cache->activated[i] = 1;
            }
            continue;
        }

        if (obj != OBJ_LEVEL_END && obj_x > px + BG_TRIGGER_LEAD_PX) break;

        if (cache->activated[i]) continue;
        if (obj_x + 48u < px) continue;

        if (obj >= 38 && obj < 64) continue;

        if (obj >= 128 && obj <= 175) {
            if (px + BG_TRIGGER_LEAD_PX >= obj_x) {
                uint8_t pal_idx = (uint8_t)(obj - 128);

                if (_cpu == CGB_TYPE) {
                    famidash_apply_bg_trigger(pal_idx);
                }

                if (pal_idx < 16) {
                    *target_bg_idx = (pal_idx == 15) ? 3 : 2;
                } else if (pal_idx < 32) {
                    *target_bg_idx = 1;
                } else {
                    *target_bg_idx = 0;
                }

                cache->activated[i] = 1;
                cache->active[i] = 0;
            }

            continue;
        }

        if (obj >= 192 && obj <= 239) {
            if (px + BG_TRIGGER_LEAD_PX >= obj_x) {
                if (_cpu == CGB_TYPE) {
                    uint8_t pal_idx = (uint8_t)(obj - 192);
                    famidash_apply_g_trigger(pal_idx);
                }
                cache->activated[i] = 1;
                cache->active[i] = 0;
            }

            continue;
        }

        if (obj_x > px + 48u) continue;

        uint16_t obj_y = cache->py[i];

        int16_t dy = (int16_t)py - (int16_t)obj_y;
        if (dy > 50 || dy < -20) continue;

        if (obj >= 16 && obj <= 19) {
            // 48-pixel (3 tile) wide horizontal gravity portal
            if (obj_x <= p_front && px <= obj_x + 48u) {
                if (py <= obj_y + 14u && p_bottom >= obj_y) {
                    if (!cache->activated[i]) {
                        uint8_t target_flipped = (obj >= 18);
                        if (p->gravity_flipped != target_flipped) {
                            p->gravity_flipped = target_flipped;
                            p->vel_y.w = (p->vel_y.w >> 1); // Halve velocity
                        }
                        cache->activated[i] = 1;
                    }
                }
            }
        } else if (obj_x <= p_front && px <= obj_x + 15) {
            switch (obj) {
                case OBJ_CUBE_PORTAL:
                case OBJ_SHIP_PORTAL:
                case OBJ_BALL_PORTAL:
                    // FamiDash mode portal: height 52px (obj_y - 2 to obj_y + 50)
                    if (py <= obj_y + 49 && p_bottom >= (obj_y - 1)) {
                        if (!cache->activated[i]) {
                            if (obj == OBJ_CUBE_PORTAL) p->mode = MODE_CUBE;
                            else if (obj == OBJ_SHIP_PORTAL) p->mode = MODE_SHIP;
                            else p->mode = MODE_BALL;
                            p->vel_y.w = (p->vel_y.w >> 1); // Halve velocity on portal entry
                            cache->activated[i] = 1;
                        }
                    }
                    break;

                case OBJ_GRAVITY_DOWN:
                case OBJ_GRAVITY_UP:
                    // FamiDash gravity portal: height 40px (obj_y + 4 to obj_y + 44)
                    if (py <= obj_y + 43 && p_bottom >= (obj_y + 5)) {
                        if (!cache->activated[i]) {
                            uint8_t target_flipped = (obj == OBJ_GRAVITY_UP);
                            if (p->gravity_flipped != target_flipped) {
                                p->gravity_flipped = target_flipped;
                                p->vel_y.w = (p->vel_y.w >> 1) + (p->vel_y.w >> 3);
                            }
                            cache->activated[i] = 1;
                        }
                    }
                    break;

                case OBJ_PAD_YELLOW:
                case OBJ_PAD_PINK:
                case OBJ_PAD_BLUE:
                case OBJ_PAD_YELLOW_UP:
                case OBJ_PAD_BLUE_UP:
                {
                    uint8_t is_ceiling = (obj == OBJ_PAD_YELLOW_UP || obj == OBJ_PAD_BLUE_UP);
                    uint16_t pad_top = is_ceiling ? obj_y : (obj_y + 13);
                    uint16_t pad_bot = is_ceiling ? (obj_y + 3) : (obj_y + 16);

                    if (py <= pad_bot && p_bottom >= pad_top) {
                        if (!cache->activated[i]) {
                            cache->activated[i] = 1;
                            if (obj == OBJ_PAD_BLUE) {
                                if (!p->gravity_flipped) {
                                    p->gravity_flipped = 1;
                                    p->vel_y.w = -BLUE_PAD_FORCE;
                                    p->on_ground = 0;
                                }
                            } else if (obj == OBJ_PAD_BLUE_UP) {
                                if (p->gravity_flipped) {
                                    p->gravity_flipped = 0;
                                    p->vel_y.w = BLUE_PAD_FORCE;
                                    p->on_ground = 0;
                                }
                            } else if (obj == OBJ_PAD_PINK) {
                                int16_t force = (p->mode == MODE_BALL) ? BALL_PINK_PAD : PINK_PAD_FORCE;
                                p->vel_y.w = (p->gravity_flipped) ? -force : force;
                                p->on_ground = 0;
                            } else {
                                int16_t force = (p->mode == MODE_BALL) ? BALL_YELLOW_PAD : PAD_JUMP_FORCE;
                                p->vel_y.w = (p->gravity_flipped) ? -force : force;
                                p->on_ground = 0;
                            }
                        }
                    }
                    break;
                }

                case OBJ_ORB_YELLOW:
                case OBJ_ORB_PINK:
                case OBJ_ORB_BLUE:
                {
                    if (joy & J_A) {
                        if ((!(p->last_joy & J_A) || p->orb_buffered) && py <= obj_y + 16 && p_feet >= obj_y) {
                            if (!cache->activated[i]) {
                                cache->activated[i] = 1;
                                p->orb_buffered = 0; // Clear buffer after hit
                                if (obj == OBJ_ORB_BLUE) {
                                    p->gravity_flipped = !p->gravity_flipped;
                                    int16_t force = (p->mode == MODE_BALL) ? BLUE_ORB_FORCE : BLUE_PAD_FORCE;
                                    p->vel_y.w = (p->gravity_flipped) ? -force : force;
                                } else if (obj == OBJ_ORB_PINK) {
                                    int16_t force = (p->mode == MODE_BALL) ? BALL_PINK_ORB : MAGENTA_JUMP_FORCE;
                                    p->vel_y.w = (p->gravity_flipped) ? -force : force;
                                } else {
                                    int16_t force = (p->mode == MODE_BALL) ? BALL_YELLOW_ORB : JUMP_FORCE;
                                    p->vel_y.w = (p->gravity_flipped) ? -force : force;
                                }
                                p->on_ground = 0;
                            }
                        }
                    }
                    break;
                }


                case OBJ_MIRROR_PORTAL:
                case OBJ_MIRROR_EXIT:
                    if (py <= obj_y + 45 && p_bottom >= (obj_y - 1)) {
                        if (!cache->activated[i]) {
                            p->reversed = (obj == OBJ_MIRROR_PORTAL) ? 1 : 0;
                            cache->activated[i] = 1;
                        }
                    }
                    break;
            }
        } else if (obj_x > p_front + 16) {
            break;
        }
    }
}

static uint8_t draw_sprites(
        SpCache *cache, uint16_t cam_px, uint16_t cam_py,
        uint8_t reversed, uint8_t oam_start
) {
    uint8_t i;
    uint8_t dist_x, screen_x, screen_y;
    uint8_t deco_drawn = 0;
    // Limit active decorations (4 on DMG, 12 on CGB) to keep 60 FPS
    uint8_t deco_max = (_cpu == CGB_TYPE) ? 12 : 4;

    // Skip drawing if no portals exist in cache on DMG
    if (_cpu != CGB_TYPE && !sp_has_portals) return oam_start;

    for (i = 0; i < MAX_ACTIVE_SP_OBJECTS && oam_start < MAX_HARDWARE_SPRITES - 2; i++) {
        if (!cache->active[i]) break;

        uint16_t obj_x = cache->px[i];
        if (obj_x > cam_px + 176u) break;

        uint8_t obj = cache->obj[i];
        if (obj == OBJ_LEVEL_END || obj >= 128) continue;

        if (_cpu != CGB_TYPE && (obj >= 128 || !is_dmg_portal(obj))) continue;

        dist_x = (uint8_t)obj_x - (uint8_t)cam_px;

        if (!reversed) {
            if (dist_x > 136 && dist_x < 224) continue;
            screen_x = dist_x + PLAYER_SCREEN_X + 8;
        } else {
            if (dist_x > 136 && dist_x < 208) continue;
            screen_x = MIRROR_PLAYER_SCREEN_X - dist_x + 8;
        }

        screen_y = ((uint8_t)cache->py[i] - (uint8_t)cam_py) + 16;

        if (screen_y > 160 && screen_y < 208) continue;

        if (obj == OBJ_MIRROR_PORTAL || obj == OBJ_MIRROR_EXIT) {
            if (oam_start > MAX_HARDWARE_SPRITES - 8) break;
            oam_start += draw_oam_mirror_portal(obj, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
            continue;
        }

        if (obj >= 38) {
            if (deco_drawn >= deco_max) continue;
            
            if (_cpu == CGB_TYPE && obj < 64) {
                const FamidashDeco *deco = famidash_deco_table[obj];
                if (deco) {
                    if (oam_start > MAX_HARDWARE_SPRITES - deco->count) break;
                    deco_drawn++;
                    oam_start += draw_oam_deco(deco, FAMIDASH_SPRITE_TILE_BASE,
                                               oam_start, screen_x, screen_y, reversed);
                }
            }
            continue;
        }

        if (oam_start > MAX_HARDWARE_SPRITES - 9) break;
        const metasprite_t *sprite = famidash_sprite_table[obj];
        if (sprite == 0) continue;

        if (obj >= 16 && obj <= 19) {
            oam_start += draw_oam_horizontal_portal(obj, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
        } else if (obj == OBJ_CUBE_PORTAL || obj == OBJ_SHIP_PORTAL || obj == OBJ_BALL_PORTAL) {
            oam_start += draw_oam_3x3(sprite, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
        } else if (obj == OBJ_GRAVITY_DOWN || obj == OBJ_GRAVITY_UP) {
            oam_start += draw_oam_2x3(sprite, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
        } else {
            oam_start += draw_oam_2x1(sprite, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
        }
    }
    return oam_start;
}

void setup_menu_font(void) BANKED {
    set_bkg_data(FONT_PUSAB_START, 39, FontPusab);
}

void draw_text(uint8_t x, uint8_t y, const char *str) BANKED {
    uint8_t tile;
    while (*str) {
        char c = *str;
        if (c == ' ') tile = 0;
        else if (c == '%') tile = 1;
        else if (c == '/') tile = 2;
        else if (c >= '0' && c <= '9') tile = (c - '0') + 3;
        else if (c >= 'A' && c <= 'Z') tile = (c - 'A') + 13;
        else if (c >= 'a' && c <= 'z') tile = (c - 'a') + 13;
        else tile = 0;
        set_bkg_tile_xy(x++, y, FONT_PUSAB_START + tile);
        str++;
    }
}

SpCache active_sp;
uint8_t collision_columns[32];

static const Level* l;
static const uint8_t* level_tiles;
static const uint8_t* level_map;
static uint16_t level_tile_count;
static uint16_t level_map_w;
static uint16_t level_map_h;
static uint8_t level_tiles_bank;
static uint8_t level_map_bank;

static uint16_t cam_px;
static uint16_t cam_py;
static uint16_t cam_py_max;
static uint16_t loaded_r;
static uint16_t max_scroll_px;

static uint16_t scroll_acc;
static uint8_t prev_joy;
static uint8_t previous_oam_index;
static uint16_t sp_stream_idx;
static uint16_t sp_cache_col;
static uint16_t cached_collision_col;
static uint8_t prev_reversed;
static uint8_t reduce_flash;
static uint8_t target_bg_idx;
static uint8_t died;
static int16_t py;
static Player player;

static const uint8_t bg_pals[] = {
    0xE4, // 0: Normal (W:W, LG:LG, DG:DG, B:B)
    0x39, // 1: Inverse (W:LG, LG:DG, DG:B, B:W)
    0x3E, // 2: Inverse (W:DG, LG:B, DG:B, B:W)
    0x3F  // 3: Inverse (W:B, LG:B, DG:B, B:W)
};

static void reload_level_state(uint8_t idx) {
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    disable_interrupts();
    DISPLAY_OFF;

    // Reload tileset and sprite data on respawn/restart
    load_bkg_tileset(level_tiles, level_tile_count, level_tiles_bank);

    set_sprite_data(0, 8, icon1_tiles);
    set_sprite_data(8, 4, ship_tiles);
    set_sprite_data(12, 8, ball_tiles);
    init_death_effect_tiles();
    init_pause_tiles();
    load_famidash_sprite_tiles();

    cam_px = 0;
    cam_py = 112;
    scroll_acc = 0;
    loaded_r = BKG_MT_W - 1;
    target_bg_idx = 0;
    end_anim_state = END_ANIM_INACTIVE;
    end_anim_frame = 0;
    end_shake_timer = 0;
    end_trigger_requested = 0;
    player_init(&player, 0, 240);
    sp_cache_reset(&active_sp, &sp_stream_idx);
    sp_cache_col = 0xFFFF;
    previous_oam_index = MAX_HARDWARE_SPRITES;
    cached_collision_col = 0xFFFF;
    move_bkg(0, (uint8_t)cam_py);
    BGP_REG = bg_pals[0];
    if (_cpu == CGB_TYPE) {
        famidash_reset_bg_palettes();
    }
    fill_scroll_bg(level_map, level_map_w, level_map_bank, 0);
    DISPLAY_ON;
    if (level_songs[idx]) {
        init_music_banked(level_songs[idx], song_bank[idx], l->timer_divider);
        current_song_bank = song_bank[idx];
        TAC_REG = 0x04;
        music_ready = 1;
    }
    enable_interrupts();
}

void play_level(uint8_t idx) BANKED {
    l = game_levels[idx];
    level_tiles = l->tiles;
    level_map = l->map;
    level_tile_count = l->tile_count;
    level_map_w = l->map_width;
    level_map_h = l->map_height;
    level_tiles_bank = BANK(chr_gb);
    level_map_bank = l->map_bank;
    if (_cpu == CGB_TYPE) level_tiles = chr_gb_cgb_tiles;

    cam_px = 0;
    cam_py = 112;
    cam_py_max = (level_map_h << 4);
    if (cam_py_max > 144u) cam_py_max -= 144u;
    else cam_py_max = 0;
    loaded_r = BKG_MT_W - 1;
    max_scroll_px = ((level_map_w - VIEW_MT_W) << 4);

    target_bg_idx = 0;
    player_init(&player, 0, 240);

    DISPLAY_OFF;
    load_bkg_tileset(level_tiles, level_tile_count, level_tiles_bank);
    set_sprite_data(0, 8, icon1_tiles);
    set_sprite_data(8, 4, ship_tiles);
    set_sprite_data(12, 8, ball_tiles);
    init_death_effect_tiles();
    init_pause_tiles();
    load_famidash_sprite_tiles();
    move_bkg(0, (uint8_t)cam_py);
    fill_scroll_bg(level_map, level_map_w, level_map_bank, 0);

    if (_cpu == CGB_TYPE) {
        famidash_reset_bg_palettes();
        fade_set_sprite_palette(0, 8, gbc_sprite_palettes);
    }

    fade_set_dmg_palettes(bg_pals[0], bg_pals[0], bg_pals[0]);
    fade_set_black();

    SPRITES_8x16;
    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;
    enable_interrupts();

    // Wait for the entry sound effect to finish (hiding load time behind SFX)
    while (is_sample_playing()) wait_vbl_done();
    stop_sample();

    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;

    fade_from_black(2);

    if (level_songs[idx]) {
        init_music_banked(level_songs[idx], song_bank[idx], l->timer_divider);
        current_song_bank = song_bank[idx];
        TAC_REG = 0x04;
        music_ready = 1;
    }

    scroll_acc = 0;
    prev_joy = 0;
    previous_oam_index = MAX_HARDWARE_SPRITES;
    sp_stream_idx = 0;
    sp_cache_col = 0xFFFF;
    cached_collision_col = 0xFFFF;
    prev_reversed = player.reversed;
    reduce_flash = 0;
    end_anim_state = END_ANIM_INACTIVE;
    end_anim_frame = 0;
    end_shake_timer = 0;
    end_trigger_requested = 0;
    sp_cache_reset(&active_sp, &sp_stream_idx);
    while (1) {
        uint8_t joy = joypad();
        if (joy & J_UP) joy |= J_A;

        // Pause game on Start press
        if (!player.dead && end_anim_state == END_ANIM_INACTIVE && (joy & J_START) && !(prev_joy & J_START)) {
            wait_vbl_done();

            // Pause music and mute active sound
            uint8_t saved_music_ready = music_ready;
            music_ready = 0;
            NR12_REG = 0; NR14_REG = 0x80;
            NR22_REG = 0; NR24_REG = 0x80;
            NR30_REG = 0;
            NR42_REG = 0; NR44_REG = 0x80;

            // Tint screen 1 gradient step down
            uint8_t saved_bgp = BGP_REG;
            uint8_t saved_obp0 = OBP0_REG;
            uint8_t saved_obp1 = OBP1_REG;

            uint8_t saved_scx = SCX_REG;
            uint8_t saved_scy = SCY_REG;
            uint8_t fine_scx = saved_scx & 7;
            uint8_t fine_scy = saved_scy & 7;

            // Grid-lock background
            move_bkg((uint8_t)(saved_scx - fine_scx), (uint8_t)(saved_scy - fine_scy));

            if (_cpu == CGB_TYPE) {
                fade_apply_pause_box_palettes();
                apply_pause_box_attributes(1);
                static const palette_color_t pause_pal[4] = {
                    RGB8(0, 0, 0), RGB8(0, 0, 0), RGB8(180, 215, 255), RGB8(255, 255, 255)
                };
                set_sprite_palette(7, 1, pause_pal);

                // Play Button: Vibrant golden yellow icon & rim, rich 2-tone green body
                static const palette_color_t play_btn_pal[4] = {
                    RGB8(0, 0, 0), RGB8(255, 235, 20), RGB8(80, 210, 20), RGB8(15, 110, 10)
                };
                set_sprite_palette(6, 1, play_btn_pal);

                // Menu & Restart Buttons: Electric cyan icon & rim, rich 2-tone green body
                static const palette_color_t misc_btn_pal[4] = {
                    RGB8(0, 0, 0), RGB8(30, 245, 255), RGB8(80, 210, 20), RGB8(15, 110, 10)
                };
                set_sprite_palette(5, 1, misc_btn_pal);
            } else {
                BGP_REG = dim_dmg_byte(saved_bgp, 1);
                OBP0_REG = 0x90;
                OBP1_REG = 0x1C;
            }

            OAM_item_t saved_pause_oam[40];
            for (uint8_t i = 0; i < 40; i++) {
                saved_pause_oam[i] = shadow_OAM[i];
            }

            // Offset player (0..3) into slots 27..30 with grid-lock offset
            for (uint8_t i = 0; i < 4; i++) {
                if (saved_pause_oam[i].y > 0) {
                    shadow_OAM[27 + i].y = (uint8_t)(saved_pause_oam[i].y + fine_scy);
                    shadow_OAM[27 + i].x = (uint8_t)(saved_pause_oam[i].x + fine_scx);
                    shadow_OAM[27 + i].tile = saved_pause_oam[i].tile;
                    shadow_OAM[27 + i].prop = saved_pause_oam[i].prop;
                } else {
                    shadow_OAM[27 + i].y = 0;
                }
            }

            // Offset active level sprites (4..12) into slots 31..39 with grid-lock offset
            for (uint8_t i = 4; i < 13; i++) {
                if (saved_pause_oam[i].y > 0) {
                    shadow_OAM[27 + i].y = (uint8_t)(saved_pause_oam[i].y + fine_scy);
                    shadow_OAM[27 + i].x = (uint8_t)(saved_pause_oam[i].x + fine_scx);
                    shadow_OAM[27 + i].tile = saved_pause_oam[i].tile;
                    shadow_OAM[27 + i].prop = saved_pause_oam[i].prop;
                } else {
                    shadow_OAM[27 + i].y = 0;
                }
            }

            uint8_t selected_btn = PAUSE_BTN_PLAY;
            draw_pause_menu_sprites(selected_btn);
            wait_vbl_done();

            uint8_t exit_level = 0;
            uint8_t restart_level = 0;
            while (joypad() & (J_START | J_SELECT | J_A | J_B)) wait_vbl_done();

            uint8_t p_prev_joy = 0;
            while (1) {
                wait_vbl_done();
                uint8_t p_joy = joypad();
                uint8_t p_pressed = p_joy & ~p_prev_joy;
                p_prev_joy = p_joy;

                if (p_pressed & J_LEFT) {
                    if (selected_btn == 0) selected_btn = 2;
                    else selected_btn--;
                    draw_pause_menu_sprites(selected_btn);
                } else if (p_pressed & J_RIGHT) {
                    if (selected_btn >= 2) selected_btn = 0;
                    else selected_btn++;
                    draw_pause_menu_sprites(selected_btn);
                } else if ((p_pressed & J_START) || (p_pressed & J_B)) {
                    break;
                } else if (p_pressed & J_A) {
                    if (selected_btn == PAUSE_BTN_PLAY) {
                        break;
                    } else if (selected_btn == PAUSE_BTN_MENU) {
                        exit_level = 1;
                        break;
                    } else if (selected_btn == PAUSE_BTN_RESTART) {
                        restart_level = 1;
                        break;
                    }
                } else if (p_pressed & J_SELECT) {
                    exit_level = 1;
                    break;
                }
            }

            for (uint8_t i = 0; i < 40; i++) {
                shadow_OAM[i] = saved_pause_oam[i];
            }

            if (_cpu == CGB_TYPE) {
                apply_pause_box_attributes(0);
                fade_restore_pause_box_palettes();
            } else {
                BGP_REG = saved_bgp;
                OBP0_REG = saved_obp0;
                OBP1_REG = saved_obp1;
            }

            // Restore fractional background scroll
            move_bkg(saved_scx, saved_scy);

            if (exit_level) {
                break;
            }

            if (restart_level) {
                while (joypad() & (J_A | J_START)) wait_vbl_done();
                reload_level_state(idx);
                prev_joy = joypad();
                if (prev_joy & J_UP) prev_joy |= J_A;
                player.last_joy = prev_joy;
                continue;
            }

            NR30_REG = 0x80;
            hUGE_reset_wave();
            music_ready = saved_music_ready;

            wait_vbl_done();

            while (joypad() & (J_A | J_START | J_B)) wait_vbl_done();

            prev_joy = joypad();
            if (prev_joy & J_UP) prev_joy |= J_A;
            player.last_joy = prev_joy;
            continue;
        }

        if (player.level_complete) {
            record_level_progress(idx, 100, 0);
            HIDE_SPRITES;
            move_bkg(0, 0);
            disable_interrupts();
            setup_menu_font();
            enable_interrupts();
            VBK_REG = 1;
            fill_bkg_rect(0, 0, 32, 32, 0x00);
            VBK_REG = 0;
            fill_bkg_rect(0, 0, 20, 18, 0x00);
            draw_text(3, 6, "LEVEL COMPLETE");
            draw_text(3, 12, "PRESS A TO EXIT");
            waitpadup();
            while (!(joypad() & J_A)) wait_vbl_done();
            break;
        }

        if ((joy & J_SELECT) && !(prev_joy & J_SELECT)) {
            break;
        }
        prev_joy = joy;

        uint16_t px_prev = cam_px >> 4;
        uint8_t needs_render = 0;
        uint16_t need_col = 0;
        uint16_t px_curr = px_prev;

        if (end_anim_state == END_ANIM_INACTIVE && cam_px < max_scroll_px) {
            scroll_acc += SCROLL_SPEED_FP;
            cam_px += scroll_acc >> 8;
            scroll_acc &= 0xFF;
            px_curr = cam_px >> 4;
            if (px_curr != px_prev) {
                uint16_t need = px_curr + VIEW_MT_W;
                if (need > loaded_r && need < level_map_w) {
                    needs_render = 1;
                    need_col = need;
                }
            }
        }

        player.world_x = cam_px;
        uint16_t sp_col = (cam_px + 8u) >> 4;
        if (sp_col != sp_cache_col) {
            sp_cache_update(l, cam_px, &active_sp, &sp_stream_idx);
            sp_cache_col = sp_col;
        }

        process_sprite_logic(&active_sp, cam_px, &player, joy, &target_bg_idx);

        if (end_trigger_requested && end_anim_state == END_ANIM_INACTIVE) {
            end_anim_state = END_ANIM_PULL;
            end_anim_frame = 0;
            locked_scroll_px = player.reversed
                ? (uint16_t)(-(int16_t)cam_px - MIRROR_PLAYER_SCREEN_X)
                : ((cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0);
            locked_cam_py = cam_py;
            end_start_x = player.reversed ? MIRROR_PLAYER_SCREEN_X : ((cam_px < PLAYER_SCREEN_X) ? (uint8_t)cam_px : PLAYER_SCREEN_X);
            end_start_y = (int16_t)player.world_y.b.h - (int16_t)cam_py;

            if (!player.reversed) {
                end_target_x = 168; // Exit off the right edge of the screen before disappearing
            } else {
                end_target_x = (int16_t)-16; // Exit off the left edge of the screen in mirror mode
            }
            int16_t ty = (int16_t)end_trigger_obj_y - (int16_t)locked_cam_py;
            if (ty < 32) ty = 40;
            if (ty > 112) ty = 80;
            end_target_y = ty;
            end_trigger_requested = 0;
        }

        if (player.reversed != prev_reversed) {
            DISPLAY_OFF;

            const uint8_t* target_tiles = player.reversed
                ? ((_cpu == CGB_TYPE) ? chr_gb_cgb_tiles_rev : l->tiles_rev)
                : level_tiles;
            load_bkg_tileset(target_tiles, level_tile_count, level_tiles_bank);

            int32_t col_start = (int32_t)(cam_px >> 4) - 4;
            if (col_start < 0) col_start = 0;
            for (uint8_t i = 0; i < 16; i++) {
                uint16_t curr_col = (uint16_t)(col_start + i);
                if (curr_col < level_map_w) {
                    uint8_t vram_slot = (uint8_t)(curr_col & 15);
                    if (player.reversed) vram_slot = (uint8_t)(-(int8_t)vram_slot & 15);
                    prepare_mt_column(curr_col, level_map, level_map_bank, player.reversed);
                    flush_mt_column(vram_slot);
                }
            }

            set_sprite_data(0, 8, icon1_tiles);
            set_sprite_data(8, 4, ship_tiles);
            set_sprite_data(12, 8, ball_tiles);
            init_pause_tiles();
            load_famidash_sprite_tiles();

            uint16_t init_scroll_px = player.reversed
                ? (uint16_t)(-(int16_t)cam_px - MIRROR_PLAYER_SCREEN_X)
                : ((cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0);
            move_bkg((uint8_t)init_scroll_px, (uint8_t)cam_py);

            SHOW_BKG;
            SHOW_SPRITES;
            SPRITES_8x16;
            DISPLAY_ON;

            loaded_r = (uint16_t)(col_start + 15);
            prev_reversed = player.reversed;
        }

        if (px_curr != cached_collision_col) {
            load_collision_columns(px_curr, level_map, level_map_w,
                                   level_map_bank, collision_columns);
            cached_collision_col = px_curr;
        }

        if (end_anim_state == END_ANIM_INACTIVE) {
            died = player_update(&player, joy, collision_columns, level_map_h);
        } else {
            died = 0;
        }

        if (end_anim_state == END_ANIM_INACTIVE) {
            if (!died) {
                py = (int16_t)player.world_y.b.h - (int16_t)cam_py;
                if (py < CAM_Y_TOP_ZONE) {
                    int16_t target_cam_py = (int16_t)player.world_y.b.h - CAM_Y_TOP_ZONE;
                    if (target_cam_py < 0) target_cam_py = 0;
                    if ((uint16_t)target_cam_py > cam_py_max) target_cam_py = (int16_t)cam_py_max;
                    cam_py = (uint16_t)target_cam_py;
                }
                else if (py > CAM_Y_BOTTOM_ZONE) {
                    int16_t target_cam_py = (int16_t)player.world_y.b.h - CAM_Y_BOTTOM_ZONE;
                    if (target_cam_py < 0) target_cam_py = 0;
                    if ((uint16_t)target_cam_py > cam_py_max) target_cam_py = (int16_t)cam_py_max;
                    cam_py = (uint16_t)target_cam_py;
                }
            }
        } else {
            cam_py = locked_cam_py;
        }

        uint16_t scroll_px;
        uint8_t sprite_x_final;
        int16_t final_py;

        if (end_anim_state == END_ANIM_INACTIVE) {
            if (player.reversed) {
                // Mirror Mode: SCX decreases as progress advances
                scroll_px = (uint16_t)(-(int16_t)cam_px - MIRROR_PLAYER_SCREEN_X);
                sprite_x_final = MIRROR_PLAYER_SCREEN_X; // Mirrored player position (112)
            } else {
                scroll_px = (cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0;
                sprite_x_final = (cam_px < PLAYER_SCREEN_X) ? (uint8_t)cam_px : PLAYER_SCREEN_X;
            }
            final_py = (int16_t)player.world_y.b.h - (int16_t)cam_py;
            if (final_py < 0) final_py = 0;
            else if (final_py > 144) final_py = 144;
        } else if (end_anim_state == END_ANIM_PULL) {
            scroll_px = locked_scroll_px;
            end_anim_frame++;
            if (end_anim_frame > LEVEL_END_PULL_FRAMES) end_anim_frame = LEVEL_END_PULL_FRAMES;

            int16_t dx = end_target_x - end_start_x;
            int16_t dy = end_target_y - end_start_y;
            uint16_t factor = level_end_ease_in[end_anim_frame];
            uint8_t arc = level_end_arc[end_anim_frame];

            int16_t cur_x = end_start_x + (int16_t)(((int32_t)dx * factor) >> 8);
            int16_t cur_y = end_start_y + (int16_t)(((int32_t)dy * factor) >> 8) - (int16_t)(((int16_t)LEVEL_END_OVERSHOOT_PX * arc) >> 8);
            if (cur_y < 8) cur_y = 8;
            sprite_x_final = (uint8_t)cur_x;
            final_py = cur_y;
            player.anim_timer += 10;
            if (player.anim_timer >= 21) {
                player.anim_timer -= 21;
                if (player.reversed) {
                    if (player.anim_frame == 0) player.anim_frame = 23;
                    else player.anim_frame--;
                } else {
                    player.anim_frame++;
                    if (player.anim_frame >= 24) player.anim_frame = 0;
                }
            }

            if (end_anim_frame >= LEVEL_END_PULL_FRAMES) {
                end_anim_state = END_ANIM_SHAKE;
                end_shake_timer = LEVEL_END_SHAKE_FRAMES;
                play_sample_with_music(BANK_LEVEL_COMPLETE_SFX, level_complete_sfx_data, LEVEL_COMPLETE_SFX_LEN);
            }
        } else {
            // END_ANIM_SHAKE
            scroll_px = locked_scroll_px;
            sprite_x_final = 0;
            final_py = 0;
        }

        // Player sprite
        uint8_t oam_index = 0;

        if (end_anim_state != END_ANIM_SHAKE) {
            if (player.mode == MODE_SHIP) {
                if (player.gravity_flipped) {
                    if (player.reversed) oam_index += move_metasprite_hvflip(ship_metasprites[0], 0, oam_index, sprite_x_final + 24, final_py + 24);
                    else oam_index += move_metasprite_hflip(ship_metasprites[0], 0, oam_index, sprite_x_final + 8, final_py + 32);
                } else {
                    if (player.reversed) oam_index += move_metasprite_vflip(ship_metasprites[0], 0, oam_index, sprite_x_final + 24, final_py + 16);
                    else oam_index += move_metasprite(ship_metasprites[0], 0, oam_index, sprite_x_final + 8, final_py + 16);
                }
            } else if (player.mode == MODE_BALL) {
                uint8_t ball_frame = (player.anim_frame >> 1) & 1;
                if (player.reversed) {
                    oam_index += move_metasprite_vflip(ball_metasprites[ball_frame], 12, oam_index, sprite_x_final + 24, final_py + 16);
                } else {
                    oam_index += move_metasprite(ball_metasprites[ball_frame], 12, oam_index, sprite_x_final + 8, final_py + 16);
                }
            } else {
                if (player.gravity_flipped) {
                    if (player.reversed) oam_index += move_metasprite_hvflip(icon1_metasprites[player.anim_frame], 0, oam_index, sprite_x_final + 24, final_py + 32);
                    else oam_index += move_metasprite_hflip(icon1_metasprites[player.anim_frame], 0, oam_index, sprite_x_final + 8, final_py + 32);
                } else {
                    if (player.reversed) oam_index += move_metasprite_vflip(icon1_metasprites[player.anim_frame], 0, oam_index, sprite_x_final + 24, final_py + 16);
                    else oam_index += move_metasprite(icon1_metasprites[player.anim_frame], 0, oam_index, sprite_x_final + 8, final_py + 16);
                }
            }
        }

        int8_t cur_shake_x = 0;
        int8_t cur_shake_y = 0;
        if (end_anim_state == END_ANIM_SHAKE) {
            if (end_shake_timer > 0) {
                end_shake_timer--;
                uint8_t r = DIV_REG;
                cur_shake_x = (int8_t)((r % 5) - 2);
                cur_shake_y = (int8_t)(((r >> 3) % 5) - 2);
                if (cur_shake_x == 0 && cur_shake_y == 0) {
                    cur_shake_x = (r & 1) ? 1 : -1;
                }
            } else {
                player.level_complete = 1;
            }
        }

        // Level sprites
        oam_index = draw_sprites(
            &active_sp, (uint16_t)((int16_t)cam_px + cur_shake_x), (uint16_t)((int16_t)cam_py + cur_shake_y),
            player.reversed, oam_index
        );
        if (oam_index < previous_oam_index) {
            uint8_t *oam_ptr = (uint8_t *)&shadow_OAM[oam_index];
            while (oam_index < previous_oam_index) {
                *oam_ptr = 0;
                oam_ptr += 4;
                oam_index++;
            }
        }
        previous_oam_index = oam_index;

        uint8_t vram_slot = 0;
        if (needs_render) {
            loaded_r = need_col;
            vram_slot = (uint8_t)(need_col & 15);
            if (player.reversed) vram_slot = (uint8_t)(-(int8_t)vram_slot & 15);
            
            prepare_mt_column(need_col, level_map, level_map_bank, player.reversed);
        }

        wait_vbl_done();
        uint8_t apply_idx = target_bg_idx;
        if (reduce_flash && (apply_idx == 1 || apply_idx == 2)) {
            apply_idx = 0;
        }
        BGP_REG = bg_pals[apply_idx];
        // Keep sprites visible on DMG during full-black flash
        if (_cpu != CGB_TYPE && apply_idx == 3) {
            OBP0_REG = bg_pals[0];
            OBP1_REG = bg_pals[0];
        } else {
            OBP0_REG = bg_pals[apply_idx];
            OBP1_REG = bg_pals[apply_idx];
        }
        uint8_t final_scx = (uint8_t)((int16_t)scroll_px + cur_shake_x);
        uint8_t final_scy = (uint8_t)((int16_t)cam_py + cur_shake_y);
        move_bkg(final_scx, final_scy);

        if (needs_render) {
            flush_mt_column(vram_slot);
        }

        if (died) {
            record_level_progress_from_cam(idx, cam_px, max_scroll_px);
            play_death_animation(sprite_x_final, (uint8_t)final_py, (uint8_t)scroll_px, (uint8_t)cam_py);
            NR52_REG = 0x80;
            NR51_REG = 0xFF;
            NR50_REG = 0x77;
            reload_level_state(idx);
        }
    }

    music_ready = 0;
    TAC_REG = 0x00;
    play_sample(BANK_SFX_DATA, quit_sound_data, QUIT_SOUND_LEN);
    fade_to_black(2);
    while (is_sample_playing()) wait_vbl_done();
    stop_sample();

    HIDE_SPRITES;
    move_bkg(0, 0);
    waitpadup();
    disable_interrupts();
    setup_menu_font();
    enable_interrupts();
    redraw = 1;
}
