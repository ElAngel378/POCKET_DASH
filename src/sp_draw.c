#pragma bank 10

#include <gb/gb.h>
#include <gbdk/font.h>

#include "sp_draw.h"
#include "gameplay.h"
#include "player.h"
#include "assets.h"
#include "famidash_sprites.h"
#include "famidash_bg.h"

#define DEBUG_MODE
#include "famidash_metatiles.h"

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

#define BG_TRIGGER_LEAD_TILES 10
#define BG_TRIGGER_LEAD_PX    ((BG_TRIGGER_LEAD_TILES) << 4)

extern const unsigned char FontPusab[];
#define FONT_PUSAB_START 0xD0

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
            if (cache->activated[i]) {
                uint8_t o = cache->obj[i];
                if (o >= 128) continue; // Any activated trigger can be safely pruned
                if (_cpu != CGB_TYPE && !is_dmg_portal(o)) continue;
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

void process_sprite_logic(
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

uint8_t draw_sprites(
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
