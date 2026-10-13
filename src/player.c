#pragma bank 12

#include "player.h"
#include "collision.h"

static const uint8_t mod6_table[24] = {
    0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5
};

void player_init(Player* p, uint16_t start_x, int16_t start_y) BANKED {
    p->world_x = start_x;
    p->world_y.w = (uint16_t)start_y << 8;
    p->vel_y.w = 0;
    p->on_ground = 0;
    p->dead = 0;
    p->anim_timer = 0;
    p->anim_frame = 0;
    p->gravity_flipped = 0;
    p->mode = MODE_CUBE;
    p->reversed = 0;
    p->last_joy = 0;
    p->ball_switched = 0;
    p->touching_orb = 0;
    p->level_complete = 0;
    p->mini = 0;
    p->sp_idx = 0;
}

static uint8_t hazard_kills(const Player* p, uint8_t col, uint8_t x_off) {
    uint8_t inner_x;
    uint8_t deadly_left;

    if (!IS_HAZARD(col)) return 0;

    if (col == COL_DEATH_LEFT || col == COL_DEATH_RIGHT) {
        inner_x = (uint8_t)(p->world_x + x_off) & 0x0F;
        // No flip in mirror mode: it mirrors the whole picture (tiles, metatiles and columns),
        // so the world, and its collision, are unchanged
        deadly_left = (col == COL_DEATH_LEFT);
        if (deadly_left) {
            if (inner_x >= 8) return 0;
        } else {
            if (inner_x < 8) return 0;
        }
    }
    return 1;
}

// Quadrant collision types (ids COL_QUAD_BASE..): per 8x8 quadrant solid / deadly.
// Low nibble = solid quadrants, high nibble = deadly quadrants.
// Quadrant bits: 1 = top-left, 2 = top-right, 4 = bottom-left, 8 = bottom-right.
// Same order as QUAD_TYPES in tools/famidash2gbdk.py.
static const uint8_t col_quads[COL_QUAD_COUNT] = {
    0x05, 0x0A, 0x01, 0x02, 0x04, 0x08, 0x09, 0x06, 0x07, 0x0B, 0x0D, 0x0E, // solid
    0x14, 0x28, 0x1C, 0x2C, 0x3C, 0x3C, 0xC3, 0xC3,                         // solid + spikes
    0x40, 0x80, 0x10, 0x20, 0xC0, 0x30                                      // half spikes
};

// Saw parts: deadly where the player's drawn box (normal 16x16, mini 8x8) overlaps a circle
// around the centre of their saw (x, y relative to the part's 16x16 metatile; mirror mode
// mirrors the whole picture, so no flip).
// The radius is the drawn saw's (teeth: small ~6.5, medium ~15.5, big ~21) minus ~2, so every
// saw kills when the player is 2-3px into it, at either size and from any direction. Small
// saw = one metatile (half small saws at the top / bottom of theirs), medium = 2x2 metatiles
// (centre at their shared corner), big = 3x3 (centre in the middle one). Found through the
// box corners (saw_corner), not the hazard probes: those sit near the player's centre, so they
// found the saw only once the player was deep in it, and how deep depended on the direction.
#define SAW_R_SMALL 5
#define SAW_R_MED   14
#define SAW_R_BIG   18
static const int8_t saw_circles[COL_SAW_COUNT][3] = {
    {  8,  8, SAW_R_SMALL },   // 0x40 SMALL_SAW
    {  8, 16, SAW_R_SMALL },   // 0x41 SMALL_SAW_TOP_HALF (saw in the bottom half)
    {  8,  0, SAW_R_SMALL },   // 0x42 SMALL_SAW_BOTTOM_HALF (saw in the top half)
    { 16, 16, SAW_R_MED },     // 0x43 MED_SAW_TOP_LEFT
    {  0, 16, SAW_R_MED },     // 0x44 MED_SAW_TOP_RIGHT
    { 16,  0, SAW_R_MED },     // 0x45 MED_SAW_BOTTOM_LEFT
    {  0,  0, SAW_R_MED },     // 0x46 MED_SAW_BOTTOM_RIGHT
    {  8, 24, SAW_R_BIG },     // 0x47 BIG_SAW_TOP_MIDDLE
    {  8, -8, SAW_R_BIG },     // 0x48 BIG_SAW_BOTTOM_MIDDLE
    { 24, 24, SAW_R_BIG },     // 0x49 BIG_SAW_TOP_LEFT
    { -8, 24, SAW_R_BIG },     // 0x4A BIG_SAW_TOP_RIGHT
    { 24,  8, SAW_R_BIG },     // 0x4B BIG_SAW_MIDDLE_LEFT
    { -8,  8, SAW_R_BIG },     // 0x4C BIG_SAW_MIDDLE_RIGHT
    { 24, -8, SAW_R_BIG },     // 0x4D BIG_SAW_BOTTOM_LEFT
    { -8, -8, SAW_R_BIG },     // 0x4E BIG_SAW_BOTTOM_RIGHT
    {  8,  8, SAW_R_BIG },     // 0x4F BIG_SAW_CENTER
};
static uint8_t saw_bw, saw_bh;   // player box width - 1, height - 1 (set by player_update)

// Corner (right?, bottom?) of the player box is at (xin, y) of the metatile column col_ptr:
// 1 when that metatile is a saw part whose circle the box overlaps. A box that overlaps a saw
// always has a corner in one of its metatiles (the box is never bigger than a metatile).
static uint8_t saw_corner(const uint8_t *col_ptr, int16_t y, uint8_t xin, uint8_t right, uint8_t bottom) {
    if ((uint16_t)y & 0xFF00) return 0;
    uint8_t py8 = (uint8_t)y;
    uint8_t s = (uint8_t)(famidash_metatile_collision[col_ptr[py8 >> 4]] - COL_SAW_BASE);
    if (s >= COL_SAW_COUNT) return 0;
    // the box in the metatile's coordinates
    int8_t l = (int8_t)xin, t = (int8_t)(py8 & 15u);
    if (right) l -= (int8_t)saw_bw;
    if (bottom) t -= (int8_t)saw_bh;
    int8_t r = (int8_t)(l + saw_bw), b = (int8_t)(t + saw_bh);
    const int8_t *c = saw_circles[s];
    // distance from the circle's centre to the nearest point of the box
    int8_t cx = c[0], cy = c[1];
    uint8_t dx = (cx < l) ? (uint8_t)(l - cx) : (cx > r) ? (uint8_t)(cx - r) : 0;
    uint8_t dy = (cy < t) ? (uint8_t)(t - cy) : (cy > b) ? (uint8_t)(cy - b) : 0;
    uint8_t rad = (uint8_t)c[2];
    if (dx >= rad || dy >= rad) return 0;
    return (uint16_t)(dx * dx) + (uint16_t)(dy * dy) < (uint16_t)(rad * rad);
}

// Collision types that depend on where inside the metatile the probe is (half blocks,
// half spikes, quadrants). Kept out of inline_col_at so its 13 inlined copies in
// player_update stay small (SDCC compiles the big inlined version very slowly).
static uint8_t col_at_partial(uint8_t col, uint8_t inner_y, uint8_t xin) {
    if (col == COL_TOP) {
        if (inner_y >= 8) return COL_NONE;
    } else if (col == COL_BOTTOM) {
        if (inner_y < 8) return COL_NONE;
    } else if (col == COL_DEATH_TOP_HALF) {
        if (inner_y < 8) return COL_NONE;
        return COL_DEATH;
    } else if (col == COL_DEATH_BOTTOM_HALF) {
        if (inner_y >= 8) return COL_NONE;
        return COL_DEATH;
    } else if ((uint8_t)(col - COL_SAW_BASE) < COL_SAW_COUNT) {
        return COL_NONE;   // saws: saw_corner
    } else if ((uint8_t)(col - COL_QUAD_BASE) < COL_QUAD_COUNT) {
        uint8_t m = col_quads[(uint8_t)(col - COL_QUAD_BASE)];
        uint8_t q = 1;
        if (inner_y >= 8) q = 4;
        if (xin & 8u) q <<= 1;
        if (m & q) return COL_ALL;
        if (m & (uint8_t)(q << 4)) return COL_DEATH;
        return COL_NONE;
    }
    return col;
}

// xin: x inside the 16px metatile of the probe point, (world_x + offset) & 15
static inline uint8_t inline_col_at(const uint8_t* col_ptr, int16_t y, uint8_t xin) {
    if ((uint16_t)y & 0xFF00) {
        return (y < 0) ? COL_NONE : COL_ALL;
    }
    uint8_t py8 = (uint8_t)y;
    uint8_t col = famidash_metatile_collision[col_ptr[py8 >> 4]];
    // Fast path for the common cases (air, plain spikes, solid blocks)
    if (col < COL_TOP || col == COL_ALL) return col;
    return col_at_partial(col, py8 & 0x0F, xin);
}

// Mini size spikes, as in Famidash (bg_coll_spikes): a spike only kills inside a small core of
// its metatile, not the whole 16x16 like the normal size check below. The mini box is 8px wide,
// so the whole-tile rule killed it ~18px around a spike instead of Famidash's ~7px.
// xin, inner y: probe position inside the metatile. Ranges are inclusive.
static uint8_t mini_spike_at(const uint8_t *col_ptr, int16_t y, uint8_t xin) {
    if ((uint16_t)y & 0xFF00) return 0;
    uint8_t py8 = (uint8_t)y;
    uint8_t col = famidash_metatile_collision[col_ptr[py8 >> 4]];
    uint8_t iy = py8 & 0x0F;
    switch (col) {
        case COL_DEATH:              // full spikes: x 4..8, y 4..11
            return (uint8_t)(xin - 4u) <= 4u && (uint8_t)(iy - 4u) <= 7u;
        case COL_DEATH_TOP:          // Famidash COL_DEATH_TOP: x 5..7, y < 6
        case COL_DEATH_BOTTOM_HALF:
            return (uint8_t)(xin - 5u) <= 2u && iy < 6u;
        case COL_DEATH_BOTTOM:       // Famidash COL_DEATH_BOTTOM: x 5..7, y > 10
        case COL_DEATH_TOP_HALF:
            return (uint8_t)(xin - 5u) <= 2u && iy > 10u;
        case COL_DEATH_LEFT:         // x < 6, y 6..8
            return xin < 6u && (uint8_t)(iy - 6u) <= 2u;
        case COL_DEATH_RIGHT:        // x >= 10, y 6..8
            return xin >= 10u && (uint8_t)(iy - 6u) <= 2u;
    }
    // half spikes / spike blocks: their deadly quadrants
    if ((uint8_t)(col - COL_QUAD_BASE) < COL_QUAD_COUNT)
        return col_at_partial(col, iy, xin) == COL_DEATH;
    return 0;
}

// off: x offset of the probe point inside the player box
#define COL_AT(off, y) inline_col_at(GET_COL_FAST(off), (int16_t)(y), (uint8_t)(wx + (off)) & 15u)
#define MINI_SPIKE(off, y) mini_spike_at(GET_COL_FAST(off), (int16_t)(y), (uint8_t)(wx + (off)) & 15u)

uint8_t player_update(
        Player* p,
        uint8_t joy,
        const uint8_t* collision_columns,
        uint16_t map_h
) BANKED {
    if (p->dead) return 1;
    if (p->level_complete) return 0;

    uint8_t mini = p->mini;
    uint8_t was_on_ground = p->on_ground;

    // Acceleration & gravity
    if (p->mode == MODE_SHIP) {
        // Famidash ship_movement() 4-state model (60fps, non-mini):
        //   holding + falling  -> SHIP_GRAVITY_HOLD_FALL (52)
        //   holding + rising   -> SHIP_GRAVITY_BASE (42)
        //   release + rising   -> SHIP_GRAVITY_AFTER_HOLD (50)
        //   release + falling  -> SHIP_GRAVITY (34)
        // "falling" is motion toward the rest surface (vel_y > 0 normally,
        // vel_y < 0 when gravity_flipped). Holding always steers away from
        // the rest surface in the local gravity frame.
        uint8_t ship_falling = (p->gravity_flipped) ? (p->vel_y.w < 0) : (p->vel_y.w > 0);
        int16_t accel;
        if (joy & (J_A | J_UP)) {
            if (mini) accel = ship_falling ? -MINI_SHIP_GRAVITY_HOLD_FALL : MINI_SHIP_THRUST;
            else      accel = ship_falling ? -SHIP_GRAVITY_HOLD_FALL : SHIP_THRUST;
        } else {
            if (mini) accel = ship_falling ? MINI_SHIP_GRAVITY : MINI_SHIP_GRAVITY_AFTER_HOLD;
            else      accel = ship_falling ? SHIP_GRAVITY : SHIP_GRAVITY_AFTER_HOLD;
        }
        if (p->gravity_flipped) accel = (int16_t)-accel;
        p->vel_y.w += accel;
    } else {
        uint16_t gravity_val;
        int16_t max_fall = MAX_FALL_SPEED;
        if (p->mode == MODE_BALL) {
            gravity_val = mini ? MINI_BALL_GRAVITY : BALL_GRAVITY;
            if (mini) max_fall = MINI_BALL_MAX_FALL;
        } else {
            gravity_val = mini ? MINI_GRAVITY : GRAVITY;
        }
        if (p->gravity_flipped) {
            p->vel_y.w -= gravity_val;
            if (p->vel_y.w < -max_fall) p->vel_y.w = -max_fall;
        } else {
            p->vel_y.w += gravity_val;
            if (p->vel_y.w > max_fall) p->vel_y.w = max_fall;
        }
    }

    // Buffer jump input while airborne
    if ((joy & J_A) && !(p->last_joy & J_A) && !p->on_ground) {
        p->orb_buffered = 1;
    }

    // Movement & collision resolution
    uint16_t prev_y = p->world_y.w;
    p->world_y.w += p->vel_y.w;

    // Famidash clamps ship velocity AFTER position integration
    // (common_gravity_routine then clamp in ship_movement)
    if (p->mode == MODE_SHIP) {
        // Famidash hard-codes these for both sizes (its mini SHIP_MAX_FALLSPEED is unused)
        int16_t vup = SHIP_MAX_VEL_UP;
        int16_t vdown = SHIP_MAX_VEL_DOWN;
        if (p->gravity_flipped) {
            if (p->vel_y.w < -vup) p->vel_y.w = -vup;
            if (p->vel_y.w > vdown) p->vel_y.w = vdown;
        } else {
            if (p->vel_y.w > vup) p->vel_y.w = vup;
            if (p->vel_y.w < -vdown) p->vel_y.w = -vdown;
        }
    }

    // Out of bounds check: ceiling underflow (going above top of screen/map)
    if (p->vel_y.w < 0 && p->world_y.w > prev_y) {
        p->world_y.w = 0;
        p->dead = 1;
        return 1;
    }

    // Out of bounds check: floor overflow
    if (p->vel_y.w > 0 && p->world_y.w < prev_y) {
        p->world_y.w = 0xFF00;
        p->dead = 1;
        return 1;
    }

    uint8_t py = p->world_y.b.h;
    const uint8_t* c0 = collision_columns;
    const uint8_t* c1 = collision_columns + 16;
    uint8_t wx = (uint8_t)p->world_x;
    uint8_t x_mod_16 = wx & 0x0F;
    uint8_t threshold = 16 - x_mod_16;
    // Box: x .. x+box_r (inclusive), y+box_top .. y+box_bot-1
    uint8_t box_r   = mini ? MINI_BOX_RIGHT : PLAYER_SIZE;
    uint8_t box_top = mini ? MINI_BOX_TOP : 0;
    uint8_t box_bot = mini ? MINI_BOX_BOTTOM : 16;
    // Max penetration still treated as landing on a front edge. Mini falls as fast as the normal
    // size (6px a frame), so it gets the same 6, and no centre test (its centre is only 4px
    // above its feet): Famidash lands it on any of the 3 floor points, however deep.
    uint8_t pen_max = 6;

#define GET_COL_FAST(off) ((off) < threshold ? c0 : c1)

    p->on_ground = 0;

    // Floor collision
    if (p->vel_y.w >= 0) {
        int16_t foot_y = py + box_bot;
        uint8_t hit_off = 0;
        uint8_t hit_col = COL_AT(0, foot_y);
        uint8_t hit_front_only = 0;
        if (!IS_SOLID(hit_col)) {
            hit_off = box_r >> 1;
            hit_col = COL_AT(box_r >> 1, foot_y);
            if (!IS_SOLID(hit_col)) {
                hit_off = box_r;
                hit_col = COL_AT(box_r, foot_y);
                if (IS_SOLID(hit_col)) hit_front_only = 1;
            }
        }
        if (IS_SOLID(hit_col)) {
            uint8_t block_top_y = (uint8_t)(foot_y & ~15);
            // A quadrant solid hit in its lower half has its top 8px lower
            if ((foot_y & 15) >= 8 && hit_col == COL_ALL && !IS_SOLID(COL_AT(hit_off, foot_y - 8))) {
                block_top_y += 8;
            }
            if (hit_front_only && ((!mini && (uint8_t)(py + box_top + ((box_bot - box_top) >> 1)) >= block_top_y) || (uint8_t)(foot_y - block_top_y) > pen_max)) {
                // Front edge struck wall side
            } else if (!p->gravity_flipped || p->mode == MODE_SHIP) {
                if (hit_col == COL_BOTTOM) {
                    p->world_y.b.h = block_top_y + 8 - box_bot;
                } else {
                    p->world_y.b.h = block_top_y - box_bot;
                }
                p->world_y.b.l = 0;
                p->vel_y.w = 0;
                if (!p->gravity_flipped) {
                    p->on_ground = 1;
                    p->orb_buffered = 0;
                }
            }
        }
    }

    // Ceiling collision
    if (p->vel_y.w < 0) {
        int16_t head_y = py + box_top;
        uint8_t hit_off = 0;
        uint8_t hit_col = COL_AT(0, head_y);
        uint8_t hit_front_only = 0;
        if (!IS_SOLID(hit_col)) {
            hit_off = box_r >> 1;
            hit_col = COL_AT(box_r >> 1, head_y);
            if (!IS_SOLID(hit_col)) {
                hit_off = box_r;
                hit_col = COL_AT(box_r, head_y);
                if (IS_SOLID(hit_col)) hit_front_only = 1;
            }
        }
        if (IS_SOLID(hit_col)) {
            uint8_t block_bottom_y = (uint8_t)((head_y & ~15) + 16);
            // A quadrant solid hit in its upper half has its bottom 8px higher
            if ((head_y & 15) < 8 && hit_col == COL_ALL && !IS_SOLID(COL_AT(hit_off, head_y + 8))) {
                block_bottom_y -= 8;
            }
            if (hit_front_only && ((!mini && (uint8_t)(py + box_top + ((box_bot - box_top) >> 1)) <= block_bottom_y) || (uint8_t)(block_bottom_y - head_y) > pen_max)) {
                // Front edge struck ceiling side
            } else if (p->gravity_flipped || p->mode == MODE_SHIP) {
                if (hit_col == COL_TOP) {
                    p->world_y.b.h = (head_y & ~15) + 8 - box_top;
                } else {
                    p->world_y.b.h = block_bottom_y - box_top;
                }
                p->world_y.b.l = 0;
                p->vel_y.w = 0;
                if (p->gravity_flipped) {
                    p->on_ground = 1;
                    p->orb_buffered = 0;
                }
            }
        }
    }

    // Front wall collision
    py = p->world_y.b.h;
    // Famidash: mini cube probes the wall 2px above / 3px below its centre
    uint8_t front_y = mini ? ((p->mode == MODE_CUBE) ? (p->gravity_flipped ? 10 : 5) : 7) : (PLAYER_SIZE >> 1);
    uint8_t front_center = COL_AT(box_r - 1, py + front_y);
    if (IS_SOLID(front_center)) {
        p->dead = 1;
        return 1;
    }

    if (mini) {
        // Famidash's mini spike points: bg_coll_floor_spikes (x+3 / x+5 at the box top + 0 and
        // bottom - 2), bg_coll_death (centre x+3, y+7) and the front side point (x+8)
        if (MINI_SPIKE(3, py + 4) || MINI_SPIKE(5, py + 4) ||
            MINI_SPIKE(3, py + 9) || MINI_SPIKE(5, py + 9) ||
            MINI_SPIKE(3, py + 7) || MINI_SPIKE(MINI_BOX_RIGHT, py + front_y)) {
            p->dead = 1; return 1;
        }
    } else {
        // Hazard collision: 4 points around the box centre
        uint8_t hx0 = PLAYER_HBOX;
        uint8_t hx1 = PLAYER_SIZE - PLAYER_HBOX;
        uint8_t hy0 = PLAYER_HBOX;
        uint8_t hy1 = PLAYER_SIZE - PLAYER_HBOX;
        uint8_t hz = COL_AT(hx0, py + hy0);
        if (IS_HAZARD(hz) && hazard_kills(p, hz, hx0)) { p->dead = 1; return 1; }
        hz = COL_AT(hx1, py + hy0);
        if (IS_HAZARD(hz) && hazard_kills(p, hz, hx1)) { p->dead = 1; return 1; }
        hz = COL_AT(hx0, py + hy1);
        if (IS_HAZARD(hz) && hazard_kills(p, hz, hx0)) { p->dead = 1; return 1; }
        hz = COL_AT(hx1, py + hy1);
        if (IS_HAZARD(hz) && hazard_kills(p, hz, hx1)) { p->dead = 1; return 1; }
    }

    // Saws: the 4 corners of the drawn player box (see saw_corner)
    if (saw_on) {
        saw_bw = mini ? 7 : 15;
        saw_bh = saw_bw;
        int16_t ty = (int16_t)py + (mini ? 3 : 0);
        int16_t by = ty + saw_bh;
        uint8_t xr = (uint8_t)(wx + saw_bw) & 15u;
        const uint8_t *cr = GET_COL_FAST(saw_bw);
        // inline check first: saw_corner (5 arguments on the stack) only runs on a saw part
#define ON_SAW(cp, y) (!((uint16_t)(y) & 0xFF00) && \
        (uint8_t)(famidash_metatile_collision[(cp)[(uint8_t)(y) >> 4]] - COL_SAW_BASE) < COL_SAW_COUNT)
        if ((ON_SAW(c0, ty) && saw_corner(c0, ty, x_mod_16, 0, 0)) ||
            (ON_SAW(cr, ty) && saw_corner(cr, ty, xr, 1, 0)) ||
            (ON_SAW(c0, by) && saw_corner(c0, by, x_mod_16, 0, 1)) ||
            (ON_SAW(cr, by) && saw_corner(cr, by, xr, 1, 1))) {
            p->dead = 1; return 1;
        }
#undef ON_SAW
    }

    // Falling off a ledge / block: start rotation at frame 1 if resting flat (frame 0)
    if (was_on_ground && !p->on_ground && p->mode == MODE_CUBE) {
        if (p->anim_frame == 0 || mod6_table[p->anim_frame] == 0) {
            p->anim_frame = (uint8_t)((p->anim_frame + 1u) % 24u);
            p->anim_timer = 0;
        }
    }

    // Ground jump handling
    if (p->on_ground) {
        if (joy & J_A) {
            if (p->mode == MODE_CUBE) {
                int16_t jf = mini ? MINI_JUMP_FORCE : JUMP_FORCE;
                p->vel_y.w = (p->gravity_flipped) ? -jf : jf;
                p->on_ground = 0;
            } else if (p->mode == MODE_BALL && !p->ball_switched) {
                p->gravity_flipped = !p->gravity_flipped;
                int16_t sv = mini ? MINI_BALL_SWITCH_VEL : BALL_SWITCH_VEL;
                p->vel_y.w = (p->gravity_flipped) ? -sv : sv;
                p->on_ground = 0;
                p->ball_switched = 1;
            }
        }
    }
    if (!(joy & J_A)) p->ball_switched = 0;

    // Animation Update
    if (p->on_ground && p->mode != MODE_BALL) {
        // Landed mid-spin: settle onto the NEAREST square side, in half steps (7.5 deg, 12 a
        // quarter turn; the half step is anim_timer >= 11, as drawn in the air), 2 a frame.
        // Past 45 deg -> finish the spin forwards; before it -> roll backwards instead.
        uint8_t s = (uint8_t)((p->anim_frame << 1) + (p->anim_timer >= 11u ? 1u : 0u));
        uint8_t q = (uint8_t)((mod6_table[p->anim_frame] << 1) + (s & 1u));
        if (q != 0) {
            if (q >= 6) s += (q == 11) ? 1 : 2;
            else s -= (q == 1) ? 1 : 2;
            if (s >= 48) s -= 48;
            p->anim_frame = (uint8_t)(s >> 1);
            p->anim_timer = (s & 1u) ? 11 : 0;
        } else {
            p->anim_timer = 0;
        }
    } else {
        p->anim_timer += 10;
        if (p->anim_timer >= 21) {
            p->anim_timer -= 21;
            p->anim_frame++;
            if (p->anim_frame >= 24) p->anim_frame = 0;
        }
    }

    // Bottom bounds check (falling below playable map)
    if (p->world_y.b.h >= (uint8_t)((map_h << 4) - 8)) {
        p->dead = 1;
        return 1;
    }

    p->last_joy = joy;
    return 0;
}

#undef GET_COL_FAST
