#pragma bank 12

#include "player.h"
#include "collision.h"

uint8_t level_max_fall_7;   // player.h

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
    p->ufo_orbed = 0;
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

// Famidash bg_coll_spikes for the spike blocks / half spikes (quadrant types COL_QUAD_BASE + 12..25,
// tools/famidash2gbdk.py QUAD_TYPES): where in the metatile a probe dies. y: 0 = top half
// (y < 8), 1 = bottom half, 2 = y > 10 (col_death_bottom_routine); x lo..hi inclusive, of x & 7
// when bit 7 of lo is set (the two-spike types)
#define SPK_X7 0x80
static const uint8_t quad_spike_zone[COL_QUAD_COUNT - 12][3] = {
    { 0, 2, 5 },             // LEFT_SPIKE_BLOCK
    { 0, 10, 12 },           // RIGHT_SPIKE_BLOCK
    { 0, 2, 5 },             // BOTTOM_LEFT_SPIKE
    { 0, 10, 12 },           // BOTTOM_RIGHT_SPIKE
    { 0, SPK_X7 | 2, 5 },    // BOTTOM_SPIKES
    { 0, 7, 10 },            // BOTTOM_CENTER_SPIKE
    { 2, 5, 7 },             // TOP_SPIKES
    { 2, 5, 7 },             // TOP_CENTER_SPIKE
    { 1, 2, 5 },             // DOWN_LEFT_SPIKE
    { 1, 10, 12 },           // DOWN_RIGHT_SPIKE
    { 0, 2, 5 },             // UP_LEFT_SPIKE
    { 0, 10, 12 },           // UP_RIGHT_SPIKE
    { 1, SPK_X7 | 2, 5 },    // DOWN_BOTH_SPIKES
    { 0, SPK_X7 | 2, 5 },    // UP_BOTH_SPIKES
};

// Spikes as in Famidash (bg_coll_spikes), for both sizes: a spike only kills a probe point
// inside a small core of its metatile (a whole 16x16 spike tile, or the whole half of a side
// spike, killed far around it).
// xin, inner y: probe position inside the metatile. Ranges are inclusive.
static uint8_t spike_at(const uint8_t *col_ptr, int16_t y, uint8_t xin) {
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
    // spike blocks / half spikes
    uint8_t q = (uint8_t)(col - COL_QUAD_BASE - 12u);
    if (q < COL_QUAD_COUNT - 12) {
        const uint8_t *z = quad_spike_zone[q];
        if (z[0] == 0 ? iy >= 8u : z[0] == 1 ? iy < 8u : iy <= 10u) return 0;
        uint8_t lo = z[1];
        if (lo & SPK_X7) { xin &= 7u; lo &= (uint8_t)~SPK_X7; }
        return (uint8_t)(xin - lo) <= (uint8_t)(z[2] - lo);
    }
    return 0;
}

// off: x offset of the probe point inside the player box
#define COL_AT(off, y) inline_col_at(GET_COL_FAST(off), (int16_t)(y), (uint8_t)(wx + (off)) & 15u)
#define SPIKE(off, y) spike_at(GET_COL_FAST(off), (int16_t)(y), (uint8_t)(wx + (off)) & 15u)

uint8_t player_update(
        Player* p_arg,
        uint8_t joy,
        const uint8_t* collision_columns,
        uint16_t map_h
) BANKED {
    // always &player: fixed addresses instead of pointer + offset for every field
    (void)p_arg;
#define p (&player)
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
        int16_t max_fall = level_max_fall_7 ? MAX_FALL_SPEED_7 : MAX_FALL_SPEED;
        if (p->mode == MODE_BALL) {
            gravity_val = mini ? MINI_BALL_GRAVITY : BALL_GRAVITY;
            max_fall = mini ? MINI_BALL_MAX_FALL : MAX_FALL_SPEED;
        } else if (p->mode == MODE_UFO) {
            gravity_val = UFO_GRAVITY;
            max_fall = mini ? MINI_UFO_MAX_FALL : UFO_MAX_FALL;
        } else {
            gravity_val = mini ? MINI_GRAVITY : GRAVITY;
        }
        // Famidash common_gravity_routine: no clamp; past the max fall speed the gravity pulls
        // the other way for a frame (the speed hovers around it)
        if (p->gravity_flipped) {
            if (p->vel_y.w < -max_fall) p->vel_y.w += gravity_val;
            else p->vel_y.w -= gravity_val;
        } else {
            if (p->vel_y.w > max_fall) p->vel_y.w -= gravity_val;
            else p->vel_y.w += gravity_val;
        }
    }

    // Buffer jump input while airborne (not the UFO: its presses are its jumps)
    if ((joy & J_A) && !(p->last_joy & J_A) && !p->on_ground && p->mode != MODE_UFO) {
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
    // Floors and ceilings as in Famidash (bg_coll_D / bg_coll_U): any of the 3 points (x, x+w/2,
    // x+w) in a block lands, however deep (up to 15 px: it climbs a block edge it falls onto)

#define GET_COL_FAST(off) ((off) < threshold ? c0 : c1)

    p->on_ground = 0;

    // Floor collision
    if (p->vel_y.w >= 0) {
        int16_t foot_y = py + box_bot;
        uint8_t hit_off = 0;
        uint8_t hit_col = COL_AT(0, foot_y);
        if (!IS_SOLID(hit_col)) {
            hit_off = box_r >> 1;
            hit_col = COL_AT(box_r >> 1, foot_y);
            if (!IS_SOLID(hit_col)) {
                hit_off = box_r;
                hit_col = COL_AT(box_r, foot_y);
            }
        }
        if (IS_SOLID(hit_col)) {
            uint8_t block_top_y = (uint8_t)(foot_y & ~15);
            // A bottom half block / quadrant solid hit in its lower half has its top 8px lower
            if (hit_col == COL_BOTTOM ||
                ((foot_y & 15) >= 8 && hit_col == COL_ALL && !IS_SOLID(COL_AT(hit_off, foot_y - 8)))) {
                block_top_y += 8;
            }
            if (!p->gravity_flipped || p->mode == MODE_SHIP || p->mode == MODE_UFO) {
                p->world_y.b.h = block_top_y - box_bot;
                p->world_y.b.l = 0;
                p->vel_y.w = 0;
                if (!p->gravity_flipped) {
                    p->on_ground = 1;
                    p->orb_buffered = 0;
                }
            }
        }
    }

    // Ceiling collision. Famidash bg_coll_U probes 1 px below its box top (our py + 2; mini
    // py + 5): the box needs a 15 px gap (mini 7) between floor and ceiling, as in Famidash. The
    // cube and ball rest with that point just below the ceiling (cube_eject / ball_eject), the
    // ship and UFO with it in the ceiling's last row (ufo_ship_eject: one more pixel).
    uint8_t head_off = (uint8_t)(box_top + (mini ? 1u : 2u));
    if (p->vel_y.w < 0) {
        int16_t head_y = py + head_off;
        uint8_t hit_off = 0;
        uint8_t hit_col = COL_AT(0, head_y);
        if (!IS_SOLID(hit_col)) {
            hit_off = box_r >> 1;
            hit_col = COL_AT(box_r >> 1, head_y);
            if (!IS_SOLID(hit_col)) {
                hit_off = box_r;
                hit_col = COL_AT(box_r, head_y);
            }
        }
        if (IS_SOLID(hit_col)) {
            uint8_t block_bottom_y = (uint8_t)((head_y & ~15) + 16);
            // A top half block / quadrant solid hit in its upper half has its bottom 8px higher
            if (hit_col == COL_TOP ||
                ((head_y & 15) < 8 && hit_col == COL_ALL && !IS_SOLID(COL_AT(hit_off, head_y + 8)))) {
                block_bottom_y -= 8;
            }
            if (p->gravity_flipped || p->mode == MODE_SHIP || p->mode == MODE_UFO) {
                p->world_y.b.h = (uint8_t)(block_bottom_y - head_off -
                                           ((p->mode == MODE_SHIP || p->mode == MODE_UFO) ? 1u : 0u));
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
    // Normal size: Famidash's player y is our py + 1 (its feet probe y + 15 is our py + 16, its head
    // probe y + 1 our py + 2), in both gravities; mini: its y is our py. Its centre probe (y + 7)
    // is our py + 8, so a half block in front at the cube's height is hit.
    uint8_t front_y = mini ? ((p->mode == MODE_CUBE) ? (p->gravity_flipped ? 10 : 5) : 7) : 8;
    // Famidash bg_coll_R: the point at x + width (mini x + 8), at the box centre height
    uint8_t front_center = COL_AT(box_r, py + front_y);
    if (IS_SOLID(front_center)) {
        p->dead = 1;
        return 1;
    }
    if (mini) {
        // Famidash's mini spike points: bg_coll_floor_spikes (x+3 / x+5 at the box top + 0 and
        // bottom - 2), bg_coll_death (centre x+3, y+7) and the front side point (x+8)
        if (SPIKE(3, py + 4) || SPIKE(5, py + 4) ||
            SPIKE(3, py + 9) || SPIKE(5, py + 9) ||
            SPIKE(3, py + 7) || SPIKE(MINI_BOX_RIGHT, py + front_y)) {
            p->dead = 1; return 1;
        }
    } else {
        // Famidash's spike points for the 15x15 box (its top is our py + 1, see front_y):
        // bg_coll_floor_spikes (x+3 / x+12 at y+2 and y+13), bg_coll_death (centre x+6, y+7) and
        // the front side point (bg_coll_R: x+15, y+7)
        int16_t fy = (int16_t)py + 1;
        if (SPIKE(3, fy + 2) || SPIKE(12, fy + 2) ||
            SPIKE(3, fy + 13) || SPIKE(12, fy + 13) ||
            SPIKE(6, fy + 7) || SPIKE(15, fy + 7)) {
            p->dead = 1; return 1;
        }
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

    // UFO (Famidash ufo_movement): every new press jumps, on the ground or in the air, unless an
    // orb took it (sp_draw.c sets ufo_orbed before this runs)
    if (p->mode == MODE_UFO && (joy & J_A) && !(p->last_joy & J_A) && !p->ufo_orbed) {
        int16_t jv = mini ? MINI_UFO_JUMP_VEL : UFO_JUMP_VEL;
        p->vel_y.w = (p->gravity_flipped) ? -jv : jv;
        p->on_ground = 0;
    }
    p->ufo_orbed = 0;

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
#undef p
}

#undef GET_COL_FAST
