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
    p->on_slope = 0;
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

// Box types (ids COL_BOX_BASE..): solid inside these rectangles of the metatile, as in GDA's
// col_type_lookup. Per type two rectangles x0, x1, y0, y1 (x0 <= x < x1, y0 <= y < y1); an
// empty second one has x0 = x1 = 0.
static const uint8_t col_boxes[COL_BOX_COUNT][8] = {
    { 2, 14,  0,  8,   0,  0, 0,  0 },   // SLAB_MED_TOP
    { 2, 14,  8, 16,   0,  0, 0,  0 },   // SLAB_MED_BOTTOM
    { 0,  8,  2, 14,   0,  0, 0,  0 },   // SLAB_MED_LEFT
    { 8, 16,  2, 14,   0,  0, 0,  0 },   // SLAB_MED_RIGHT
    { 5, 11,  0,  8,   0,  0, 0,  0 },   // SLAB_TINY_TOP
    { 5, 11,  8, 16,   0,  0, 0,  0 },   // SLAB_TINY_BOTTOM
    { 0,  8,  5, 11,   0,  0, 0,  0 },   // SLAB_TINY_LEFT
    { 8, 16,  5, 11,   0,  0, 0,  0 },   // SLAB_TINY_RIGHT
    { 0, 16,  0, 10,   0,  0, 0,  0 },   // EA_SLAB_TOP
    { 0, 16,  6, 16,   0,  0, 0,  0 },   // EA_SLAB_BOTTOM
    { 0, 10,  0, 16,   0,  0, 0,  0 },   // EA_SLAB_LEFT
    { 6, 16,  0, 16,   0,  0, 0,  0 },   // EA_SLAB_RIGHT
    { 0, 10,  0, 10,   0,  0, 0,  0 },   // EA_CORNER_TL
    { 6, 16,  0, 10,   0,  0, 0,  0 },   // EA_CORNER_TR
    { 0, 10,  6, 16,   0,  0, 0,  0 },   // EA_CORNER_BL
    { 6, 16,  6, 16,   0,  0, 0,  0 },   // EA_CORNER_BR
    { 0, 10,  0, 16,  10, 16, 0, 10 },   // EA_INSIDE_TL
    { 6, 16,  0, 16,   0,  6, 0, 10 },   // EA_INSIDE_TR
    { 0, 10,  0, 16,  10, 16, 6, 16 },   // EA_INSIDE_BL
    { 6, 16,  0, 16,   0,  6, 6, 16 },   // EA_INSIDE_BR
    { 0, 16,  0,  4,   0,  0, 0,  0 },   // OUTLINE_TOP
    { 0, 16, 12, 16,   0,  0, 0,  0 },   // OUTLINE_BOTTOM
    { 0,  4,  0, 16,   0,  0, 0,  0 },   // OUTLINE_LEFT
    { 12, 16, 0, 16,   0,  0, 0,  0 },   // OUTLINE_RIGHT
    { 4, 12,  4, 12,   0,  0, 0,  0 },   // CENTERED_MINI
};

// The rectangle of box type col that contains (xin, iy), or 0
static const uint8_t *box_at(uint8_t col, uint8_t xin, uint8_t iy) {
    const uint8_t *b = col_boxes[(uint8_t)(col - COL_BOX_BASE)];
    if (xin >= b[0] && xin < b[1] && iy >= b[2] && iy < b[3]) return b;
    b += 4;
    if (xin >= b[0] && xin < b[1] && iy >= b[2] && iy < b[3]) return b;
    return 0;
}

// Top (bottom: 0) or bottom edge, inside the metatile, of the box type solid at (xin, iy)
static uint8_t box_edge(uint8_t col, uint8_t xin, uint8_t iy, uint8_t bottom) {
    const uint8_t *b = box_at(col, xin, iy);
    return bottom ? b[3] : b[2];
}

// Slopes (COL_SLOPE_BASE..): the line y = c + k x / 2 (clamped to the metatile, 0..16), solid below
// it (above: the _UD ones), as GDA draws them (metatiles.c COL1_SLOPE_*, COL1_22_*, COL1_66_*: the
// same pixels, the 22 degree ones within half a pixel);
// vy: the speed (8.8, per frame at the scroll speed, gameplay.c SCROLL_SPEED_FP 714) of a cube
// walking up it (0: it goes down)
typedef struct { int8_t c, k; uint8_t ud; int16_t vy; } SlopeLine;
static const SlopeLine slope_lines[COL_SLOPE_COUNT] = {
    { 15, -2, 0, -714 }, { 0, 2, 0, 0 }, { 1, 2, 1, 0 }, { 16, -2, 1, 0 },             // 45: UP, DOWN, UP_UD, DOWN_UD
    { 15, -1, 0, -357 }, { 7, -1, 0, -357 }, { 0, 1, 0, 0 }, { 8, 1, 0, 0 },          // 22: UP_1, UP_2, DOWN_1, DOWN_2
    { 1, 1, 1, 0 }, { 9, 1, 1, 0 }, { 16, -1, 1, 0 }, { 8, -1, 1, 0 },                // 22: UP_UD_1, _2, DOWN_UD_1, _2
    { 30, -4, 0, -1428 }, { 14, -4, 0, -1428 }, { -16, 4, 0, 0 }, { 0, 4, 0, 0 },     // 66: UP_1, UP_2, DOWN_1, DOWN_2
    { -14, 4, 1, 0 }, { 2, 4, 1, 0 }, { 32, -4, 1, 0 }, { 16, -4, 1, 0 },             // 66: UP_UD_1, _2, DOWN_UD_1, _2
};

// The line's height (0 top .. 16 bottom) of slope type col at x xin of the metatile
static uint8_t slope_h(uint8_t col, uint8_t xin) {
    const SlopeLine *l = &slope_lines[(uint8_t)(col - COL_SLOPE_BASE)];
    int16_t h = (int16_t)l->c + (((int16_t)l->k * (int16_t)xin) >> 1);
    if (h < 0) return 0;
    if (h > 16) return 16;
    return (uint8_t)h;
}

// Collision types that depend on where inside the metatile the probe is (half blocks,
// half spikes, quadrants, boxes, slopes). Kept out of inline_col_at so its 13 inlined copies in
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
    } else if ((uint8_t)(col - COL_BOX_BASE) < COL_BOX_COUNT) {
        return box_at(col, xin, inner_y) ? COL_ALL : COL_NONE;
    } else if (IS_SLOPE(col)) {
        uint8_t h = slope_h(col, xin);
        uint8_t below = (inner_y >= h);
        return (below != slope_lines[(uint8_t)(col - COL_SLOPE_BASE)].ud) ? COL_ALL : COL_NONE;
    } else if (col == COL_BREAKABLE) {
        return COL_ALL;
    }
    return col;
}

// xin: x inside the 16px metatile of the probe point, (world_x + offset) & 15
static inline uint8_t inline_col_at(const uint8_t* col_ptr, int16_t y, uint8_t xin) {
    if ((uint16_t)y & 0xFF00) {
        return (y < 0) ? COL_NONE : COL_ALL;
    }
    uint8_t py8 = (uint8_t)y;
    uint8_t col = level_mt_col[col_ptr[py8 >> 4]];
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
#define MT_BOTTOM_BACKGROUND_SPIKES 12
#define MT_PITSPIKES                24
static uint8_t spike_at(const uint8_t *col_ptr, int16_t y, uint8_t xin) {
    if ((uint16_t)y & 0xFF00) return 0;
    uint8_t py8 = (uint8_t)y;
    uint8_t mt = col_ptr[py8 >> 4];
    uint8_t col = level_mt_col[mt];
    uint8_t iy = py8 & 0x0F;
    // Pit spikes and background spikes are a row of spikes across the whole metatile: they
    // kill at any x (Famidash: x 5..7 only, so touching them for a frame often did nothing)
    if (mt == MT_PITSPIKES || mt == MT_BOTTOM_BACKGROUND_SPIKES) return iy > 10u;
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

// The collision types spike_at can kill on (a superset is fine: it only gates the spike_at calls)
#define IS_SPIKE_COL(col) ((uint8_t)((col) - COL_DEATH_RIGHT) < 4u || (col) == COL_DEATH || \
        (uint8_t)((col) - COL_DEATH_TOP_HALF) < 2u || \
        (uint8_t)((col) - (COL_QUAD_BASE + 12u)) < (uint8_t)(COL_QUAD_COUNT - 12))

// Saw cores. Saw parts use Famidash's spike types (spike_at), but those only kill in small zones
// that leave holes: the big saw's centre metatile has no collision at all, and a medium saw only
// kills in two 3px strips, so a player 1-5px off a saw's centre line went straight through.
// So each saw also kills where the drawn player box (normal 16x16, mini 8x8) overlaps a core
// circle around its centre (teeth: small ~6.5, medium ~15.5, big ~21; the core lets the box 5-6px
// into the drawn saw at most, the old circles 2-3px):
// it closes the holes and leaves Famidash's zones deciding near the saw's edge.
// { x, y, r }: the centre relative to the part's 16x16 metatile, r = 0 for not a saw part.
// Metatiles 0x00..0x0B and 0x74..0x7F (famidash_metatiles_dmg.c).
#define SAW_R_SMALL 4
#define SAW_R_MED   10
#define SAW_R_BIG   15
#define SAW_MT_HI   0x74
static const int8_t saw_core_lo[12][3] = {
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
    {  8,  0, SAW_R_SMALL },   // 0x04 SMALL_SAW_BOTTOM_HALF (saw in the top half)
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
    { 16, 16, SAW_R_MED },     // 0x08 MED_SAW_TOP_LEFT
    {  0, 16, SAW_R_MED },     // 0x09 MED_SAW_TOP_RIGHT
    { 16,  0, SAW_R_MED },     // 0x0A MED_SAW_BOTTOM_LEFT
    {  0,  0, SAW_R_MED },     // 0x0B MED_SAW_BOTTOM_RIGHT
};
static const int8_t saw_core_hi[12][3] = {
    { 24, 24, SAW_R_BIG },     // 0x74 BIG_SAW_TOP_LEFT
    {  8, 24, SAW_R_BIG },     // 0x75 BIG_SAW_TOP_MIDDLE
    { -8, 24, SAW_R_BIG },     // 0x76 BIG_SAW_TOP_RIGHT
    { 24,  8, SAW_R_BIG },     // 0x77 BIG_SAW_MIDDLE_LEFT
    {  8,  8, SAW_R_BIG },     // 0x78 BIG_SAW_MIDDLE
    { -8,  8, SAW_R_BIG },     // 0x79 BIG_SAW_MIDDLE_RIGHT
    { 24, -8, SAW_R_BIG },     // 0x7A BIG_SAW_BOTTOM_LEFT
    {  8, -8, SAW_R_BIG },     // 0x7B BIG_SAW_BOTTOM_MIDDLE
    { -8, -8, SAW_R_BIG },     // 0x7C BIG_SAW_BOTTOM_RIGHT
    {  8,  8, SAW_R_SMALL },   // 0x7D SMALL_SAW
    { 0, 0, 0 },
    {  8, 16, SAW_R_SMALL },   // 0x7F SMALL_SAW_TOP_HALF (saw in the bottom half)
};
#define IS_SAW_MT(mt) ((uint8_t)((mt) - 8u) < 4u || (mt) == 4u || (uint8_t)((mt) - SAW_MT_HI) < 12u)
static uint8_t saw_bw, saw_bh;   // player box width - 1, height - 1 (set by player_update)
// n * n (the 16-bit multiply is a library call)
static const uint8_t saw_sq[16] = { 0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 169, 196, 225 };

// Corner (right?, bottom?) of the player box is at (xin, y) of the metatile column col_ptr:
// 1 when that metatile is a saw part whose core the box overlaps. A box that overlaps a core
// always has a corner in one of the saw's metatiles (the box is never bigger than a metatile).
static uint8_t saw_corner(const uint8_t *col_ptr, int16_t y, uint8_t xin, uint8_t right, uint8_t bottom) {
    if ((uint16_t)y & 0xFF00) return 0;
    uint8_t py8 = (uint8_t)y;
    uint8_t mt = col_ptr[py8 >> 4];
    const int8_t *c;
    if (mt < 12u) c = saw_core_lo[mt];
    else if ((uint8_t)(mt - SAW_MT_HI) < 12u) c = saw_core_hi[(uint8_t)(mt - SAW_MT_HI)];
    else return 0;
    uint8_t rad = (uint8_t)c[2];
    if (!rad) return 0;
    // the box in the metatile's coordinates
    int8_t l = (int8_t)xin, t = (int8_t)(py8 & 15u);
    if (right) l -= (int8_t)saw_bw;
    if (bottom) t -= (int8_t)saw_bh;
    int8_t r = (int8_t)(l + saw_bw), b = (int8_t)(t + saw_bh);
    // distance from the core's centre to the nearest point of the box
    int8_t cx = c[0], cy = c[1];
    uint8_t dx = (cx < l) ? (uint8_t)(l - cx) : (cx > r) ? (uint8_t)(cx - r) : 0;
    uint8_t dy = (cy < t) ? (uint8_t)(t - cy) : (cy > b) ? (uint8_t)(cy - b) : 0;
    if (dx >= rad || dy >= rad) return 0;
    return (uint16_t)(saw_sq[dx] + saw_sq[dy]) < saw_sq[rad];   // dx, dy < rad <= 15
}

// Not inlined (the slope code's probes): inline_col_at's body once
static uint8_t probe_col(const uint8_t *col_ptr, int16_t y, uint8_t xin) {
    return inline_col_at(col_ptr, y, xin);
}
// A non-slope solid at probe offset off, height y (the slope code)
#define FLAT_AT(off, y) (IS_SOLID(probe_col(GET_COL_FAST(off), (int16_t)(y), (uint8_t)(wx + (off)) & 15u)) && !IS_SLOPE(RAW_COL(off, y)))

// off: x offset of the probe point inside the player box
#define COL_AT(off, y) inline_col_at(GET_COL_FAST(off), (int16_t)(y), (uint8_t)(wx + (off)) & 15u)
#define SPIKE(off, y) spike_at(GET_COL_FAST(off), (int16_t)(y), (uint8_t)(wx + (off)) & 15u)
// The metatile's own collision type at a probe point (COL_ALL off the map)
#define RAW_COL(off, y) (((uint16_t)(y) & 0xFF00) ? COL_ALL : level_mt_col[GET_COL_FAST(off)[(uint8_t)(y) >> 4]])

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

    // Floor slopes (normal gravity): the feet stand on the highest slope line under the 3 foot
    // probes, in the metatile of the feet or the one above it (and, standing on a slope already,
    // up to 8 px below: down a slope). Up to 8 px up (a slope's tall side is a wall). A cube going
    // up a slope moves up at the slope's speed and keeps it off its top (GD's slope launch).
    uint8_t was_on_slope = p->on_slope;
    p->on_slope = 0;
    if (!p->gravity_flipped && (p->vel_y.w >= 0 || was_on_slope)) {
        int16_t foot_y = py + box_bot;
        int16_t best = 0x7FFF;
        int16_t best_vy = 0;
        for (uint8_t k = 0; k < 3u; k++) {
            uint8_t off = (k == 0) ? 0 : (k == 1) ? (uint8_t)(box_r >> 1) : box_r;
            for (uint8_t d = 0; d < 3u; d++) {
                int16_t yy = (d == 0) ? foot_y - 1 : (d == 1) ? foot_y : foot_y + 8;
                if (d == 2 && !was_on_slope) break;
                uint8_t raw = RAW_COL(off, yy);
                if (!IS_SLOPE(raw)) continue;
                const SlopeLine *l = &slope_lines[(uint8_t)(raw - COL_SLOPE_BASE)];
                if (l->ud) continue;
                uint8_t h = slope_h(raw, (uint8_t)(wx + off) & 15u);
                if (h >= 16u) continue;   // nothing of the slope in this column of the metatile
                int16_t sy = (int16_t)(yy & ~15) + h;
                if (sy < best) { best = sy; best_vy = l->vy; }
            }
        }
        // A foot on flat ground right under the feet (the top of a slope onto blocks), and no
        // slope line above them: the ground holds the cube (the floor code below), no launch
        // (standing on a slope: as far down as it would follow the slope)
        if (best >= foot_y) {
            for (int16_t fy = foot_y + 1; fy <= foot_y + (was_on_slope ? 8 : 1); fy += 7) {
                if (FLAT_AT(0, fy) || FLAT_AT(box_r >> 1, fy) || FLAT_AT(box_r, fy)) {
                    best = 0x7FFF;
                    if (p->vel_y.w < 0) p->vel_y.w = 0;
                    break;
                }
            }
        }
        if (best >= foot_y - 8 && best <= foot_y + (was_on_slope ? 8 : 0)) {
            p->world_y.b.h = (uint8_t)(best - box_bot);
            p->world_y.b.l = 0;
            p->vel_y.w = best_vy;
            p->on_ground = 1;
            p->orb_buffered = 0;
            p->on_slope = 1;
        }
    }

    // Floor collision
    if (p->vel_y.w >= 0 && !p->on_slope) {
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
            uint8_t raw = RAW_COL(hit_off, foot_y);
            // A box has its top where its rectangle under the probe starts
            if ((uint8_t)(raw - COL_BOX_BASE) < COL_BOX_COUNT) {
                block_top_y += box_edge(raw, (uint8_t)(wx + hit_off) & 15u, (uint8_t)foot_y & 15u, 0);
            // A bottom half block / quadrant solid hit in its lower half has its top 8px lower
            } else if (hit_col == COL_BOTTOM ||
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
            uint8_t raw = RAW_COL(hit_off, head_y);
            // A ceiling slope: its line above the probe
            if (IS_SLOPE(raw)) {
                block_bottom_y = (uint8_t)((head_y & ~15) + slope_h(raw, (uint8_t)(wx + hit_off) & 15u));
            // A box has its bottom where its rectangle above the probe ends
            } else if ((uint8_t)(raw - COL_BOX_BASE) < COL_BOX_COUNT) {
                block_bottom_y = (uint8_t)((head_y & ~15) +
                                 box_edge(raw, (uint8_t)(wx + hit_off) & 15u, (uint8_t)head_y & 15u, 1));
            // A top half block / quadrant solid hit in its upper half has its bottom 8px higher
            } else if (hit_col == COL_TOP ||
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
        // A breakable block in front breaks instead (GDA: the player's centre hitbox breaks it)
        int16_t fy = (int16_t)py + front_y;
        if (RAW_COL(box_r, fy) != COL_BREAKABLE) {
            p->dead = 1;
            return 1;
        }
        break_block((uint16_t)(p->world_x + box_r) >> 4, (uint8_t)((uint16_t)(p->y_base + (uint8_t)fy) >> 4));
    }
    // Most frames touch no spike: look at the (up to 4) metatiles the spike probes below lie in
    // first, and only run the 6 spike_at calls when one of them is a spike type
    uint8_t spike_near = 1;
    {
        int16_t sy0 = (int16_t)py + (mini ? 4 : 3);
        int16_t sy1 = (int16_t)py + (mini ? 10 : 14);
        if (!(((uint16_t)sy0 | (uint16_t)sy1) & 0xFF00)) {
            uint8_t r0 = (uint8_t)sy0 >> 4, r1 = (uint8_t)sy1 >> 4;
            uint8_t k0 = level_mt_col[c0[r0]];
            uint8_t k1 = level_mt_col[c0[r1]];
            uint8_t k2 = level_mt_col[c1[r0]];
            uint8_t k3 = level_mt_col[c1[r1]];
            spike_near = IS_SPIKE_COL(k0) || IS_SPIKE_COL(k1) || IS_SPIKE_COL(k2) || IS_SPIKE_COL(k3);
        }
    }
    if (!spike_near) {
        // no spike probe can hit
    } else if (mini) {
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

    // Saw cores: the 4 corners of the drawn player box (see saw_corner)
    if (saw_on) {
        saw_bw = mini ? 7 : 15;
        saw_bh = saw_bw;
        int16_t ty = (int16_t)py + (mini ? 3 : 0);
        int16_t by = ty + saw_bh;
        uint8_t xr = (uint8_t)(wx + saw_bw) & 15u;
        const uint8_t *cr = GET_COL_FAST(saw_bw);
        // inline check first: saw_corner (5 arguments on the stack) only runs on a saw part
#define ON_SAW(cp, y) (!((uint16_t)(y) & 0xFF00) && IS_SAW_MT((cp)[(uint8_t)(y) >> 4]))
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
