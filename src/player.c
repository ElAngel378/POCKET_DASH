#pragma bank 12

#include "player.h"
#include "collision.h"
#include "scratch.h"

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
    p->slope_state = 0;
    p->slope_type = 0xFF;
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

// Slopes (COL_SLOPE_BASE..), as GDA does them (collision.c collide_with_map_slopes, slope_check):
// the player is a circle (its box's centre, radius half its height), each slope a right triangle
// (from the slope's metatile; the 22 and 66 degree ones span two). A circle touching the long side
// is pushed off it a pixel at a time (step: 1 a floor slope, up; -1 a ceiling slope, down) and gets
// the slope's y speed; the flat side is a floor / ceiling within 4 px; the tall side kills when the
// player's centre (radius 2, mini 1) touches it. Not solid for the block probes.
// Per type, from the metatile's top left: the long side (x1, y1 - x2, y2), the flat side's x range
// (its y is down_y), the tall side (x, y range), up_y / down_y (the triangle's top and bottom along
// step), step, the y speed in half x speeds (GDA slope_speed_multiplier x 2) and kind
#define SK_UP 1    // goes up the way the player goes (GDA DEGREES_* below DEGREES_45_DOWN)
#define SK_22 2
#define SK_66 4
typedef struct {
    int8_t hx1, hy1, hx2, hy2, ex1, ex2, vx, vy1, vy2, up_y, down_y, step, speed;
    uint8_t kind;
} Slope;
static const Slope slopes[COL_SLOPE_COUNT] = {
    {   0,  16,  16,   0,    0,  16,   16,   0,  16,    0,  16,   1, -2, SK_UP },         // 45_UP
    {   0,   0,  16,  16,    0,  16,    0,   0,  16,    0,  16,   1,  2, 0 },             // 45_DOWN
    {  16,  16,   0,   0,    0,  16,   16,   0,  16,   16,   0,  -1,  2, SK_UP },         // 45_UP_UD
    {  16,   0,   0,  16,    0,  16,    0,   0,  16,   16,   0,  -1, -2, 0 },             // 45_DOWN_UD
    {   0,  16,  32,   0,    0,  32,   32,   0,  16,    0,  16,   1, -1, SK_UP|SK_22 },   // 22_UP_1
    { -16,  16,  16,   0,  -16,  16,   16,   0,  16,    0,  16,   1, -1, SK_UP|SK_22 },   // 22_UP_2
    {   0,   0,  32,  16,    0,  32,    0,   0,  16,    0,  16,   1,  1, SK_22 },         // 22_DOWN_1
    { -16,   0,  16,  16,  -16,  16,  -16,   0,  16,    0,  16,   1,  1, SK_22 },         // 22_DOWN_2
    {  32,  16,   0,   0,    0,  32,   32,   0,  16,   16,   0,  -1,  1, SK_UP|SK_22 },   // 22_UP_UD_1
    {  16,  16, -16,   0,  -16,  16,   16,   0,  16,   16,   0,  -1,  1, SK_UP|SK_22 },   // 22_UP_UD_2
    {  32,   0,   0,  16,    0,  32,    0,   0,  16,   16,   0,  -1, -1, SK_22 },         // 22_DOWN_UD_1
    {  16,   0, -16,  16,  -16,  16,  -16,   0,  16,   16,   0,  -1, -1, SK_22 },         // 22_DOWN_UD_2
    {   0,  32,  16,   0,    0,  16,   16,   0,  32,    0,  32,   1, -4, SK_UP|SK_66 },   // 66_UP_1
    {   0,  16,  16, -16,    0,  16,   16, -16,  16,  -16,  16,   1, -4, SK_UP|SK_66 },   // 66_UP_2
    {   0, -16,  16,  16,    0,  16,    0, -16,  16,  -16,  16,   1,  4, SK_66 },         // 66_DOWN_1
    {   0,   0,  16,  32,    0,  16,    0,   0,  32,    0,  32,   1,  4, SK_66 },         // 66_DOWN_2
    {  16,  16,   0, -16,    0,  16,   16, -16,  16,   16, -16,  -1,  4, SK_UP|SK_66 },   // 66_UP_UD_1
    {  16,  32,   0,   0,    0,  16,   16,   0,  32,   32,   0,  -1,  4, SK_UP|SK_66 },   // 66_UP_UD_2
    {  16,   0,   0,  32,    0,  16,    0,   0,  32,   32,   0,  -1, -4, SK_66 },         // 66_DOWN_UD_1
    {  16, -16,   0,  16,    0,  16,    0, -16,  16,   16, -16,  -1, -4, SK_66 },         // 66_DOWN_UD_2
};

// GDA's find_squared_distance_to_line(...) <= r * r (its integer division included) without 32 bit
// maths: beside the long side |cross| <= floor(sqrt((r * r + 1) * len * len - 1)), by radius
// (1, 2, 4, 8) and length (45: 16 x 16, 22 / 66: 32 x 16)
static const uint16_t slope_cross_max[2][4] = { { 31, 50, 93, 182 }, { 50, 79, 147, 288 } };

#define SLOPE_NONE 0x7FFF
#define EJ_HIPO 1
#define EJ_HORZ 2
#define EJ_VERT 3
// The slope being checked: its type, its metatile's top left (x from the left edge of the
// player's metatile column, y in the collision window); the circle being checked. In scratch_buf:
// WRAM is full, and nothing else uses it while player_update runs
typedef struct {
    const Slope *sl;
    uint8_t sl_type;
    int16_t sl_ox, sl_oy;
    int16_t c_x, c_y;
    uint8_t c_r, c_ri;      // radius, its index in slope_cross_max
    int8_t c_dir;           // 1: the player's y speed is down (> 0), else -1
    uint8_t c_skip_hipo;    // GDA horizontal_slope_counter
} SlopeVars;
#define SV ((SlopeVars *)scratch_buf)
#define sl          (SV->sl)
#define sl_type     (SV->sl_type)
#define sl_ox       (SV->sl_ox)
#define sl_oy       (SV->sl_oy)
#define c_x         (SV->c_x)
#define c_y         (SV->c_y)
#define c_r         (SV->c_r)
#define c_ri        (SV->c_ri)
#define c_dir       (SV->c_dir)
#define c_skip_hipo (SV->c_skip_hipo)
// Player.slope_state
#define SLOPE_COUNTER(p) ((p)->slope_state & 7u)
#define SLOPE_MULT(p)    ((p)->slope_state >> 4)
#define SET_SLOPE_COUNTER(p, n) ((p)->slope_state = (uint8_t)(((p)->slope_state & 0xF0u) | (n)))

// No multiplications below (the Game Boy has none: SDCC's routine made a frame on a slope take up
// to 1.3 frames on DMG): a long side's dx, dy are +-16 or +-32, v x d is a shift
static int16_t mul_side(int16_t v, int8_t d) {
    v <<= 4;
    if (d == 32 || d == -32) v <<= 1;
    return (d < 0) ? -v : v;
}
static const uint8_t sq9[9] = { 0, 1, 4, 9, 16, 25, 36, 49, 64 };
// The point (px, py) from the circle's centre within its radius
static uint8_t point_near(int16_t px, int16_t py) {
    if (px < 0) px = -px;
    if (py < 0) py = -py;
    if (px > c_r || py > c_r) return 0;
    return (uint8_t)(sq9[(uint8_t)px] + sq9[(uint8_t)py]) <= (uint8_t)(c_r * c_r);
}

// The circle (c_x, c_y, c_r) within its radius of the segment from (x1, y1) by (dx, dy), a long side
static uint8_t seg_near(int16_t x1, int16_t y1, int8_t dx, int8_t dy) {
    int16_t px = c_x - x1, py = c_y - y1;
    int16_t t = mul_side(px, dx) + mul_side(py, dy);
    if (t <= 0) return point_near(px, py);
    uint8_t long22 = (sl->kind & (SK_22 | SK_66)) ? 1 : 0;
    if (t >= (long22 ? 1280 : 512)) return point_near(px - dx, py - dy);
    int16_t c = mul_side(px, dy) - mul_side(py, dx);
    if (c < 0) c = -c;
    return (uint16_t)c <= slope_cross_max[long22][c_ri];
}

// GDA check_distance_circle_hipotenuse
static uint8_t hipo_near(void) {
    if (c_skip_hipo) return 0;
    // down slopes: not in the top 2 px
    if (!(sl->kind & SK_UP)) {
        int16_t d = (sl->step > 0 ? c_y + c_r : c_y - c_r) - (sl_oy + sl->up_y);
        if (d > -2 && d < 2) return 0;
    }
    return seg_near(sl_ox + sl->hx1, sl_oy + sl->hy1, (int8_t)(sl->hx2 - sl->hx1), (int8_t)(sl->hy2 - sl->hy1));
}

// GDA check_distance_circle_horizontal_edge: on (step -1) / under (step 1) the flat side, moving to it
static uint8_t horz_near(void) {
    if (sl->step == c_dir) return 0;
    if (c_x + c_r <= sl_ox + sl->ex1 || c_x - c_r >= sl_ox + sl->ex2) return 0;
    int16_t d = (sl->step > 0 ? c_y - c_r : c_y + c_r) - (sl_oy + sl->down_y);
    if (sl->step > 0) d = -d;
    return d >= 0 && d < 4;
}

// GDA check_distance_circle_vertical_edge
static uint8_t vert_near(void) {
    int16_t dx = sl_ox + sl->vx - c_x;
    int16_t y = c_y, y0 = sl_oy + sl->vy1, y1 = sl_oy + sl->vy2;
    if (y < y0) y = y0;
    if (y > y1) y = y1;
    return point_near(dx, y - c_y);
}

// GDA check_slope_eject_type
static uint8_t slope_eject_type(void) {
    if (horz_near()) return EJ_HORZ;
    if (hipo_near()) return EJ_HIPO;
    if (vert_near()) return EJ_VERT;
    return 0;
}

// GDA check_slope_collision: how far to move the circle off the slope (SLOPE_NONE: not touching)
static int16_t slope_eject(uint8_t type) {
    if (type == EJ_HIPO) {
        // GDA's loop: the circle a pixel at a time along step until hipo_near is false. The sums
        // hipo_near / seg_near work out change by the same amount each pixel: kept by adding
        int8_t step = sl->step, dx = (int8_t)(sl->hx2 - sl->hx1), dy = (int8_t)(sl->hy2 - sl->hy1);
        int16_t px = c_x - (sl_ox + sl->hx1);
        int16_t py = c_y - (sl_oy + sl->hy1);
        int16_t t = mul_side(px, dx) + mul_side(py, dy), dt = mul_side(step, dy);
        int16_t c = mul_side(px, dy) - mul_side(py, dx), dc = mul_side(step, dx);
        uint8_t long22 = (sl->kind & (SK_22 | SK_66)) ? 1 : 0;
        int16_t len2 = long22 ? 1280 : 512;
        int16_t cmax = (int16_t)slope_cross_max[long22][c_ri];
        // down slopes: not in the top 2 px (hipo_near)
        uint8_t down = !(sl->kind & SK_UP);
        int16_t d = (step > 0 ? c_y + c_r : c_y - c_r) - (sl_oy + sl->up_y);
        int8_t n = 0;
        for (; n < 48; n++, py -= step, t -= dt, c += dc, d -= step) {
            if (down && d > -2 && d < 2) break;
            uint8_t near;
            if (t <= 0) near = point_near(px, py);
            else if (t >= len2) near = point_near(px - dx, py - dy);
            else near = (c < 0 ? -c : c) <= cmax;
            if (!near) break;
        }
        return (int16_t)(-n * step);
    }
    if (type == EJ_HORZ) return (int16_t)(sl_oy + sl->down_y + (int8_t)(c_r * sl->step)) - c_y;
    if (type == EJ_VERT) return 0;
    return SLOPE_NONE;
}

// The player's centre (GDA's internal hitbox: radius 2, mini 1) touches the slope: its tall side
// (vert), or any side
static uint8_t centre_touches(uint8_t vert) {
    uint8_t r = c_r, ri = c_ri, hit;
    c_r = player.mini ? 1 : 2;
    c_ri = player.mini ? 0 : 1;
    hit = slope_eject_type();
    hit = vert ? (hit == EJ_VERT) : (slope_eject(hit) != SLOPE_NONE);
    c_r = r; c_ri = ri;
    return hit;
}

// GDA slope_check: the circle touches the slope sl (ej: how far to move it, type: which side).
// 1: look at no more slopes
static uint8_t slope_check(int16_t ej, uint8_t type) {
    if (type == EJ_VERT) {
        if (centre_touches(1)) player.dead = 1;
        return 0;
    }
    int8_t step = sl->step;
    // A cube stands on the slopes on its gravity's side only; the others kill when its centre
    // touches them
    if (player.mode == MODE_CUBE && (step < 0) != (player.gravity_flipped != 0)) {
        if (centre_touches(0)) player.dead = 1;
        return 1;
    }
    // The y speed along the slope: GDA's multiplier x the x speed (gameplay.c SCROLL_SPEED_FP 714),
    // going up eased in over 4 frames. Its size m, shifts only (SDCC's multiplications and
    // divisions here made a frame on a slope take up to 1.5 frames on DMG)
    uint16_t m = 0;
    if (type == EJ_HIPO) {
        int8_t k = sl->speed;
        if (k < 0) k = -k;
        m = (k == 1) ? 357 : (k == 2) ? 714 : 1428;
        if (sl->kind & SK_UP) {
            uint8_t q = SLOPE_MULT(&player);
            m = (q == 0) ? 0 : (q == 1) ? (m >> 2) : (q == 2) ? (m >> 1) : (q == 3) ? (uint16_t)(m - (m >> 2)) : m;
        }
    } else {
        player.vel_y.w = 0;
    }
    player.world_y.b.h += (int8_t)ej;
    player.world_y.b.l = (step > 0) ? 0xFF : 0;
    c_y += ej;
    if ((sl->kind & (SK_UP | SK_66)) == (SK_UP | SK_66)) {
        if (player.mode == MODE_BALL) m >>= 1;                            // GDA: / 2
        else if (player.mode == MODE_UFO) m = (uint16_t)(m - (m >> 2));  // x 3 / 4
    }
    if (player.mode == MODE_SHIP) m = (uint16_t)(m - (m >> 3));         // x 14 / 16
    int16_t speed = (sl->speed < 0) ? -(int16_t)m : (int16_t)m;
    if ((step > 0) != (player.gravity_flipped != 0)) {
        player.on_ground = 1;
        player.orb_buffered = 0;
    }
    if (type == EJ_HIPO) {
        player.on_slope = 1;
        // the slope counts 5 more frames (a cube after a 22 degree slope up: 3)
        SET_SLOPE_COUNTER(&player, (player.mode == MODE_CUBE && player.slope_type != 0xFF &&
            (slopes[player.slope_type].kind & (SK_UP | SK_22)) == (SK_UP | SK_22)) ? 3 : 5);
        player.slope_type = sl_type;
    } else if (type == EJ_HORZ) {
        c_skip_hipo = 1;
    }
    if (step < 0) {
        if (player.vel_y.w < speed) player.vel_y.w = speed;
    } else {
        if (player.vel_y.w > speed) player.vel_y.w = speed;
    }
    return 0;
}

// GDA collide_with_map_slopes: the slopes in the 2 x 2 metatiles from the top left of the
// player's box (columns c0, c1; xm: x inside c0's metatile; py: the box's top row). 1: died
static uint8_t slope_pass(const uint8_t *c0, const uint8_t *c1, uint8_t py, uint8_t xm) {
    uint8_t mini = player.mini;
    c_r = mini ? 4 : 8;
    c_ri = mini ? 2 : 3;
    c_x = xm + (mini ? 4 : 8);
    c_y = py + (mini ? 7 : 8);
    c_dir = (player.vel_y.w > 0) ? 1 : -1;
    c_skip_hipo = 0;
    int16_t top = py + (mini ? 3 : 0);
    for (uint8_t side = 0; side < 4u; side++) {
        uint8_t yo = (side & 2u) ? 16 : 0;
        if (player.vel_y.w < 0) yo = 16 - yo;
        int16_t y = top + yo;
        if ((uint16_t)y & 0xFF00) continue;
        uint8_t col = level_mt_col[((side & 1u) ? c1 : c0)[(uint8_t)y >> 4]];
        if (!IS_SLOPE(col)) continue;
        sl_type = (uint8_t)(col - COL_SLOPE_BASE);
        sl = &slopes[sl_type];
        sl_ox = (side & 1u) ? 16 : 0;
        sl_oy = y & ~15;
        uint8_t type = slope_eject_type();
        int16_t ej = slope_eject(type);
        if (ej == SLOPE_NONE) continue;
        if (slope_check(ej, type)) break;
    }
    return player.dead;
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
        return COL_NONE;   // the slope code (slope_pass)
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
            // Just off a 22 / 45 degree slope up: no gravity while the slope still counts (GDA:
            // the cube starts falling later)
            if (!was_on_ground && SLOPE_COUNTER(p) &&
                (slopes[p->slope_type].kind & (SK_UP | SK_66)) == SK_UP) gravity_val = 0;
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

    // Slopes first (GDA collision_cube): they move the player, the blocks below see where to
    p->on_slope = 0;
    if (slope_pass(c0, c1, py, x_mod_16)) return 1;
    py = p->world_y.b.h;
    // Standing on a slope (GDA slope_counter): the block probes are only the front ones, the
    // others would catch the blocks the slope is on
    uint8_t probe0 = SLOPE_COUNTER(p) ? box_r : 0;
    uint8_t probe1 = SLOPE_COUNTER(p) ? box_r : (uint8_t)(box_r >> 1);

    // Floor collision
    if (p->vel_y.w >= 0) {
        int16_t foot_y = py + box_bot;
        uint8_t hit_off = probe0;
        uint8_t hit_col = COL_AT(probe0, foot_y);
        if (!IS_SOLID(hit_col)) {
            hit_off = probe1;
            hit_col = COL_AT(probe1, foot_y);
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
        uint8_t hit_off = probe0;
        uint8_t hit_col = COL_AT(probe0, head_y);
        if (!IS_SOLID(hit_col)) {
            hit_off = probe1;
            hit_col = COL_AT(probe1, head_y);
            if (!IS_SOLID(hit_col)) {
                hit_off = box_r;
                hit_col = COL_AT(box_r, head_y);
            }
        }
        if (IS_SOLID(hit_col)) {
            uint8_t block_bottom_y = (uint8_t)((head_y & ~15) + 16);
            uint8_t raw = RAW_COL(hit_off, head_y);
            // A box has its bottom where its rectangle above the probe ends
            if ((uint8_t)(raw - COL_BOX_BASE) < COL_BOX_COUNT) {
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
                if (p->on_slope) SET_SLOPE_COUNTER(p, 2);   // GDA cube_gamemode
                p->on_slope = 0;
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
    if (p->mode == MODE_CUBE && SLOPE_COUNTER(p)) {
        // On (or just off) a slope: lined up with it (GDA snap_to_45 / snap_to_tan_theta_1_2), in
        // half steps (7.5 deg) past a square side: 45 deg 6, a line going down 22 deg 4 (26.5),
        // 66 deg 8; going up 12 minus that
        const Slope *s = &slopes[p->slope_type];
        uint8_t t = (s->kind & SK_22) ? 4 : (s->kind & SK_66) ? 8 : 6;
        if (((s->kind & SK_UP) != 0) == (s->step > 0)) t = (uint8_t)(12u - t);
        uint8_t h = (uint8_t)((p->anim_frame << 1) + (p->anim_timer >= 11u ? 1u : 0u));
        h = (uint8_t)(h - ((mod6_table[p->anim_frame] << 1) + (h & 1u)) + t);
        p->anim_frame = (uint8_t)(h >> 1);
        p->anim_timer = (h & 1u) ? 11 : 0;
    } else if (p->on_ground && p->mode != MODE_BALL) {
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

    // GDA's per frame slope bookkeeping: the speed up a slope eases in, the slope stops counting
    if (SLOPE_COUNTER(p)) {
        if (SLOPE_MULT(p) < 4u) p->slope_state += 0x10;
        p->slope_state--;
        if (!SLOPE_COUNTER(p)) {
            p->on_slope = 0;
            p->slope_type = 0xFF;
            p->slope_state = 0;
        }
    }

    p->last_joy = joy;
    return 0;
#undef p
}

#undef GET_COL_FAST
