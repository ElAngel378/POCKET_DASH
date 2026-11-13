#pragma bank 12

#include "player.h"
#include "collision.h"
#include "bg_stream.h"

uint8_t level_max_fall_7;   // player.h

static const uint8_t mod6_table[24] = {
    0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5
};

uint8_t player_draw_dy;   // player.h
static uint8_t draw_was_flipped;

// Flipped gravity: 2 px down for the cube / ball, 1 mini; ship / UFO 2 mini, 5 full size. The full
// size ship's art is 1 px off centre in its cell, so its mirrored image shows 1 px higher: that
// pixel is taken up at once at the flip, the picture must not jump.
static uint8_t draw_dy_wanted(void) {
    if (!player.gravity_flipped) return 0;
    if (player.mode != MODE_SHIP && player.mode != MODE_UFO) return (uint8_t)(2u - player.mini);
    return player.mini ? 2 : 5;
}

void player_draw_dy_snap(void) BANKED {
    player_draw_dy = draw_dy_wanted();
    draw_was_flipped = player.gravity_flipped;
}

// Each physics frame (portals have already run this frame)
static void player_draw_dy_step(void) {
    uint8_t want = draw_dy_wanted();
    uint8_t flipped = player.gravity_flipped;
    if (flipped != draw_was_flipped && player.mode == MODE_SHIP && !player.mini) {
        if (flipped) player_draw_dy++;
        else if (player_draw_dy) player_draw_dy--;
    }
    if (player_draw_dy < want) player_draw_dy++;
    else if (player_draw_dy > want) player_draw_dy--;
    draw_was_flipped = flipped;
}

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
    p->ball_spin = 2;
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
// half spikes, quadrants). pack: probe position inside the metatile, (x << 4) | y.
static uint8_t col_at_partial(uint8_t col, uint8_t pack) {
    uint8_t lower = pack & 8u;         // y >= 8
    if (col == COL_TOP) {
        if (lower) return COL_NONE;
    } else if (col == COL_BOTTOM) {
        if (!lower) return COL_NONE;
    } else if (col == COL_DEATH_TOP_HALF) {
        if (!lower) return COL_NONE;
        return COL_DEATH;
    } else if (col == COL_DEATH_BOTTOM_HALF) {
        if (lower) return COL_NONE;
        return COL_DEATH;
    } else if ((uint8_t)(col - COL_QUAD_BASE) < COL_QUAD_COUNT) {
        uint8_t m = col_quads[(uint8_t)(col - COL_QUAD_BASE)];
        uint8_t q = 1;
        if (lower) q = 4;
        if (pack & 0x80u) q <<= 1;
        if (m & q) return COL_ALL;
        if (m & (uint8_t)(q << 4)) return COL_DEATH;
        return COL_NONE;
    } else if (col == COL_BREAKABLE) {
        return COL_ALL;
    }
    return col;
}

// Collision type at the point (player x + off, y). Only the lookup is in assembly: what the
// types do is col_at_partial and player_update, in C.
static uint8_t col_at(int16_t y, uint8_t off) __naked {
    (void)y; (void)off;
    __asm
        ld      b, a                    ; off (A), y (DE)
        ld      a, (_player)
        and     a, #15
        add     a, b
        ld      b, a                    ; b: bit 4 = right column, low nibble = x in the metatile
        ld      a, d
        or      a, a
        jr      NZ, 2$
        ld      a, b
        and     a, #0x10
        ld      c, a
        ld      a, e
        swap    a
        and     a, #0x0F
        or      a, c
        add     a, #<_collision_columns
        ld      l, a
        adc     a, #>_collision_columns
        sub     a, l
        ld      h, a
        ld      a, (hl)                 ; metatile
        add     a, #<_scratch_buf       ; col_tab[metatile]
        ld      l, a
        adc     a, #>_scratch_buf
        sub     a, l
        ld      h, a
        ld      a, (hl)
        cp      a, #COL_TOP
        ret     C                       ; air, plain spikes
        cp      a, #COL_ALL
        ret     Z
        ld      c, a
        ld      a, b
        swap    a
        and     a, #0xF0
        ld      b, a
        ld      a, e
        and     a, #0x0F
        or      a, b
        ld      e, a                    ; pack
        ld      a, c                    ; col
        jp      _col_at_partial
    2$:                                 ; y outside 0..255: air above, solid below
        xor     a, a
        bit     7, d
        ret     NZ
        ld      a, #COL_ALL
        ret
    __endasm;
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
// pack: probe position inside the metatile, (x << 4) | y
static uint8_t spike_shape(uint8_t mt, uint8_t pack) {
    uint8_t col = col_tab[mt];
    uint8_t iy = pack & 0x0Fu;
    uint8_t xin = pack >> 4;
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

static uint8_t spike_at(const uint8_t *col_ptr, int16_t y, uint8_t xin) {
    if ((uint16_t)y & 0xFF00) return 0;
    uint8_t py8 = (uint8_t)y;
    return spike_shape(col_ptr[py8 >> 4], (uint8_t)((uint8_t)(xin << 4) | (py8 & 0x0Fu)));
}

// The collision types spike_at can kill on (a superset is fine: it only gates the spike_at calls)
#define IS_SPIKE_COL(col) ((uint8_t)((col) - COL_DEATH_RIGHT) < 4u || (col) == COL_DEATH || \
        (uint8_t)((col) - COL_DEATH_TOP_HALF) < 2u || \
        (uint8_t)((col) - (COL_QUAD_BASE + 12u)) < (uint8_t)(COL_QUAD_COUNT - 12))
// The same as a table, for the check made on every frame (collision types are below 64)
static const uint8_t spike_col_tab[64] = {
    IS_SPIKE_COL(0u), IS_SPIKE_COL(1u), IS_SPIKE_COL(2u), IS_SPIKE_COL(3u), IS_SPIKE_COL(4u), IS_SPIKE_COL(5u), IS_SPIKE_COL(6u), IS_SPIKE_COL(7u),
    IS_SPIKE_COL(8u), IS_SPIKE_COL(9u), IS_SPIKE_COL(10u), IS_SPIKE_COL(11u), IS_SPIKE_COL(12u), IS_SPIKE_COL(13u), IS_SPIKE_COL(14u), IS_SPIKE_COL(15u),
    IS_SPIKE_COL(16u), IS_SPIKE_COL(17u), IS_SPIKE_COL(18u), IS_SPIKE_COL(19u), IS_SPIKE_COL(20u), IS_SPIKE_COL(21u), IS_SPIKE_COL(22u), IS_SPIKE_COL(23u),
    IS_SPIKE_COL(24u), IS_SPIKE_COL(25u), IS_SPIKE_COL(26u), IS_SPIKE_COL(27u), IS_SPIKE_COL(28u), IS_SPIKE_COL(29u), IS_SPIKE_COL(30u), IS_SPIKE_COL(31u),
    IS_SPIKE_COL(32u), IS_SPIKE_COL(33u), IS_SPIKE_COL(34u), IS_SPIKE_COL(35u), IS_SPIKE_COL(36u), IS_SPIKE_COL(37u), IS_SPIKE_COL(38u), IS_SPIKE_COL(39u),
    IS_SPIKE_COL(40u), IS_SPIKE_COL(41u), IS_SPIKE_COL(42u), IS_SPIKE_COL(43u), IS_SPIKE_COL(44u), IS_SPIKE_COL(45u), IS_SPIKE_COL(46u), IS_SPIKE_COL(47u),
    IS_SPIKE_COL(48u), IS_SPIKE_COL(49u), IS_SPIKE_COL(50u), IS_SPIKE_COL(51u), IS_SPIKE_COL(52u), IS_SPIKE_COL(53u), IS_SPIKE_COL(54u), IS_SPIKE_COL(55u),
    IS_SPIKE_COL(56u), IS_SPIKE_COL(57u), IS_SPIKE_COL(58u), IS_SPIKE_COL(59u), IS_SPIKE_COL(60u), IS_SPIKE_COL(61u), IS_SPIKE_COL(62u), IS_SPIKE_COL(63u)
};

// Which of the 6 spike probes of the full size player can hit: bit set = that probe's own metatile
// can hold a spike (bits 0, 1: x+3, x+12 at py+3; 2, 3: x+3, x+12 at py+14; 4, 5: x+6, x+15 at
// py+8). 0xFF: off the top of the 256 px window, all of them. Only the lookup is in assembly: what
// each spike does is spike_at, in C, like every other mechanic.
static uint8_t spike_probes(void) __naked {
    __asm
        ld      a, (_player + 3)        ; py
        cp      a, #242                 ; py + 14 over 255: cannot tell
        jr      C, 1$
        ld      a, #0xFF
        ret
    1$:
        ld      b, a
        add     a, #3
        swap    a
        and     a, #0x0F
        ld      d, a                    ; d = r0, row of py + 3
        ld      a, b
        add     a, #14
        swap    a
        and     a, #0x0F
        ld      e, a                    ; e = r1, row of py + 14
        ld      a, b
        add     a, #8
        swap    a
        and     a, #0x0F
        cp      a, d
        ld      a, #1                   ; (ld keeps the flags) centre probes in the upper row?
        jr      Z, 2$
        xor     a, a
    2$:
        push    af                      ; rc
        ld      a, d
        call    30$
        push    af                      ; s0: column 0, r0
        ld      a, e
        call    30$
        push    af                      ; s1: column 0, r1
        ld      a, d
        add     a, #16
        call    30$
        push    af                      ; s2: column 1, r0
        ld      a, e
        add     a, #16
        call    30$
        ld      b, a                    ; b = s3: column 1, r1
        pop     af
        ld      c, a                    ; c = s2
        pop     af
        ld      d, a                    ; d = s1
        pop     af
        ld      e, a                    ; e = s0
        pop     af
        ld      h, a                    ; h = rc
        ld      a, b
        or      a, c
        or      a, d
        or      a, e
        ret     Z                       ; no spike metatile around: most frames
        ld      a, (_player)            ; which column each probe is in: x & 15
        and     a, #15
        ld      l, #0
        cp      a, #13                  ; l bit 0: x+3 in column 0
        jr      NC, 3$
        set     0, l
    3$:
        cp      a, #4                   ; bit 1: x+12
        jr      NC, 4$
        set     1, l
    4$:
        cp      a, #10                  ; bit 2: x+6
        jr      NC, 5$
        set     2, l
    5$:
        cp      a, #1                   ; bit 3: x+15
        jr      NC, 6$
        set     3, l
    6$:
        xor     a, a
        ; bit 5: x+15, centre row
        bit     0, h
        jr      Z, 8$
        bit     3, l
        jr      Z, 7$
        or      a, e
        jr      11$
    7$:
        or      a, c
        jr      11$
    8$:
        bit     3, l
        jr      Z, 9$
        or      a, d
        jr      11$
    9$:
        or      a, b
    11$:
        add     a, a                    ; bit 4: x+6, centre row
        bit     0, h
        jr      Z, 13$
        bit     2, l
        jr      Z, 12$
        or      a, e
        jr      16$
    12$:
        or      a, c
        jr      16$
    13$:
        bit     2, l
        jr      Z, 14$
        or      a, d
        jr      16$
    14$:
        or      a, b
    16$:
        add     a, a                    ; bit 3: x+12, lower row
        bit     1, l
        jr      Z, 17$
        or      a, d
        jr      18$
    17$:
        or      a, b
    18$:
        add     a, a                    ; bit 2: x+3, lower row
        bit     0, l
        jr      Z, 19$
        or      a, d
        jr      20$
    19$:
        or      a, b
    20$:
        add     a, a                    ; bit 1: x+12, upper row
        bit     1, l
        jr      Z, 21$
        or      a, e
        jr      22$
    21$:
        or      a, c
    22$:
        add     a, a                    ; bit 0: x+3, upper row
        bit     0, l
        jr      Z, 23$
        or      a, e
        ret
    23$:
        or      a, c
        ret
        ; a = offset in collision_columns -> a = 1 if that metatile can hold a spike
    30$:
        add     a, #<_collision_columns
        ld      l, a
        adc     a, #>_collision_columns
        sub     a, l
        ld      h, a
        ld      a, (hl)                 ; metatile
        add     a, #<_scratch_buf       ; col_tab[metatile]
        ld      l, a
        adc     a, #>_scratch_buf
        sub     a, l
        ld      h, a
        ld      a, (hl)
        and     a, #63
        add     a, #<_spike_col_tab
        ld      l, a
        adc     a, #>_spike_col_tab
        sub     a, l
        ld      h, a
        ld      a, (hl)
        ret
    __endasm;
}

// Famidash's spike points for the 15x15 box, in the bit order of spike_probes: x, y from the
// top of the player (bg_coll_floor_spikes x+3 / x+12 at y+3 and y+14, bg_coll_death centre x+6,
// y+8, and the front side point bg_coll_R x+15, y+8)
static const uint8_t spike_pts[12] = { 3, 3, 12, 3, 3, 14, 12, 14, 6, 8, 15, 8 };
// Mini: Famidash's points (x+3 / x+5 at y+4 and y+9, the centre x+3, y+7, the front x+8 at front_y:
// 5, 7 or 10, see player_update)
static const uint8_t spike_pts_mini[3][12] = {
    { 3, 4, 5, 4, 3, 9, 5, 9, 3, 7, MINI_BOX_RIGHT, 5 },
    { 3, 4, 5, 4, 3, 9, 5, 9, 3, 7, MINI_BOX_RIGHT, 7 },
    { 3, 4, 5, 4, 3, 9, 5, 9, 3, 7, MINI_BOX_RIGHT, 10 },
};

// Nonzero if one of the spike points in mask (from spike_probes, not 0xFF) kills. Only the walk
// over the points is in assembly: the shape of each spike is spike_shape, in C.
// pts: x, y of each point (spike_pts, spike_pts_mini)
static uint8_t spike_hit(uint8_t mask, const uint8_t *pts) __naked {
    (void)mask;
    (void)pts;
    __asm
        ld      c, a                    ; (pts in de)
    1$:
        srl     c
        jr      C, 2$
        inc     de
        inc     de
        jr      NZ, 1$                  ; (inc de keeps the flags)
        xor     a, a
        ret
    2$:
        ld      a, (de)                 ; x of the point
        inc     de
        ld      l, a
        ld      a, (_player)
        and     a, #15
        add     a, l
        ld      l, a                    ; l: bit 4 = right column, low nibble = x in the metatile
        ld      a, (de)                 ; y of the point
        inc     de
        ld      h, a
        ld      a, (_player + 3)
        add     a, h
        ld      h, a                    ; h = y
        push    bc
        push    de
        ld      a, l
        swap    a
        and     a, #0xF0
        ld      c, a
        ld      a, h
        and     a, #0x0F
        or      a, c
        ld      e, a                    ; pack
        ld      a, l
        and     a, #0x10
        ld      c, a
        ld      a, h
        swap    a
        and     a, #0x0F
        or      a, c
        add     a, #<_collision_columns
        ld      l, a
        adc     a, #>_collision_columns
        sub     a, l
        ld      h, a
        ld      a, (hl)                 ; metatile
        call    _spike_shape
        pop     de
        pop     bc
        or      a, a
        ret     NZ
        jr      1$
    __endasm;
}

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
#define COL_AT(off, y) col_at((int16_t)(y), (off))
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
    player_draw_dy_step();
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
        // A brick breaks instead (GDA: the player's centre hitbox), solid on every other side
        uint8_t fy = (uint8_t)(py + front_y);
        if ((uint16_t)((int16_t)py + front_y) & 0xFF00u || col_tab[GET_COL_FAST(box_r)[fy >> 4]] != COL_BREAKABLE) {
            p->dead = 1;
            return 1;
        }
        brick_break((uint16_t)(p->world_x + box_r) >> 4, (uint8_t)((uint16_t)(p->y_base + fy) >> 4));
    }
    // Most frames touch no spike: look at the (up to 4) metatiles the spike probes below lie in
    // first, and only run the 6 spike_at calls when one of them is a spike type
    uint8_t spike_near = 1;
    uint8_t spike_mask = 0;            // full size: the probes spike_probes says can hit
    if (mini) {
        int16_t sy0 = (int16_t)py + 4;
        int16_t sy1 = (int16_t)py + 10;
        if (!(((uint16_t)sy0 | (uint16_t)sy1) & 0xFF00)) {
            uint8_t r0 = (uint8_t)sy0 >> 4, r1 = (uint8_t)sy1 >> 4;
            uint8_t k0 = col_tab[c0[r0]];
            uint8_t k1 = col_tab[c0[r1]];
            uint8_t k2 = col_tab[c1[r0]];
            uint8_t k3 = col_tab[c1[r1]];
            spike_near = spike_col_tab[k0 & 63u] | spike_col_tab[k1 & 63u] | spike_col_tab[k2 & 63u] | spike_col_tab[k3 & 63u];
        }
    } else {
        spike_mask = spike_probes();
        spike_near = spike_mask;
    }
    if (!spike_near) {
        // no spike probe can hit
    } else if (mini) {
        // Famidash's mini spike points: bg_coll_floor_spikes (x+3 / x+5 at the box top + 0 and
        // bottom - 2), bg_coll_death (centre x+3, y+7) and the front side point (x+8)
        if (py <= 245u) {
            // every point inside the collision window: the walk in assembly
            if (spike_hit(0x3F, spike_pts_mini[front_y == 5 ? 0 : front_y == 7 ? 1 : 2])) {
                p->dead = 1; return 1;
            }
        } else if (SPIKE(3, py + 4) || SPIKE(5, py + 4) ||
            SPIKE(3, py + 9) || SPIKE(5, py + 9) ||
            SPIKE(3, py + 7) || SPIKE(MINI_BOX_RIGHT, py + front_y)) {
            p->dead = 1; return 1;
        }
    } else if (spike_mask != 0xFF) {
        if (spike_hit(spike_mask, spike_pts)) {
            p->dead = 1; return 1;
        }
    } else {
        // at the bottom of the 256 px window, where some points are past it (see spike_pts)
        int16_t fy = (int16_t)py + 1;
        if (SPIKE(3, fy + 2) || SPIKE(12, fy + 2) || SPIKE(3, fy + 13) || SPIKE(12, fy + 13) ||
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
                p->ball_spin = -1;
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
    } else if (p->mode == MODE_BALL) {
        // GDA ball_gamemode: full speed on the ground, half in the air, backwards after a switch
        // (the step counts toward rolling with the gravity there is now, cube_turn)
        if (p->on_ground) p->ball_spin = 2;
        if (p->ball_spin < 0) {
            if (p->anim_timer < 5u) {
                p->anim_timer += 16;
                p->anim_frame = p->anim_frame ? (uint8_t)(p->anim_frame - 1u) : 23u;
            } else p->anim_timer -= 5;
        } else {
            p->anim_timer += (uint8_t)(p->ball_spin * 5);
            if (p->anim_timer >= 21) {
                p->anim_timer -= 21;
                if (++p->anim_frame >= 24) p->anim_frame = 0;
            }
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
