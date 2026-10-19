#pragma bank 13

#include <gb/gb.h>
#include "music.h"
#include <string.h>
#include <gbdk/font.h>

#include "sp_draw.h"
#include "gameplay.h"
#include "player.h"
#include "assets.h"
#include "famidash_sprites.h"
#include "famidash_bg.h"
#include "settings.h"

#define DEBUG_MODE
#include "famidash_metatiles.h"
#include "music_beats.h"

// The object cache is always gameplay.c's active_sp: address it directly. Through the
// SpCache* parameter SDCC reloads the pointer from the stack for every cache->x[i]
// access, which made the per-frame object loops several times slower (DMG frame drops).
extern SpCache active_sp;
#define cache (&active_sp)

// ID Mappings for SP Layer Logic
#define OBJ_CUBE_PORTAL   0
#define OBJ_SHIP_PORTAL   1
#define OBJ_BALL_PORTAL   2
#define OBJ_UFO_PORTAL    3
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
#define OBJ_PAD_PINK_UP   4    // Famidash 38 (tools/tmx2sprites.py)
#define OBJ_PAD_RED       20   // Famidash 0x52 (tools/tmx2sprites.py)
#define OBJ_PAD_RED_UP    21   // Famidash 0x53
#define OBJ_LEVEL_END     15
#define OBJ_MIRROR_PORTAL 126
#define OBJ_MIRROR_EXIT   121
#define OBJ_PORTAL_DN_HORIZ_DN  16
#define OBJ_PORTAL_DN_HORIZ_UP  17
#define OBJ_PORTAL_UP_HORIZ_DN  18
#define OBJ_PORTAL_UP_HORIZ_UP  19
#define OBJ_COIN1         7
#define OBJ_MINI_PORTAL   24
#define OBJ_GROW_PORTAL   25
#define OBJ_COIN2         26
#define OBJ_COIN3         27
#define OBJ_PULSING_BALL  73
#define OBJ_MUSIC_NOTE    74

static inline uint8_t coin_bit(uint8_t o) {
    return (o == OBJ_COIN1) ? 1 : (o == OBJ_COIN2) ? 2 : 4;
}

// Coins (Famidash): collected this attempt, and the flying-away animation per coin
uint8_t coins_collected;
uint8_t coins_saved;       // coins of this level already in the save (drawn as "X" coins)
static uint8_t coin_anim_timer[3];
static uint16_t coin_anim_x[3];
static uint16_t coin_anim_y[3];   // world y in 8.8 fixed point
static uint16_t coin_anim_speed[3];
static uint8_t coin_frame_ctr;

void coins_reset(void) BANKED {
    coins_collected = 0;
    coin_anim_timer[0] = coin_anim_timer[1] = coin_anim_timer[2] = 0;
}

// Orb / pad launch velocities per game mode, normal and mini size
// (Famidash sprite_gamemode_adjust_heights, 60fps value x 714/708).
// [mini][kind][mode]: kind 0 yellow orb, 1 yellow pad, 2 pink orb, 3 pink pad, 4 red pad;
// mode 0 cube, 1 ship, 2 ball, 3 UFO.
static const int16_t launch_force[2][5][4] = {
    { { JUMP_FORCE,         -1113, BALL_YELLOW_ORB, -952 },
      { PAD_JUMP_FORCE,      -968, BALL_YELLOW_PAD, -823 },
      { MAGENTA_JUMP_FORCE,  -516, BALL_PINK_ORB,   -549 },
      { PINK_PAD_FORCE,      -629, BALL_PINK_PAD,   -597 },
      { -2566,              -1581, -1597,          -1033 } },
    { { -1242, -1194, -1113, -984 },
      { -1678, -1081, -1242, -936 },
      {  -855,  -484,  -855, -436 },
      { -1017,  -484,  -920, -339 },
      { -2114, -1743, -1468, -1372 } },
};
#define LAUNCH(p, kind) launch_force[(p)->mini][(kind)][(p)->mode]

#define BG_TRIGGER_LEAD_TILES 10
#define BG_TRIGGER_LEAD_PX    ((BG_TRIGGER_LEAD_TILES) << 4)

extern const unsigned char FontPusab[];
#define FONT_PUSAB_START 0xD0

static uint8_t sp_has_drawn = 0;

// DMG draws the portals, orbs and pads, coins and mirror portals (the object cache keeps and
// draws only these there): bit (o & 7) of dmg_drawn_mask[o >> 3], for the object ids below 128.
static const uint8_t dmg_drawn_mask[16] = {
    0xE7, 0x7F, 0x3F, 0x0F, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42
};
static inline uint8_t is_dmg_drawn(uint8_t o) {
    return o < 128 && (dmg_drawn_mask[o >> 3] & (uint8_t)(1u << (o & 7)));
}

static uint8_t dmg_portal_art_ready;
static void dmg_portal_art_init(void);

void sp_cache_reset(SpCache *cache_arg, uint16_t *stream_idx) BANKED {
    uint8_t i;
    *stream_idx = 0;
    sp_has_drawn = 0;
    for (i = 0; i < MAX_ACTIVE_SP_OBJECTS; i++) cache->active[i] = 0;
    if (_cpu != CGB_TYPE && !dmg_portal_art_ready) dmg_portal_art_init();
}

// The cache update is split in two halves that run in different frames (see
// play_level): retiring objects behind the camera, then loading the new ones.
// Together with the per-frame object work they could overrun a DMG frame.
// Retires the cache entries behind the camera (px < cr_keep) and the activated ones that are
// not drawn (any trigger; on DMG the objects it does not draw), and compacts the rest. The
// active entries are always the first ones. Hand-written: in C it took up to ~20k dots (30% of
// a DMG frame) with a full cache. SpCache layout: obj +0, px +16, py +48, active +80, activated +96
// (entry b of an array at offset off: _active_sp + off + b, the 16-bit ones at 2b).
static uint16_t cr_keep;
static uint8_t cr_dmg;
static const uint8_t bit_of[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
static void cache_retire(void) __naked {
    __asm
        ld      bc, #0                  ; b = read index, c = write index
    1$:
        ld      a, b
        cp      a, #MAX_ACTIVE_SP_OBJECTS
        jp      NC, 8$
        ld      a, b
        add     a, #<(_active_sp + 80)
        ld      l, a
        ld      a, #>(_active_sp + 80)
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        or      a, a
        jp      Z, 8$                   ; first inactive entry: done
        ld      a, b
        add     a, #<(_active_sp + 96)
        ld      l, a
        ld      a, #>(_active_sp + 96)
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        or      a, a
        jr      Z, 3$                   ; not activated: keep if not behind
        ld      a, b
        add     a, #<(_active_sp + 0)
        ld      l, a
        ld      a, #>(_active_sp + 0)
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        cp      a, #128
        jp      NC, 7$                  ; an activated trigger: retire
        ld      a, (_cr_dmg)
        or      a, a
        jr      Z, 3$
        ld      a, (hl)                 ; DMG: retire if it is not drawn
        ld      e, a
        srl     a
        srl     a
        srl     a
        add     a, #<_dmg_drawn_mask
        ld      l, a
        ld      a, #0
        adc     a, #>_dmg_drawn_mask
        ld      h, a
        ld      d, (hl)
        ld      a, e
        and     a, #7
        add     a, #<_bit_of
        ld      l, a
        ld      a, #0
        adc     a, #>_bit_of
        ld      h, a
        ld      a, (hl)
        and     a, d
        jp      Z, 7$
    3$:
        ; behind the camera (px < cr_keep): retire
        ld      a, b
        add     a, a
        add     a, #<(_active_sp + 16)
        ld      l, a
        ld      a, #>(_active_sp + 16)
        adc     a, #0
        ld      h, a
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)                 ; de = px
        ld      a, (_cr_keep)
        ld      l, a
        ld      a, e
        sub     a, l
        ld      a, (_cr_keep + 1)
        ld      l, a
        ld      a, d
        sbc     a, l
        jr      C, 7$
        ; keep: move it to the write index when they differ
        ld      a, b
        cp      a, c
        jr      Z, 6$
        push    bc
        ld      a, b
        add     a, #<(_active_sp + 0)
        ld      l, a
        ld      a, #>(_active_sp + 0)
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        ld      d, a
        ld      b, c
        ld      a, b
        add     a, #<(_active_sp + 0)
        ld      l, a
        ld      a, #>(_active_sp + 0)
        adc     a, #0
        ld      h, a
        ld      (hl), d
        pop     bc
        push    bc
        ld      a, b
        add     a, #<(_active_sp + 96)
        ld      l, a
        ld      a, #>(_active_sp + 96)
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        ld      d, a
        ld      b, c
        ld      a, b
        add     a, #<(_active_sp + 96)
        ld      l, a
        ld      a, #>(_active_sp + 96)
        adc     a, #0
        ld      h, a
        ld      (hl), d
        pop     bc
        push    bc                      ; px and py (16 bit)
        ld      a, b
        add     a, a
        ld      b, a
        ld      a, b
        add     a, #<(_active_sp + 16)
        ld      l, a
        ld      a, #>(_active_sp + 16)
        adc     a, #0
        ld      h, a
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)
        ld      a, c
        add     a, a
        ld      b, a
        ld      a, b
        add     a, #<(_active_sp + 16)
        ld      l, a
        ld      a, #>(_active_sp + 16)
        adc     a, #0
        ld      h, a
        ld      a, e
        ld      (hl+), a
        ld      (hl), d
        pop     bc
        push    bc
        ld      a, b
        add     a, a
        ld      b, a
        ld      a, b
        add     a, #<(_active_sp + 48)
        ld      l, a
        ld      a, #>(_active_sp + 48)
        adc     a, #0
        ld      h, a
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)
        ld      a, c
        add     a, a
        ld      b, a
        ld      a, b
        add     a, #<(_active_sp + 48)
        ld      l, a
        ld      a, #>(_active_sp + 48)
        adc     a, #0
        ld      h, a
        ld      a, e
        ld      (hl+), a
        ld      (hl), d
        pop     bc
    6$:
        inc     c
    7$:
        inc     b
        jp      1$
    8$:
        ; entries c .. 15: inactive
        ld      b, c
    9$:
        ld      a, b
        cp      a, #MAX_ACTIVE_SP_OBJECTS
        ret     NC
        ld      a, b
        add     a, #<(_active_sp + 80)
        ld      l, a
        ld      a, #>(_active_sp + 80)
        adc     a, #0
        ld      h, a
        ld      (hl), #0
        inc     b
        jr      9$
    __endasm;
}

void sp_cache_retire(uint16_t cam_px) BANKED {
    cr_keep = (cam_px >= 48u) ? (uint16_t)(cam_px - 48u) : 0;   // px + 48 >= cam_px
    cr_dmg = (_cpu != CGB_TYPE);
    cache_retire();
}

void sp_cache_fill(const Level *l, uint16_t cam_px, uint16_t *stream_idx) BANKED {
    uint8_t i;
    const SpDef *sp_list = (_cpu == CGB_TYPE || !l->sp_list_dmg) ? l->sp_list : l->sp_list_dmg;

    sp_cache_load(l->sp_bank, sp_list, cam_px, cache, stream_idx, l->map_height);

    // Only draw_sprites on DMG reads sp_has_drawn
    sp_has_drawn = 0;
    if (_cpu == CGB_TYPE) return;
    for (i = 0; i < MAX_ACTIVE_SP_OBJECTS; i++) {
        if (!cache->active[i]) break;
        uint8_t o = cache->obj[i];
        if (o < 128 && is_dmg_drawn(o)) {
            sp_has_drawn = 1;
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

// Pads and orbs (CGB), animated like Famidash's (YELLOW_PAD_SPRITES, YELLOW_ORB_SPRITES...):
// 4 frames of 5 video frames over 4 tile pairs a, b, c, d; frame f shows pair f on the left and
// pair f + 1 mirrored on the right (a b, b c, c d, d a), so the dots drift. a, b: the
// metasprite's tiles (VRAM bank 0); c, d: VRAM bank 1 from anim_x (tools/build_sprite_tiles.py)
#define ANIM_FRAME_TICKS 5
static uint8_t obj_anim_tick;  // 0 .. ANIM_FRAME_TICKS - 1, one step per frame (draw_sprites)
static uint8_t obj_anim_f;     // the frame, 0 .. 3

static uint8_t draw_oam_anim2(const metasprite_t* meta, uint8_t anim_x, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t t[4], b[4];
    t[0] = meta[0].dtile + FAMIDASH_SPRITE_TILE_BASE; b[0] = 0;
    t[1] = meta[1].dtile + FAMIDASH_SPRITE_TILE_BASE; b[1] = 0;
    t[2] = anim_x;      b[2] = S_BANK;
    t[3] = anim_x + 2;  b[3] = S_BANK;
    uint8_t f = obj_anim_f;
    uint8_t g = (uint8_t)(f + 1u) & 3u;
    uint8_t flip = reversed ? S_FLIPX : 0;
    *oam++ = sy; *oam++ = reversed ? (uint8_t)(sx + 8) : sx;
    *oam++ = t[f]; *oam++ = (meta[0].props | b[f]) ^ flip;
    *oam++ = sy; *oam++ = reversed ? sx : (uint8_t)(sx + 8);
    *oam++ = t[g]; *oam++ = (meta[1].props | b[g]) ^ flip;
    return 2;
}

// First VRAM bank 1 tile of the object's animation frames 2, 3; 0: not animated
static uint8_t obj_anim_tiles(uint8_t obj) {
    switch (obj) {
        case OBJ_PAD_YELLOW: case OBJ_PAD_YELLOW_UP: case OBJ_PAD_BLUE: case OBJ_PAD_BLUE_UP:
        case OBJ_PAD_RED: case OBJ_PAD_RED_UP:
            return 234;   // Famidash $FB $FD
        case OBJ_ORB_YELLOW: case OBJ_ORB_BLUE:
            return 238;   // $9D $9F
        case OBJ_ORB_PINK:
            return 242;   // $BD $BF
        case OBJ_PAD_PINK: case OBJ_PAD_PINK_UP:
            return 246;   // $5D $5F
    }
    return 0;
}

// 2x3 metasprite (gravity portals)
// Portal columns (bit c = art column c) for draw_oam_2x3 / 3x3 / mini_portal. CGB: the column
// with the ring's far arc is drawn before the player (draw_portal_fronts), the rest after it, so
// the player goes through the ring instead of over it.
#define COLS_ALL 0xFF

#define PUT_SPR(x, m) { *oam++ = y; *oam++ = (x); *oam++ = (m)->dtile + tile_base; *oam++ = (m)->props ^ flip; }

static uint8_t draw_oam_2x3(const metasprite_t* meta, uint8_t tile_base, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed, uint8_t cols) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t *oam0 = oam;
    uint8_t flip = reversed ? S_FLIPX : 0;
    uint8_t x0 = reversed ? (uint8_t)(sx + 8) : sx;
    uint8_t x1 = reversed ? sx : (uint8_t)(sx + 8);
    uint8_t y = sy;
    for (uint8_t row = 0; row < 3; row++, meta += 2, y += 16) {
        if (cols & 1) PUT_SPR(x0, meta);
        if (cols & 2) PUT_SPR(x1, meta + 1);
    }
    return (uint8_t)((uint8_t)(oam - oam0) >> 2);
}

// 3x3 metasprite (cube/ship portals)
static uint8_t draw_oam_3x3(const metasprite_t* meta, uint8_t tile_base, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed, uint8_t cols) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t *oam0 = oam;
    uint8_t flip = reversed ? S_FLIPX : 0;
    uint8_t x0 = reversed ? (uint8_t)(sx + 16) : sx;
    uint8_t x1 = (uint8_t)(sx + 8);
    uint8_t x2 = reversed ? sx : (uint8_t)(sx + 16);
    uint8_t y = sy;
    for (uint8_t row = 0; row < 3; row++, meta += 3, y += 16) {
        if (cols & 1) PUT_SPR(x0, meta);
        if (cols & 2) PUT_SPR(x1, meta + 1);
        if (cols & 4) PUT_SPR(x2, meta + 2);
    }
    return (uint8_t)((uint8_t)(oam - oam0) >> 2);
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

// Pulsing rods (Famidash "lights": a dotted pole with a ball on top), like GD's pulse rods: their
// ball tops (D_C9 / D_CB / D_CD) swell to 7 px on the music's hits and shrink back to 3 px
// (tools/make_music_beats.py, tools/build_sprite_tiles.py ROD_PULSE). All rods share one size.
#define ROD_PULSE_STRONG 12     // frames: 4 at 7 px, 4 at 5 px, then 3 px
#define ROD_PULSE_WEAK   10     // 2 at 7 px, 4 at 5 px
#define ROD_PULSE_5PX    4      // above: 5 px, above ROD_PULSE_5PX + 4: 7 px
#define ROD_TILE_7PX (FAMIDASH_SPRITE_TILE_BASE)
#define ROD_TILE_5PX (222 - D_C9)
#define ROD_TILE_3PX (228 - D_C9)
static uint8_t rod_pulse;
static uint8_t rod_tile = (uint8_t)ROD_TILE_3PX;   // + D_C9 / D_CB / D_CD: the ball tops shown
// the pulsing ball decoration (D_PB) pulses with the rods: 7 px 250, 5 px 252, 3 px 254
#define PB_TILE_7PX (FAMIDASH_SPRITE_TILE_BASE + D_PB)
static uint8_t pb_tile = (uint8_t)(PB_TILE_7PX + 4);

static void rod_pulse_update(void) {
    uint8_t hit = music_beats_poll();
    uint8_t fur_hit = fur_beats_poll();   // the Furnace song's timeline (furnace_songs.c)
    if (fur_hit > hit) hit = fur_hit;
    if (hit == MUSIC_HIT_STRONG) rod_pulse = ROD_PULSE_STRONG;
    else if (hit && rod_pulse < ROD_PULSE_WEAK) rod_pulse = ROD_PULSE_WEAK;
    else if (rod_pulse) rod_pulse--;
    rod_tile = (rod_pulse > ROD_PULSE_5PX + 4) ? ROD_TILE_7PX : (rod_pulse > ROD_PULSE_5PX) ? ROD_TILE_5PX : ROD_TILE_3PX;
    pb_tile = (uint8_t)(PB_TILE_7PX + ((rod_pulse > ROD_PULSE_5PX + 4) ? 0 : (rod_pulse > ROD_PULSE_5PX) ? 2 : 4));
}

// tile of a deco sprite: a rod's ball top or the pulsing ball in the size shown
#define DECO_TILE(t, tile_base) ((uint8_t)((t) == D_PB ? pb_tile : (uint8_t)((t) - D_C9) <= (uint8_t)(D_CD - D_C9) ? rod_tile + (t) : (tile_base) + (t)))

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
        *oam++ = sy + dy[0]; *oam++ = sx + dx[0]; *oam++ = DECO_TILE(dt[0], tile_base); *oam++ = dp[0];
        if (count > 1) {
            *oam++ = sy + dy[1]; *oam++ = sx + dx[1]; *oam++ = DECO_TILE(dt[1], tile_base); *oam++ = dp[1];
            if (count > 2) {
                *oam++ = sy + dy[2]; *oam++ = sx + dx[2]; *oam++ = DECO_TILE(dt[2], tile_base); *oam++ = dp[2];
            }
        }
    } else {
        uint8_t rx = sx + deco->width - 8;
        *oam++ = sy + dy[0]; *oam++ = rx - dx[0]; *oam++ = DECO_TILE(dt[0], tile_base); *oam++ = dp[0] ^ S_FLIPX;
        if (count > 1) {
            *oam++ = sy + dy[1]; *oam++ = rx - dx[1]; *oam++ = DECO_TILE(dt[1], tile_base); *oam++ = dp[1] ^ S_FLIPX;
            if (count > 2) {
                *oam++ = sy + dy[2]; *oam++ = rx - dx[2]; *oam++ = DECO_TILE(dt[2], tile_base); *oam++ = dp[2] ^ S_FLIPX;
            }
        }
    }
    return count;
}

// 4 columns x 3 rows (12 8x16 hardware sprites = 32x48 pixels, tools/make_mirror_portal_gfx.py).
// The art is mirrored top to bottom: row 0 is stored, row 1 is the middle, row 2 is row 0 drawn
// with S_FLIPY. The exit art faces the same way as the entrance (CGB: its own tiles, ring in
// colour 3; the DMG exit is the entrance tiles). Both are drawn flipped in the mirrored view
// (reversed), where the world box [x, x + 32) is on screen at [sx - 16, sx + 16).
static uint8_t draw_oam_mirror_portal(uint8_t obj, uint8_t tile_base, uint8_t oam_idx,
                                      uint8_t sx, uint8_t sy, uint8_t reversed) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t t_base = tile_base + MIRROR_PORTAL_ENTER_TILE;
    if (obj != OBJ_MIRROR_PORTAL && _cpu == CGB_TYPE)
        t_base = tile_base + MIRROR_PORTAL_EXIT_TILE;
    uint8_t pal = S_PAL(6);   // entrance and exit share palette 6 (see gbc_palettes.c)
    uint8_t props = reversed ? (pal | S_FLIPX) : pal;
    int8_t dx = reversed ? -8 : 8;

    for (uint8_t row = 0; row < 3; row++) {
        uint8_t x = reversed ? (uint8_t)(sx + 8) : sx;
        uint8_t y = (uint8_t)(sy + (row << 4));
        uint8_t first = (row == 1) ? (uint8_t)(t_base + 8) : t_base;
        uint8_t pr = (row == 2) ? (props ^ S_FLIPY) : props;
        for (uint8_t k = 0; k < 4; k++) {
            *oam++ = y; *oam++ = x; *oam++ = first + (uint8_t)(k << 1); *oam++ = pr;
            x += dx;
        }
    }
    return 12;
}

// Player box of the current process_sprite_logic() call, for touch_object(): Famidash's
// (sprite_collide): x + 1 .. x + 1 + width (15, mini 8), centred vertically (mini 7 tall at +4)
static uint16_t t_px, t_front, t_py, t_bottom;

// Famidash check_collision against the object's box (sprite_x/y_offset, sprite_widths/heights):
// x + xo .. x + xo + w, y + yo .. y + yo + h, edges included
static uint8_t touch_box(uint16_t ox, uint16_t oy, uint8_t xo, uint8_t w, int8_t yo, uint8_t h) {
    uint16_t l = ox + xo;
    uint16_t t = oy + (int16_t)yo;
    return l <= t_front && t_px <= l + w && t <= t_bottom && t_py <= t + h;
}

// Player overlaps the object horizontally: portals, pads, orbs. act = its activated flag.
// (Separate from process_sprite_logic: SDCC compiles one huge function very slowly.)
static void touch_object(Player* p_arg, uint8_t obj, uint16_t obj_x, uint16_t obj_y, uint8_t joy, uint8_t *act) {
    (void)p_arg;
#define p (&player)   // always &player: fixed addresses (player.h)
    switch (obj) {
        case OBJ_MINI_PORTAL:
        case OBJ_GROW_PORTAL:
            // Famidash: 8px right of the object, 16 x 52
            if (touch_box(obj_x, obj_y, 8, 16, -2, 52)) {
                if (!*act) {
                    p->mini = (obj == OBJ_MINI_PORTAL);
                    *act = 1;
                }
            }
            break;

        case OBJ_CUBE_PORTAL:
        case OBJ_SHIP_PORTAL:
        case OBJ_BALL_PORTAL:
        case OBJ_UFO_PORTAL:
            // Famidash mode portal: 16 x 52 (obj_y - 2 to obj_y + 50)
            if (touch_box(obj_x, obj_y, 0, 16, -2, 52)) {
                if (!*act) {
                    // object ids 0..3 are the modes' ids (cube, ship, ball, UFO)
                    uint8_t mode = obj;
                    // Famidash: the ship, ball and UFO portals halve the speed when the mode
                    // changes, the cube portal keeps it
                    if (mode != MODE_CUBE && mode != p->mode) p->vel_y.w /= 2;
                    p->mode = mode;
                    cam_portal_y = obj_y;
                    *act = 1;
                }
            }
            break;

        case OBJ_GRAVITY_DOWN:
        case OBJ_GRAVITY_UP:
            // Famidash gravity portal: 14 x 40 at +1, +4
            if (touch_box(obj_x, obj_y, 1, 14, 4, 40)) {
                if (!*act) {
                    uint8_t target_flipped = (obj == OBJ_GRAVITY_UP);
                    if (p->gravity_flipped != target_flipped) {
                        p->gravity_flipped = target_flipped;
                        p->vel_y.w /= 2;   // Famidash spcl_gvity_portal_common
                    }
                    *act = 1;
                }
            }
            break;

        case OBJ_PAD_YELLOW:
        case OBJ_PAD_PINK:
        case OBJ_PAD_BLUE:
        case OBJ_PAD_YELLOW_UP:
        case OBJ_PAD_BLUE_UP:
        case OBJ_PAD_PINK_UP:
        case OBJ_PAD_RED:
        case OBJ_PAD_RED_UP:
        {
            // Famidash: 15 x 3, at +5 on the floor (+ the 8 of its floor pad offset, which the
            // level export leaves out, see tools/tmx2sprites.py), at +0 on the ceiling
            uint8_t is_ceiling = (obj == OBJ_PAD_YELLOW_UP || obj == OBJ_PAD_BLUE_UP || obj == OBJ_PAD_PINK_UP || obj == OBJ_PAD_RED_UP);
            if (touch_box(obj_x, obj_y, 0, 15, is_ceiling ? 0 : 13, 3)) {
                // Famidash pad_stuff: yellow / pink pads launch on every frame the player
                // touches them (going up through one, it launches twice); blue pads only while
                // the gravity is still the one they flip
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
                } else if (obj == OBJ_PAD_PINK || obj == OBJ_PAD_PINK_UP) {
                    int16_t force = LAUNCH(p, 3);
                    p->vel_y.w = (p->gravity_flipped) ? -force : force;
                    p->on_ground = 0;
                } else if (obj == OBJ_PAD_RED || obj == OBJ_PAD_RED_UP) {
                    int16_t force = LAUNCH(p, 4);
                    p->vel_y.w = (p->gravity_flipped) ? -force : force;
                    p->on_ground = 0;
                } else {
                    int16_t force = LAUNCH(p, 1);
                    p->vel_y.w = (p->gravity_flipped) ? -force : force;
                    p->on_ground = 0;
                }
            }
            break;
        }

        case OBJ_ORB_YELLOW:
        case OBJ_ORB_PINK:
        case OBJ_ORB_BLUE:
        {
            if (joy & J_A) {
                // Famidash: 16 x 18 at -1
                if ((!(p->last_joy & J_A) || p->orb_buffered) && touch_box(obj_x, obj_y, 0, 16, -1, 18)) {
                    if (!*act) {
                        *act = 1;
                        p->orb_buffered = 0; // Clear buffer after hit
                        p->ufo_orbed = 1;    // the UFO does not also jump on this press
                        if (obj == OBJ_ORB_BLUE) {
                            p->gravity_flipped = !p->gravity_flipped;
                            int16_t force = (p->mode == MODE_BALL) ? BLUE_ORB_FORCE : BLUE_PAD_FORCE;
                            p->vel_y.w = (p->gravity_flipped) ? -force : force;
                        } else if (obj == OBJ_ORB_PINK) {
                            int16_t force = LAUNCH(p, 2);
                            p->vel_y.w = (p->gravity_flipped) ? -force : force;
                        } else {
                            int16_t force = LAUNCH(p, 0);
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
            if (touch_box(obj_x, obj_y, 0, 15, -1, 46)) {
                if (!*act) {
                    p->reversed = (obj == OBJ_MIRROR_PORTAL) ? 1 : 0;
                    *act = 1;
                }
            }
            break;
    }
#undef p
}

// Object logic pre-pass, hand-written like the draw loops: the C loop below ran its chain of checks
// on every cached object every frame (SDCC keeps its locals on the stack: ~600 cycles for an
// object near the player), up to 30% of a DMG frame in dense parts (Xstep's pads, portal runs). This lists, in cache order, only the entries the C loop could act on: the
// level end, triggers, coins, horizontal gravity portals and the objects overlapping the player
// in x (touch_object). It skips the ones the C loop would pass over with a continue (activated,
// behind lim_back, decorations, past lim_near, out of reach vertically, not overlapping) and stops
// where it would break (inactive, past lim_ahead, past lim_lead, past p_front + 16), so the C loop
// does the same. Positions are taken relative to lim_back (one 16-bit subtraction per entry).
static uint16_t psl_back, psl_py20;
// the limits minus lim_back (all < 256)
static uint8_t psl_rel_ahead, psl_rel_lead, psl_rel_near, psl_rel_tfront, psl_rel_pf16, psl_rel_x24;
static uint8_t psl_obj, psl_rel;
static uint8_t psl_n;
static uint8_t psl_idx[MAX_ACTIVE_SP_OBJECTS];
static void psl_filter(void) __naked {
    __asm
        ld      bc, #0                  ; b = cache index, c = entries listed
    00001$:
        ld      a, b
        cp      a, #MAX_ACTIVE_SP_OBJECTS
        jp      NC, 00009$
        ld      e, b
        ld      d, #0
        ; active[b]: the active entries are the first ones
        ld      hl, #(_active_sp + 80)
        add     hl, de
        ld      a, (hl)
        or      a, a
        jp      Z, 00009$
        ; activated[b]: skip
        ld      hl, #(_active_sp + 96)
        add     hl, de
        ld      a, (hl)
        or      a, a
        jp      NZ, 00008$
        ; psl_obj = obj[b]
        ld      hl, #_active_sp
        add     hl, de
        ld      a, (hl)
        ld      (_psl_obj), a
        ; e = rel = px[b] - psl_back
        ld      hl, #(_active_sp + 16)
        add     hl, de
        add     hl, de
        ld      a, (_psl_back)
        ld      e, a
        ld      a, (hl+)
        sub     a, e
        ld      e, a
        ld      a, (_psl_back + 1)
        ld      d, a
        ld      a, (hl)
        sbc     a, d
        jr      NC, 00002$
        ; behind lim_back: the level end to C, the rest skipped
        ld      a, (_psl_obj)
        cp      a, #OBJ_LEVEL_END
        jp      Z, 00007$
        jp      00008$
    00002$:
        jp      NZ, 00009$              ; rel >= 256: past lim_ahead, done
        ld      a, (_psl_obj)
        ld      d, a                    ; d = obj
        cp      a, #OBJ_LEVEL_END
        jr      NZ, 00003$
        ld      a, (_psl_rel_ahead)     ; the level end: to C up to lim_ahead
        cp      a, e
        jp      C, 00009$
        jp      00007$
    00003$:
        ld      a, (_psl_rel_lead)      ; past lim_lead: done
        cp      a, e
        jp      C, 00009$
        ld      a, d
        cp      a, #128                 ; triggers: to C
        jp      NC, 00007$
        cp      a, #OBJ_MUSIC_NOTE      ; decorations (38..63, the pulsing ball), the music note: skip
        jp      Z, 00008$
        cp      a, #OBJ_PULSING_BALL
        jp      Z, 00008$
        sub     a, #38
        cp      a, #26
        jp      C, 00008$
        ld      a, (_psl_rel_near)      ; past lim_near: skip
        cp      a, e
        jp      C, 00008$
        ld      a, e
        ld      (_psl_rel), a
        ; out of reach vertically (dy = player y - py[b] not in -20..50): skip
        ld      a, b
        add     a, a
        ld      e, a
        ld      d, #0
        ld      hl, #(_active_sp + 48)
        add     hl, de
        ld      a, (_psl_py20)
        sub     a, (hl)
        ld      e, a
        inc     hl
        ld      a, (_psl_py20 + 1)
        sbc     a, (hl)
        jp      NZ, 00008$
        ld      a, e
        cp      a, #71
        jp      NC, 00008$
        ; coins and horizontal gravity portals (16..19): to C
        ld      a, (_psl_obj)
        cp      a, #OBJ_COIN1
        jp      Z, 00007$
        cp      a, #OBJ_COIN2
        jp      Z, 00007$
        cp      a, #OBJ_COIN3
        jp      Z, 00007$
        sub     a, #16
        cp      a, #4
        jp      C, 00007$
        ; overlapping the player in x (obj_x <= t_front, px <= obj_x + 24): to C (touch_object)
        ld      a, (_psl_rel)
        ld      e, a
        ld      a, (_psl_rel_tfront)
        cp      a, e
        jr      C, 00004$
        ld      a, (_psl_rel_x24)
        ld      d, a
        ld      a, e
        cp      a, d
        jp      NC, 00007$
        jp      00008$
    00004$:
        ld      a, (_psl_rel_pf16)      ; obj_x > p_front + 16: the C loop stops there
        cp      a, e
        jp      C, 00009$
        jp      00008$
    00007$:
        ld      hl, #_psl_idx
        ld      e, c
        ld      d, #0
        add     hl, de
        ld      (hl), b
        inc     c
    00008$:
        inc     b
        jp      00001$
    00009$:
        ld      a, c
        ld      (_psl_n), a
        ret
    __endasm;
}

// Background colour trigger (object ids 128..175)
static void bg_trigger(uint8_t obj, uint8_t *target_bg_idx) {
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
}

void process_sprite_logic(
        SpCache *cache_arg, uint16_t cam_px,
        Player* p_arg, uint8_t joy, uint8_t* target_bg_idx
) BANKED {
    (void)p_arg;
#define p (&player)   // always &player: fixed addresses (player.h)
    uint8_t i;
    uint16_t px = p->world_x;
    uint16_t py = p->y_base + p->world_y.b.h;

    // Player box (mini: 8x7 at +4 like Famidash)
    uint16_t p_front = px + (p->mini ? MINI_BOX_RIGHT : 15u);
    uint16_t p_bottom = py + (p->mini ? (MINI_BOX_BOTTOM - 1) : PLAYER_SIZE);
    if (p->mini) py += MINI_BOX_TOP;
    t_px = px + 1u;
    t_front = p_front + 1u;
    if (p->mini) {
        t_py = py;
        t_bottom = (uint16_t)(py + 7u);
    } else {
        // Famidash sprite_collide: the 15x15 box (16 rows: check_collision is inclusive) at its
        // player y, our py + 1 (see front_y in player.c)
        t_py = (uint16_t)(py + 1u);
        t_bottom = (uint16_t)(t_py + 15u);
    }
    // Loop limits, computed once (SDCC recomputes 16-bit sums on every pass otherwise)
    uint16_t lim_ahead = cam_px + 176u;
    uint16_t lim_lead = px + BG_TRIGGER_LEAD_PX;
    uint16_t lim_near = px + 48u;
    uint16_t lim_back = (px >= 48u) ? (uint16_t)(px - 48u) : 0;   // obj_x + 48 < px

    psl_back = lim_back;
    psl_py20 = py + 20u;   // (uint16_t)(dy + 20) <= 70: the dy test below
    uint8_t fast = ((uint16_t)(lim_ahead - lim_back) < 256u && lim_lead >= lim_back && lim_near >= lim_back);
    if (fast) {
        // always (px is cam_px: 224, 208, 96 at most)
        psl_rel_ahead = (uint8_t)(lim_ahead - lim_back);
        psl_rel_lead = (uint8_t)(lim_lead - lim_back);
        psl_rel_near = (uint8_t)(lim_near - lim_back);
        psl_rel_tfront = (uint8_t)(t_front - lim_back);
        psl_rel_pf16 = (uint8_t)(p_front + 16u - lim_back);
        psl_rel_x24 = (px >= lim_back + 24u) ? (uint8_t)(px - 24u - lim_back) : 0;   // px <= obj_x + 24
        psl_filter();
    } else {
        for (psl_n = 0; psl_n < MAX_ACTIVE_SP_OBJECTS; psl_n++) psl_idx[psl_n] = psl_n;
    }
    for (uint8_t k = 0; k < psl_n; k++) {
        i = psl_idx[k];
        if (fast) {
            // psl_filter listed it, so the checks below up to its branch pass: go there directly
            // for the common ones (an object overlapping the player, a colour trigger)
            uint8_t o = cache->obj[i];
            if (o >= 128) {
                if (o <= 175) {
                    bg_trigger(o, target_bg_idx);
                    cache->activated[i] = 1;
                    continue;
                }
            } else if (o != OBJ_LEVEL_END && o != OBJ_COIN1 && o != OBJ_COIN2 && o != OBJ_COIN3 &&
                       (uint8_t)(o - 16u) >= 4u) {
                touch_object(p, o, cache->px[i], cache->py[i], joy, &cache->activated[i]);
                continue;
            }
        }
        if (!cache->active[i]) break;
        if (cache->activated[i]) continue;

        uint16_t obj_x = cache->px[i];
        if (obj_x > lim_ahead) break;

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

        if (obj != OBJ_LEVEL_END && obj_x > lim_lead) break;

        if (cache->activated[i]) continue;
        if (obj_x < lim_back) continue;

        if (obj >= 38 && obj < 64) continue;
        if (obj == OBJ_MUSIC_NOTE || obj == OBJ_PULSING_BALL) continue;

        if (obj == OBJ_COIN1 || obj == OBJ_COIN2 || obj == OBJ_COIN3) {
            uint16_t cy = cache->py[i];
            if (obj_x <= p_front && px <= obj_x + 15u && py <= cy + 15u && p_bottom >= cy) {
                uint8_t n = (obj == OBJ_COIN1) ? 0 : (obj == OBJ_COIN2) ? 1 : 2;
                cache->activated[i] = 1;
                coins_collected |= coin_bit(obj);
                coin_anim_timer[n] = 1;
                coin_anim_x[n] = obj_x;
                coin_anim_y[n] = cy << 8;
                coin_anim_speed[n] = 0x0200;
            }
            continue;
        }

        if (obj >= 128 && obj <= 175) {
            if (lim_lead >= obj_x) {
                bg_trigger(obj, target_bg_idx);
                cache->activated[i] = 1;
            }
            continue;
        }

        if (obj >= 176 && obj <= 191) {
            // block outline colour (CGB: the level lists of DMG leave these out)
            if (lim_lead >= obj_x) {
                if (_cpu == CGB_TYPE) famidash_apply_outline_trigger((uint8_t)(obj - 176));
                cache->activated[i] = 1;
            }
            continue;
        }

        if (obj >= 192 && obj <= 239) {
            if (lim_lead >= obj_x) {
                if (_cpu == CGB_TYPE) {
                    uint8_t pal_idx = (uint8_t)(obj - 192);
                    famidash_apply_g_trigger(pal_idx);
                }
                cache->activated[i] = 1;
            }

            continue;
        }

        if (obj_x > lim_near) continue;

        uint16_t obj_y = cache->py[i];

        int16_t dy = (int16_t)py - (int16_t)obj_y;
        if (dy > 50 || dy < -20) continue;

        if (obj >= 16 && obj <= 19) {
            // horizontal gravity portal (48px wide art): Famidash box 40 x 14 at +4, +1
            if (touch_box(obj_x, obj_y, 4, 40, 1, 14)) {
                uint8_t target_flipped = (obj >= 18);
                if (p->gravity_flipped != target_flipped) {
                    p->gravity_flipped = target_flipped;
                    p->vel_y.w /= 2;   // Famidash spcl_gvity_portal_common
                }
                cache->activated[i] = 1;
            }
        } else if (obj_x <= t_front && px <= obj_x + 24u) {
            // (the mini / growth portal box is 8px right of the object: touch_object checks x)
            touch_object(p, obj, obj_x, obj_y, joy, &cache->activated[i]);
        } else if (obj_x > p_front + 16) {
            break;
        }
    }
#undef p
}

// Mini / growth portal (Famidash Mini_Portal / Growth_Portal: 7 8x16 sprites)
static uint8_t draw_oam_mini_portal(uint8_t obj, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed, uint8_t cols) {
    static const int8_t mx[7] = { 0, 8, -8, 0, 8, 0, 8 };
    static const uint8_t my[7] = { 0, 0, 16, 16, 16, 32, 32 };
    static const uint8_t mt[7] = { MINI_PORTAL_TILE_A, MINI_PORTAL_TILE_B, MINI_PORTAL_TILE_B + 2,
                                   MINI_PORTAL_TILE_B + 4, MINI_PORTAL_TILE_B + 6,
                                   MINI_PORTAL_TILE_A, MINI_PORTAL_TILE_B };
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t grow = (obj == OBJ_GROW_PORTAL);
    uint8_t pal = grow ? S_PAL(1) : S_PAL(4);
    uint8_t n = 0;
    for (uint8_t k = 0; k < 7; k++) {
        int8_t x = mx[k];
        if (!(cols & (uint8_t)(1u << ((uint8_t)(x + 8) >> 3)))) continue;   // columns -8, 0, 8
        n++;
        if (grow) x += 8;                     // growth portal art starts 8px further right
        if (reversed) x = (int8_t)((grow ? 16 : 0) - x);  // mirror inside the portal box
        *oam++ = sy + my[k];
        *oam++ = (uint8_t)(sx + x);
        *oam++ = mt[k];
        *oam++ = pal | (k >= 5 ? S_FLIPY : 0) | (reversed ? S_FLIPX : 0);
    }
    return n;
}

// Coin (tools/make_coin_tiles.py): 6 spin frames, 5 video frames each, 2 sprites (frame 3,
// edge on: 1 sprite, 4px in). Tile pair p: CGB COIN_TILE_BASE + 2p (VRAM bank 1); DMG
// dmg_coin_pair[p]. A coin already in the save ("gotten") is silver on CGB (the HUD text
// palette) and flickers on DMG (half transparent on the LCD).
#define COIN_FRAME_TICKS 5
static const uint8_t coin_frame_l[6] = { 0, 2, 4, 6, 7, 9 };
static const uint8_t coin_frame_r[6] = { 1, 3, 5, 0xFF, 8, 10 };
static const uint8_t coin_frame_x[6] = { 0, 0, 0, 4, 0, 0 };
static const uint8_t dmg_coin_pair[11] = {
    DMG_COIN_TILE_BASE, DMG_COIN_TILE_BASE + 2, DMG_COIN_TILE_BASE + 4, DMG_COIN_TILE_BASE + 6,
    DMG_COIN_TILE_BASE + 8, DMG_COIN_TILE_BASE + 10, DMG_COIN_TILE_BASE + 12, DMG_COIN_TILE_BASE + 14,
    DMG_COIN_TILE_B, DMG_COIN_TILE_B + 2, DMG_COIN_TILE_C
};
static uint8_t draw_oam_coin(uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t gotten) {
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t f = coin_frame_ctr / COIN_FRAME_TICKS;
    uint8_t l = coin_frame_l[f], r = coin_frame_r[f];
    uint8_t p, tl, tr;
    if (_cpu == CGB_TYPE) {
        p = (gotten ? S_PAL(7) : S_PAL(3)) | S_BANK;
        tl = COIN_TILE_BASE + (uint8_t)(l << 1);
        tr = COIN_TILE_BASE + (uint8_t)(r << 1);
    } else {
        if (gotten && (coin_frame_ctr & 1)) return 0;
        p = 0;
        tl = dmg_coin_pair[l];
        tr = (r == 0xFF) ? 0 : dmg_coin_pair[r];
    }
    *oam++ = sy; *oam++ = sx + coin_frame_x[f]; *oam++ = tl; *oam++ = p;
    if (r == 0xFF) return 1;
    *oam++ = sy; *oam++ = sx + 8; *oam++ = tr; *oam = p;
    return 2;
}

// Collected coins fly up and vanish after 40 frames (Famidash animate_coin_*)
static uint8_t draw_coin_anims(uint16_t cam_px, uint16_t cam_py, uint8_t reversed, uint8_t oam_start) {
    for (uint8_t n = 0; n < 3; n++) {
        if (!coin_anim_timer[n]) continue;
        coin_anim_y[n] -= coin_anim_speed[n] & 0xFF00;
        coin_anim_speed[n] -= 0x0040;
        if (++coin_anim_timer[n] >= 40) { coin_anim_timer[n] = 0; continue; }
        if (oam_start > MAX_HARDWARE_SPRITES - 2) continue;
        uint8_t dist_x = (uint8_t)coin_anim_x[n] - (uint8_t)cam_px;
        uint8_t sx = reversed ? (uint8_t)(MIRROR_PLAYER_SCREEN_X - dist_x + 8) : (uint8_t)(dist_x + PLAYER_SCREEN_X + 8);
        uint8_t sy = (uint8_t)((uint8_t)(coin_anim_y[n] >> 8) - (uint8_t)cam_py) + 16;
        if (sy > 160 && sy < 208) continue;
        oam_start += draw_oam_coin(oam_start, sx, sy, 0);
    }
    return oam_start;
}

// DMG objects (tools/make_dmg_object_icons.py). Pads are ONE 8x16 sprite each, an icon of
// their type (the CGB art is 2 sprites and only differs by colour). Orbs are GD-style 16x16
// rings with a disc (2 sprites, one tile pair mirrored). Portals keep their
// art plus a badge with the mode symbol on their side (they also only differ by colour).
#define DMG_ICON_ORB_YELLOW 0
#define DMG_ICON_ORB_BLUE   1
#define DMG_ICON_ORB_PINK   2
#define DMG_ICON_PAD_YELLOW 3
#define DMG_ICON_PAD_BLUE   4
#define DMG_ICON_PAD_PINK   5
#define DMG_ICON_TILE       180   // orb / pad icons: over the CGB orb and pad art (tiles 180..191)
#define DMG_BADGE_CUBE      0
#define DMG_BADGE_SHIP      1
#define DMG_BADGE_BALL      2
#define DMG_BADGE_GRAVITY   3     // arrow down, S_FLIPY = up
#define DMG_BADGE_MINI      4
#define DMG_BADGE_GROW      5
#define DMG_BADGE_UFO       6
// Badge tile pairs: sprite tiles 202..207 and 240..245 are unused on DMG, 238..239 too (the CGB
// mirror exit art; 224..237 are background tiles in some levels, tools/build_bg_tiles.py)
static const uint8_t dmg_badge_tile[7] = { 202, 204, 206, 240, 242, 244, 238 };

// Per object id below 38. Orbs / pads: icon, its sprite y offset from the object's sprite
// position (orbs: 16x16 at it, drawn by dmg_object_loop as 2 sprites; pads at the bottom of
// their cell, ceiling pads at the top - a flipped 8x16 sprite shows its top tile at the
// bottom) and attributes (pads only). OBP1 is the player's: everything here uses OBP0, the
// blue pad its own hourglass, the blue orb its own arrow. Portals: badge and its attributes.
#define NI 0xFF
const uint8_t dmg_icon[38] = {
    NI, NI, NI, NI, DMG_ICON_PAD_PINK,   DMG_ICON_ORB_BLUE, DMG_ICON_ORB_PINK, NI,   NI, NI,
    DMG_ICON_PAD_YELLOW, DMG_ICON_ORB_YELLOW, DMG_ICON_PAD_YELLOW, DMG_ICON_PAD_BLUE, DMG_ICON_PAD_BLUE, NI,
    NI, NI, NI, NI,   DMG_ICON_PAD_YELLOW, DMG_ICON_PAD_YELLOW, NI, NI, NI, NI, NI, NI,   NI, NI, NI, NI, NI, NI, NI, NI, NI,
    DMG_ICON_PAD_PINK
};
const int8_t dmg_icon_y[38] = {
    0, 0, 0, 0, -8,   0, 0, 0,   0, 0,   8, 0, -8, 8, -8, 0,   0, 0, 0, 0,
    8, -8, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 8
};
const uint8_t dmg_icon_prop[38] = {
    0, 0, 0, 0, S_FLIPY,   0, 0, 0,   0, 0,   0, 0, S_FLIPY, 0, S_FLIPY, 0,
    0, 0, 0, 0,   0, S_FLIPY, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t dmg_badge[38] = {
    DMG_BADGE_CUBE, DMG_BADGE_SHIP, DMG_BADGE_BALL, DMG_BADGE_UFO, NI,   NI, NI, NI,
    DMG_BADGE_GRAVITY, DMG_BADGE_GRAVITY,   NI, NI, NI, NI, NI, NI,
    DMG_BADGE_GRAVITY, DMG_BADGE_GRAVITY, DMG_BADGE_GRAVITY, DMG_BADGE_GRAVITY,
    NI, NI, NI, NI, DMG_BADGE_MINI, DMG_BADGE_GROW, NI, NI,   NI, NI, NI, NI, NI, NI, NI, NI, NI, NI
};
// Gravity portals 8 / 16 / 17 pull down (arrow down), 9 / 18 / 19 up
static const uint8_t dmg_badge_prop[38] = {
    0, 0, 0, 0, 0, 0, 0, 0,   0, S_FLIPY,   0, 0, 0, 0, 0, 0,   0, 0, S_FLIPY, S_FLIPY,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
// Badge x: just right of the portal's ring at mid height, like the GD badges (mirror mode: the
// other side, mirrored). It never overlaps the portal's own sprite columns: on DMG overlapping
// sprites are ordered by x first (smaller x in front), so a badge on top of the ring merged with
// it into one black blob (gravity portals). Portal sprite columns: cube/ship/ball 0/8/16,
// gravity 0/8, mini -8/0/8, growth 0/8/16. Horizontal gravity portals (48x16 ring, columns
// 0..40, mirror mode -40..0): the badge goes just past the ring's end.
static const int8_t dmg_badge_x[38] = {
    16, 16, 16, 16, 0, 0, 0, 0,   16, 16,   0, 0, 0, 0, 0, 0,   48, 48, 48, 48,
    0, 0, 0, 0, 16, 24, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const int8_t dmg_badge_x_rev[38] = {
    0, 0, 0, 0, 0, 0, 0, 0,   -8, -8,   0, 0, 0, 0, 0, 0,   -48, -48, -48, -48,
    0, 0, 0, 0, -16, -8, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#undef NI

// DMG object loop. The hot part (every cached object, every frame) is hand-written: SDCC kept
// all its locals on the stack and spent ~180 cycles per object. It writes the orb / pad icons
// to OAM itself and lists the rest (portals, coins, mirror portals: a few at a time) for C.
// In: dmgd_*; out: dmgd_oam, dmgd_slow_n entries of dmgd_slow (object, screen x, screen y,
// cache index).
static uint16_t dmgd_lim;      // cam_px + 176: the cache is sorted by x, stop past it
static uint16_t dmgd_ybias;    // 48 - cam_py
static uint8_t dmgd_camlo;     // (uint8_t)cam_px
static uint8_t dmgd_rev;       // mirror mode
static uint8_t dmgd_rej;       // dist_x in 137..dmgd_rej-1 is off screen (224 normal, 208 mirror)
static uint8_t dmgd_oam;       // next free OAM entry
static uint8_t dmgd_obj, dmgd_sx;
static uint8_t dmgd_slow_n;
static uint8_t dmgd_slow[MAX_ACTIVE_SP_OBJECTS * 4];

// DMG portals (and mirror portals): every frame they were drawn by the C metasprite helpers
// (9-10 sprites, up to 20% of a frame for two portals). Their sprites only depend on the portal
// kind and mirror mode, so they are recorded once (dmg_portal_art_init: the helpers drawn at
// 64, 64, plus the badge) and copied by dmg_copy_art: n, then n x (y, x, tile, attributes)
// relative to the object's sprite position.
#define DMG_PT_KINDS 13
#define DMG_PT_MAX   10
static uint8_t dmg_portal_art[2][DMG_PT_KINDS][1 + DMG_PT_MAX * 4];
static const uint8_t *dmgp_src;
static uint8_t dmgp_sx, dmgp_sy;
static const uint8_t dmg_pt_obj[DMG_PT_KINDS] = {
    OBJ_CUBE_PORTAL, OBJ_SHIP_PORTAL, OBJ_BALL_PORTAL, OBJ_GRAVITY_DOWN, OBJ_GRAVITY_UP,
    16, 17, 18, 19, OBJ_MINI_PORTAL, OBJ_GROW_PORTAL, OBJ_MIRROR_PORTAL, OBJ_MIRROR_EXIT
};

// Portal kind (index in dmg_pt_obj) of the object ids below 38 (0xFF: none; the mirror portals are
// drawn directly), and each kind's offset in dmg_portal_art[mirror mode]
#define PK 0xFF
static const uint8_t dmg_portal_kind_of[38] = {
    0, 1, 2, PK, PK, PK, PK, PK,   3, 4,   PK, PK, PK, PK, PK, PK,   5, 6, 7, 8,
    PK, PK, PK, PK, 9, 10, PK, PK,   PK, PK, PK, PK, PK, PK, PK, PK, PK, PK
};
#undef PK
#define PA (1 + DMG_PT_MAX * 4)
static const uint16_t dmg_portal_art_ofs[DMG_PT_KINDS] = {
    0, PA, 2 * PA, 3 * PA, 4 * PA, 5 * PA, 6 * PA, 7 * PA, 8 * PA, 9 * PA, 10 * PA, 11 * PA, 12 * PA
};
#undef PA

// A portal drawn the slow way: badge (in front: lower OAM index), then the art.
static uint8_t dmg_draw_portal_slow(uint8_t obj, uint8_t oam_idx, uint8_t sx, uint8_t sy, uint8_t reversed) {
    if (obj == OBJ_MIRROR_PORTAL || obj == OBJ_MIRROR_EXIT)
        return draw_oam_mirror_portal(obj, FAMIDASH_SPRITE_TILE_BASE, oam_idx, sx, sy, reversed);
    uint8_t *oam = (uint8_t *)&shadow_OAM[oam_idx];
    uint8_t horiz = (obj >= 16 && obj <= 19);
    *oam++ = (uint8_t)(sy + (horiz ? 0 : 16));
    *oam++ = (uint8_t)(sx + (reversed ? dmg_badge_x_rev[obj] : dmg_badge_x[obj]));
    *oam++ = dmg_badge_tile[dmg_badge[obj]];
    *oam = dmg_badge_prop[obj] | (reversed ? S_FLIPX : 0);
    oam_idx++;
    if (obj == OBJ_MINI_PORTAL || obj == OBJ_GROW_PORTAL)
        return 1 + draw_oam_mini_portal(obj, oam_idx, sx, sy, reversed, COLS_ALL);
    if (horiz)
        return 1 + draw_oam_horizontal_portal(obj, FAMIDASH_SPRITE_TILE_BASE, oam_idx, sx, sy, reversed);
    if (obj >= OBJ_GRAVITY_DOWN)
        return 1 + draw_oam_2x3(famidash_sprite_table[obj], FAMIDASH_SPRITE_TILE_BASE, oam_idx, sx, sy, reversed, COLS_ALL);
    return 1 + draw_oam_3x3(famidash_sprite_table[obj], FAMIDASH_SPRITE_TILE_BASE, oam_idx, sx, sy, reversed, COLS_ALL);
}

// Uses shadow_OAM as scratch: call while the level is loading (it is redrawn every frame).
// Drawn at y 200 (below the screen: a VBlank meanwhile must not show it, which it did at the
// level start), offsets taken from (64, 200).
#define DMG_PT_REC_Y 200
static void dmg_portal_art_init(void) {
    for (uint8_t r = 0; r < 2; r++) {
        for (uint8_t k = 0; k < DMG_PT_KINDS; k++) {
            if (dmg_pt_obj[k] == OBJ_MIRROR_PORTAL || dmg_pt_obj[k] == OBJ_MIRROR_EXIT) continue;   // drawn directly
            uint8_t n = dmg_draw_portal_slow(dmg_pt_obj[k], 0, 64, DMG_PT_REC_Y, r);
            uint8_t *t = dmg_portal_art[r][k];
            const uint8_t *o = (const uint8_t *)&shadow_OAM[0];
            *t++ = n;
            for (uint8_t i = 0; i < n; i++) {
                *t++ = (uint8_t)(*o++ - DMG_PT_REC_Y);
                *t++ = (uint8_t)(*o++ - 64);
                *t++ = *o++;
                *t++ = *o++;
            }
        }
    }
    dmg_portal_art_ready = 1;
}

// Copies portal art dmgp_src to OAM entry dmgd_oam at (dmgp_sx, dmgp_sy); advances dmgd_oam.
static void dmg_copy_art(void) __naked {
    __asm
        ld      hl, #_dmgp_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      a, (hl+)
        ld      c, a                    ; c = sprite count (> 0)
        ld      a, (_dmgd_oam)
        ld      b, a
        add     a, c
        ld      (_dmgd_oam), a
        ld      a, b
        add     a, a
        add     a, a
        ld      e, a
        ld      d, #>_shadow_OAM        ; de = &shadow_OAM[dmgd_oam] (256 byte aligned)
        ld      a, (_dmgp_sy)
        ld      b, a
00001$:
        ld      a, (hl+)
        add     a, b
        ld      (de), a
        inc     e
        ld      a, (_dmgp_sx)
        add     a, (hl)
        inc     hl
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        ld      a, (hl+)
        ld      (de), a
        inc     e
        dec     c
        jr      NZ, 00001$
        ret
    __endasm;
}

static void dmg_object_loop(void) __naked {
    __asm
        xor     a
        ld      (_dmgd_slow_n), a
        ld      b, a                    ; b = cache index
00001$:
        ld      a, b
        cp      a, #MAX_ACTIVE_SP_OBJECTS
        ret     NC
        ld      a, (_dmgd_oam)
        cp      a, #(MAX_HARDWARE_SPRITES - 2)
        ret     NC
        ; active[b]
        ld      hl, #(_active_sp + 80)
        ld      a, l
        add     a, b
        ld      l, a
        ld      a, h
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        or      a, a
        ret     Z
        ; de = px[b]; past the screen: done (sorted by x)
        ld      a, b
        add     a, a
        add     a, #<(_active_sp + 16)
        ld      l, a
        ld      a, #>(_active_sp + 16)
        adc     a, #0
        ld      h, a
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)
        ld      a, (_dmgd_lim)
        sub     a, e
        ld      a, (_dmgd_lim + 1)
        sbc     a, d
        ret     C
        ; c = dist_x = (uint8_t)px - camlo
        ld      a, (_dmgd_camlo)
        ld      c, a
        ld      a, e
        sub     a, c
        ld      c, a
        ; obj = obj[b]
        ld      hl, #_active_sp
        ld      a, l
        add     a, b
        ld      l, a
        ld      a, h
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        ld      (_dmgd_obj), a
        cp      a, #38
        jp      C, 00002$
        cp      a, #OBJ_MIRROR_PORTAL
        jp      Z, 00002$
        cp      a, #OBJ_MIRROR_EXIT
        jp      NZ, 00009$
00002$:
        ; off screen when 137 <= dist_x < rej
        ld      a, (_dmgd_rej)
        sub     a, #137
        ld      d, a
        ld      a, c
        sub     a, #137
        cp      a, d
        jp      C, 00009$
        ; screen x: normal dist_x + PLAYER_SCREEN_X + 8, mirror MIRROR_PLAYER_SCREEN_X + 8 - dist_x
        ld      a, (_dmgd_rev)
        or      a, a
        jp      NZ, 00003$
        ld      a, c
        add     a, #(PLAYER_SCREEN_X + 8)
        jp      00004$
00003$:
        ld      a, #(MIRROR_PLAYER_SCREEN_X + 8)
        sub     a, c
00004$:
        ld      (_dmgd_sx), a
        ; d = py[b] + ybias; on screen (48px margin) when <= 192
        ld      a, b
        add     a, a
        add     a, #<(_active_sp + 48)
        ld      l, a
        ld      a, #>(_active_sp + 48)
        adc     a, #0
        ld      h, a
        ld      a, (_dmgd_ybias)
        add     a, (hl)
        ld      e, a
        inc     hl
        ld      a, (_dmgd_ybias + 1)
        adc     a, (hl)
        jp      NZ, 00009$
        ld      a, #192
        cp      a, e
        jp      C, 00009$
        ld      a, e
        sub     a, #32
        ld      e, a                    ; e = screen y
        ; orb / pad: one icon sprite
        ld      a, (_dmgd_obj)
        cp      a, #38
        jp      NC, 00005$
        ld      c, a                    ; c = obj (dist_x no longer needed)
        ld      hl, #_dmg_icon
        add     a, l
        ld      l, a
        ld      a, h
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        cp      a, #0xFF
        jp      Z, 00005$
        add     a, a
        add     a, #DMG_ICON_TILE
        ld      d, a                    ; d = tile
        ; hl = &shadow_OAM[dmgd_oam] (shadow_OAM is 256 byte aligned)
        ld      a, (_dmgd_oam)
        inc     a
        ld      (_dmgd_oam), a
        dec     a
        add     a, a
        add     a, a
        push    bc
        ld      c, a
        ; y = screen y + dmg_icon_y[obj]
        ld      hl, #_dmg_icon_y
        ld      a, (_dmgd_obj)
        add     a, l
        ld      l, a
        ld      a, h
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        add     a, e
        ld      h, #>_shadow_OAM
        ld      l, c
        ld      (hl+), a
        ld      c, a                    ; c = y
        ld      a, d
        cp      a, #(DMG_ICON_TILE + 6)
        jr      NC, 00010$
        ; orb: 16x16, the right half is the left tile pair mirrored (2 OAM entries are free)
        ld      a, (_dmgd_sx)
        ld      (hl+), a
        ld      a, d
        ld      (hl+), a
        xor     a, a
        ld      (hl+), a
        ld      a, c
        ld      (hl+), a
        ld      a, (_dmgd_sx)
        add     a, #8
        ld      (hl+), a
        ld      a, d
        ld      (hl+), a
        ld      (hl), #0x20             ; S_FLIPX
        ld      hl, #_dmgd_oam
        inc     (hl)
        pop     bc
        jp      00009$
00010$:
        ; pad: one sprite, centred
        ld      a, (_dmgd_sx)
        add     a, #4
        ld      (hl+), a
        ld      a, d
        ld      (hl+), a
        push    hl
        ld      hl, #_dmg_icon_prop
        ld      a, (_dmgd_obj)
        add     a, l
        ld      l, a
        ld      a, h
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        pop     hl
        ld      (hl), a
        pop     bc
        jp      00009$
00005$:
        ; anything else: to the list for C (object, screen x, screen y, cache index)
        ld      a, (_dmgd_slow_n)
        cp      a, #MAX_ACTIVE_SP_OBJECTS
        jp      NC, 00009$
        inc     a
        ld      (_dmgd_slow_n), a
        dec     a
        add     a, a
        add     a, a
        add     a, #<_dmgd_slow
        ld      l, a
        ld      a, #>_dmgd_slow
        adc     a, #0
        ld      h, a
        ld      a, (_dmgd_obj)
        ld      (hl+), a
        ld      a, (_dmgd_sx)
        ld      (hl+), a
        ld      a, e
        ld      (hl+), a
        ld      (hl), b
00009$:
        inc     b
        jp      00001$
    __endasm;
}

// The portals with recorded art (dmg_portal_art) in dmgd_slow, from entry dmgd_pos on, copied to
// OAM in order: stops at the first other entry (dmgd_pos: it, for C), at the end of the list
// (dmgd_pos = dmgd_slow_n), or at a portal that no longer fits (dmgd_full = 1: nothing more is
// drawn). In C, the loop overhead was most of a portal's cost (DMG frame drops in Best
// Automatic Lvl's portal runs).
static const uint8_t *dmgp_base;   // dmg_portal_art[mirror mode]
static uint8_t dmgd_pos, dmgd_full;
static void dmg_slow_portals(void) __naked {
    __asm
        ld      a, (_dmgd_pos)
        ld      b, a
    00001$:
        ld      a, (_dmgd_slow_n)
        cp      a, b
        jr      Z, 00008$               ; end of the list
        ; hl = &dmgd_slow[4 * b]
        ld      a, b
        add     a, a
        add     a, a
        add     a, #<_dmgd_slow
        ld      l, a
        ld      a, #>_dmgd_slow
        adc     a, #0
        ld      h, a
        ld      a, (hl+)                ; object
        cp      a, #38
        jr      NC, 00008$              ; not a portal with recorded art: C
        push    hl
        add     a, #<_dmg_portal_kind_of
        ld      l, a
        ld      a, #>_dmg_portal_kind_of
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        pop     hl
        cp      a, #0xFF
        jr      Z, 00008$
        ld      c, a                    ; c = kind
        ld      a, (hl+)
        ld      (_dmgp_sx), a
        ld      a, (hl)
        ld      (_dmgp_sy), a
        ; hl = dmgp_base + dmg_portal_art_ofs[kind]
        ld      a, c
        add     a, a
        add     a, #<_dmg_portal_art_ofs
        ld      l, a
        ld      a, #>_dmg_portal_art_ofs
        adc     a, #0
        ld      h, a
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      a, (_dmgp_base)
        add     a, l
        ld      l, a
        ld      a, (_dmgp_base + 1)
        adc     a, h
        ld      h, a
        ; no longer fits (dmgd_oam + art[0] > MAX_HARDWARE_SPRITES): done
        ld      a, (_dmgd_oam)
        add     a, (hl)
        cp      a, #(MAX_HARDWARE_SPRITES + 1)
        jr      NC, 00009$
        ld      a, l
        ld      (_dmgp_src), a
        ld      a, h
        ld      (_dmgp_src + 1), a
        push    bc
        call    _dmg_copy_art
        pop     bc
        inc     b
        jp      00001$
    00008$:
        ld      a, b
        ld      (_dmgd_pos), a
        ret
    00009$:
        ld      a, #1
        ld      (_dmgd_full), a
        ret
    __endasm;
}

static uint8_t draw_sprites_dmg(uint16_t cam_px, uint16_t cam_py, uint8_t reversed, uint8_t oam_start) {
    dmgd_lim = cam_px + 176u;
    dmgd_ybias = 48u - cam_py;
    dmgd_camlo = (uint8_t)cam_px;
    dmgd_rev = reversed;
    dmgd_rej = reversed ? 208 : 224;
    dmgd_oam = oam_start;
    dmg_object_loop();

    // The listed objects in order: portals by dmg_slow_portals, the rest here
    dmgp_base = &dmg_portal_art[reversed ? 1 : 0][0][0];
    dmgd_pos = 0;
    dmgd_full = 0;
    for (;;) {
        dmg_slow_portals();
        if (dmgd_full || dmgd_pos >= dmgd_slow_n) break;
        const uint8_t *e = &dmgd_slow[(uint8_t)(dmgd_pos << 2)];
        uint8_t obj = e[0], screen_x = e[1], screen_y = e[2];
        oam_start = dmgd_oam;
        if (obj == OBJ_COIN1 || obj == OBJ_COIN2 || obj == OBJ_COIN3) {
            if (!cache->activated[e[3]]) {
                if (oam_start > MAX_HARDWARE_SPRITES - 2) break;
                oam_start += draw_oam_coin(oam_start, screen_x, screen_y, coins_saved & coin_bit(obj));
            }
        } else if (obj == OBJ_MIRROR_PORTAL || obj == OBJ_MIRROR_EXIT) {   // 12 sprites: not in the art cache
            if (oam_start > MAX_HARDWARE_SPRITES - 12) break;
            oam_start += draw_oam_mirror_portal(obj, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
        } else if (obj == OBJ_UFO_PORTAL) {   // rare (Theory of Everything): drawn directly, not cached (WRAM is full)
            if (oam_start > MAX_HARDWARE_SPRITES - 10) break;
            oam_start += dmg_draw_portal_slow(obj, oam_start, screen_x, screen_y, reversed);
        }
        dmgd_oam = oam_start;
        dmgd_pos++;
    }
    return dmgd_oam;
}

// CGB: OAM slots reserved before the player (gameplay.c) for the front column of the portal the
// player is in: drawn there, that column is over the player and the rest of the portal (drawn
// after the player) under it, so the player goes through the ring. 0: none this frame.
uint8_t sp_front_slot;

// The column with the ring's far arc: art column 1, the mini / growth portal's column at +8
static uint8_t portal_front_cols(uint8_t obj) {
    return (obj == OBJ_MINI_PORTAL || obj == OBJ_GROW_PORTAL) ? 4 : 2;
}

// CGB objects: the loop, in asm like the DMG one (in C the decorations and pads were 2 to 4
// scanlines each, and the dense parts of Clutterfunk dropped frames). Decorations (from
// famidash_deco_table) and the animated pads / orbs are drawn here; portals, coins and the rest go
// to dmgd_slow for draw_sprites. Uses the dmgd_* inputs (draw_sprites_dmg's) and:
static uint8_t cgb_deco_n;               // decorations drawn this frame (at most CGB_DECO_MAX)
#define CGB_DECO_MAX 12
static uint8_t cgbf_sy;                  // screen y of the object
static uint8_t cgbf_w;                   // decoration width - 8 (its mirrored x)
// Pads / orbs: animation kind (1 pad, 2 orb, 3 pink orb, 4 pink pad; 0: not animated) and, per
// kind, this frame's left tile, its bank bit, right tile, its bank bit (draw_sprites)
static const uint8_t cgb_anim_kind[38] = {
    0, 0, 0, 0, 4, 2, 3, 0, 0, 0, 1, 2, 1, 1, 1, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4
};
static uint8_t cgb_anim_fr[4 * 4];
static uint8_t cgb_anim_frames[4][4 * 4];
static uint8_t cgb_anim_ready;

static void cgb_object_loop(void) __naked {
    __asm
        xor     a
        ld      (_dmgd_slow_n), a
        ld      b, a                    ; b = cache index
00001$:
        ld      a, b
        cp      a, #MAX_ACTIVE_SP_OBJECTS
        ret     NC
        ld      a, (_dmgd_oam)
        cp      a, #(MAX_HARDWARE_SPRITES - 2)
        ret     NC
        ; active[b]
        ld      hl, #(_active_sp + 80)
        ld      a, l
        add     a, b
        ld      l, a
        ld      a, h
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        or      a, a
        ret     Z
        ; de = px[b]; past the screen: done (sorted by x)
        ld      a, b
        add     a, a
        add     a, #<(_active_sp + 16)
        ld      l, a
        ld      a, #>(_active_sp + 16)
        adc     a, #0
        ld      h, a
        ld      a, (hl+)
        ld      e, a
        ld      d, (hl)
        ld      a, (_dmgd_lim)
        sub     a, e
        ld      a, (_dmgd_lim + 1)
        sbc     a, d
        ret     C
        ; c = dist_x = (uint8_t)px - camlo
        ld      a, (_dmgd_camlo)
        ld      c, a
        ld      a, e
        sub     a, c
        ld      c, a
        ; obj = obj[b]: colour triggers (128..) and the level end are not drawn
        ld      hl, #_active_sp
        ld      a, l
        add     a, b
        ld      l, a
        ld      a, h
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        ld      (_dmgd_obj), a
        cp      a, #128
        jp      NC, 00009$
        cp      a, #OBJ_LEVEL_END
        jp      Z, 00009$
        ; off screen when 137 <= dist_x < rej
        ld      a, (_dmgd_rej)
        sub     a, #137
        ld      d, a
        ld      a, c
        sub     a, #137
        cp      a, d
        jp      C, 00009$
        ; screen x: normal dist_x + PLAYER_SCREEN_X + 8, mirror MIRROR_PLAYER_SCREEN_X + 8 - dist_x
        ld      a, (_dmgd_rev)
        or      a, a
        jp      NZ, 00003$
        ld      a, c
        add     a, #(PLAYER_SCREEN_X + 8)
        jp      00004$
00003$:
        ld      a, #(MIRROR_PLAYER_SCREEN_X + 8)
        sub     a, c
00004$:
        ld      (_dmgd_sx), a
        ; d = py[b] + ybias; on screen (48px margin) when <= 192
        ld      a, b
        add     a, a
        add     a, #<(_active_sp + 48)
        ld      l, a
        ld      a, #>(_active_sp + 48)
        adc     a, #0
        ld      h, a
        ld      a, (_dmgd_ybias)
        add     a, (hl)
        ld      e, a
        inc     hl
        ld      a, (_dmgd_ybias + 1)
        adc     a, (hl)
        jp      NZ, 00009$
        ld      a, #192
        cp      a, e
        jp      C, 00009$
        ld      a, e
        sub     a, #32
        ld      (_cgbf_sy), a
        ld      e, a                    ; e = screen y (for the slow list)
        ; which kind of object
        ld      a, (_dmgd_obj)
        cp      a, #38
        jp      C, 00020$               ; below 38: animated pad / orb, or slow
        cp      a, #FAMIDASH_DECO_TABLE_SIZE
        jp      NC, 00005$              ; slow (mirror portals...)
        ; decoration: hl = famidash_deco_table[obj] (0: not drawn)
        add     a, a
        add     a, #<_famidash_deco_table
        ld      l, a
        ld      a, #>_famidash_deco_table
        adc     a, #0
        ld      h, a
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        or      a, h
        jp      Z, 00009$
        ld      a, (_cgb_deco_n)
        cp      a, #CGB_DECO_MAX
        jp      NC, 00009$
        inc     a
        ld      (_cgb_deco_n), a
        push    bc
        ld      a, (hl+)                ; count
        ld      b, a
        ld      a, (_dmgd_oam)
        add     a, b
        cp      a, #(MAX_HARDWARE_SPRITES + 1)
        jr      C, 00030$
        pop     bc
        ret                             ; OAM full
00030$:
        ld      c, a                    ; the next free entry after it
        ld      a, (_dmgd_oam)
        add     a, a
        add     a, a
        ld      e, a
        ld      d, #>_shadow_OAM        ; de = &shadow_OAM[dmgd_oam] (256 byte aligned)
        ld      a, c
        ld      (_dmgd_oam), a
        ld      a, (hl+)                ; width
        sub     a, #8
        ld      (_cgbf_w), a
        ; hl = x[0]; x[k] + 3 = y[k], + 6 = tile[k], + 9 = props[k]
00031$:
        ld      c, (hl)                 ; c = x[k]
        inc     hl
        inc     hl
        inc     hl
        ld      a, (_cgbf_sy)
        add     a, (hl)                 ; y
        ld      (de), a
        inc     e
        ld      a, (_dmgd_rev)
        or      a, a
        jr      NZ, 00032$
        ld      a, (_dmgd_sx)
        add     a, c
        jr      00033$
00032$:
        ld      a, (_cgbf_w)
        sub     a, c
        ld      c, a
        ld      a, (_dmgd_sx)
        add     a, c
00033$:
        ld      (de), a                 ; x
        inc     e
        inc     hl
        inc     hl
        inc     hl
        ld      a, (hl)                 ; tile: a rod ball top / the pulsing ball is shown in the pulse size
        ld      c, a
        cp      a, #D_PB
        jr      NZ, 00036$
        ld      a, (_pb_tile)
        jr      00037$
00036$:
        sub     a, #D_C9
        cp      a, #(D_CD - D_C9 + 1)
        ld      a, (_rod_tile)
        jr      C, 00034$
        ld      a, #FAMIDASH_SPRITE_TILE_BASE
00034$:
        add     a, c
00037$:
        ld      (de), a
        inc     e
        inc     hl
        inc     hl
        inc     hl
        ld      a, (_dmgd_rev)
        or      a, a
        ld      a, (hl)                 ; props
        jr      Z, 00035$
        xor     a, #0x20                ; S_FLIPX
00035$:
        ld      (de), a
        inc     e
        ; back to x[k + 1]
        ld      a, l
        sub     a, #8
        ld      l, a
        ld      a, h
        sbc     a, #0
        ld      h, a
        dec     b
        jr      NZ, 00031$
        pop     bc
        jp      00009$
00020$:
        ; pad / orb: kind
        ld      hl, #_cgb_anim_kind
        add     a, l
        ld      l, a
        ld      a, h
        adc     a, #0
        ld      h, a
        ld      a, (hl)
        or      a, a
        jp      Z, 00005$
        push    bc
        dec     a
        add     a, a
        add     a, a
        add     a, #<_cgb_anim_fr
        ld      c, a
        ld      a, #>_cgb_anim_fr
        adc     a, #0
        ld      b, a                    ; bc = this frame's tiles of the kind
        ld      a, (_dmgd_oam)
        cp      a, #(MAX_HARDWARE_SPRITES - 1)
        jr      C, 00021$
        pop     bc
        ret                             ; OAM full
00021$:
        add     a, a
        add     a, a
        ld      e, a
        ld      d, #>_shadow_OAM
        ld      a, (_dmgd_oam)
        add     a, #2
        ld      (_dmgd_oam), a
        ; hl = famidash_sprite_table[obj]: props at + 3 (left) and + 7 (right)
        ld      a, (_dmgd_obj)
        add     a, a
        add     a, #<_famidash_sprite_table
        ld      l, a
        ld      a, #>_famidash_sprite_table
        adc     a, #0
        ld      h, a
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        inc     hl
        inc     hl
        inc     hl
        ; left sprite: x sx (mirror: sx + 8)
        ld      a, (_cgbf_sy)
        ld      (de), a
        inc     e
        ld      a, (_dmgd_rev)
        or      a, a
        ld      a, (_dmgd_sx)
        jr      Z, 00022$
        add     a, #8
00022$:
        ld      (de), a
        inc     e
        ld      a, (bc)                 ; tile
        inc     bc
        ld      (de), a
        inc     e
        ld      a, (bc)                 ; bank bit
        inc     bc
        or      a, (hl)                 ; | props
        call    00040$                  ; ^ S_FLIPX in mirror mode
        ld      (de), a
        inc     e
        inc     hl
        inc     hl
        inc     hl
        inc     hl
        ; right sprite: x sx + 8 (mirror: sx)
        ld      a, (_cgbf_sy)
        ld      (de), a
        inc     e
        ld      a, (_dmgd_rev)
        or      a, a
        ld      a, (_dmgd_sx)
        jr      NZ, 00023$
        add     a, #8
00023$:
        ld      (de), a
        inc     e
        ld      a, (bc)
        inc     bc
        ld      (de), a
        inc     e
        ld      a, (bc)
        or      a, (hl)
        call    00040$
        ld      (de), a
        pop     bc
        jp      00009$
00040$:
        ; a ^= S_FLIPX in mirror mode (keeps hl, bc, de)
        push    af
        ld      a, (_dmgd_rev)
        or      a, a
        jr      Z, 00041$
        pop     af
        xor     a, #0x20
        ret
00041$:
        pop     af
        ret
00005$:
        ; anything else: to the list for C (object, screen x, screen y, cache index)
        ld      a, (_dmgd_slow_n)
        cp      a, #MAX_ACTIVE_SP_OBJECTS
        jp      NC, 00009$
        inc     a
        ld      (_dmgd_slow_n), a
        dec     a
        add     a, a
        add     a, a
        add     a, #<_dmgd_slow
        ld      l, a
        ld      a, #>_dmgd_slow
        adc     a, #0
        ld      h, a
        ld      a, (_dmgd_obj)
        ld      (hl+), a
        ld      a, (_dmgd_sx)
        ld      (hl+), a
        ld      a, (_cgbf_sy)
        ld      (hl+), a
        ld      (hl), b
00009$:
        inc     b
        jp      00001$
    __endasm;
}

uint8_t draw_sprites(
        SpCache *cache_arg, uint16_t cam_px, uint16_t cam_py,
        uint8_t reversed, uint8_t oam_start
) BANKED {
    uint8_t i;
    uint8_t screen_x, screen_y;

    if (++coin_frame_ctr >= 6 * COIN_FRAME_TICKS) coin_frame_ctr = 0;
    oam_start = draw_coin_anims(cam_px, cam_py, reversed, oam_start);

    // DMG: one icon sprite per object (skipped when the cache has nothing DMG draws)
    if (_cpu != CGB_TYPE) {
        sp_front_slot = 0;
        if (!sp_has_drawn) return oam_start;
        return draw_sprites_dmg(cam_px, cam_py, reversed, oam_start);
    }

    rod_pulse_update();
    if (!cgb_anim_ready) {
        // the pad / orb tiles of the 4 animation frames per kind (pad, orb, pink orb, pink pad),
        // see draw_oam_anim2: computed once, copied to cgb_anim_fr when the frame changes
        static const uint8_t rep_obj[4] = { OBJ_PAD_YELLOW, OBJ_ORB_YELLOW, OBJ_ORB_PINK, OBJ_PAD_PINK };
        for (uint8_t f = 0; f < 4; f++) {
            uint8_t g = (uint8_t)(f + 1u) & 3u;
            uint8_t *fr = cgb_anim_frames[f];
            for (uint8_t k = 0; k < 4; k++) {
                const metasprite_t *meta = famidash_sprite_table[rep_obj[k]];
                uint8_t ax = obj_anim_tiles(rep_obj[k]);
                uint8_t t[4];
                t[0] = meta[0].dtile + FAMIDASH_SPRITE_TILE_BASE;
                t[1] = meta[1].dtile + FAMIDASH_SPRITE_TILE_BASE;
                t[2] = ax;
                t[3] = ax + 2;
                *fr++ = t[f]; *fr++ = (f >= 2) ? S_BANK : 0;
                *fr++ = t[g]; *fr++ = (g >= 2) ? S_BANK : 0;
            }
        }
        cgb_anim_ready = 1;
        memcpy(cgb_anim_fr, cgb_anim_frames[obj_anim_f], sizeof(cgb_anim_fr));
    }
    if (++obj_anim_tick >= ANIM_FRAME_TICKS) {
        obj_anim_tick = 0;
        obj_anim_f = (uint8_t)(obj_anim_f + 1u) & 3u;
        memcpy(cgb_anim_fr, cgb_anim_frames[obj_anim_f], sizeof(cgb_anim_fr));
    }
    dmgd_lim = cam_px + 176u;
    dmgd_ybias = 48u - cam_py;
    dmgd_camlo = (uint8_t)cam_px;
    dmgd_rev = reversed;
    dmgd_rej = reversed ? 208 : 224;
    dmgd_oam = oam_start;
    cgb_deco_n = 0;
    cgb_object_loop();
    oam_start = dmgd_oam;

    uint8_t front_free = (sp_front_slot != 0);
    uint8_t front_n = 0;
    const uint8_t *e = dmgd_slow;
    for (uint8_t n = dmgd_slow_n; n; n--, e += 4) {
        uint8_t obj = e[0];
        screen_x = e[1];
        screen_y = e[2];
        i = e[3];
        uint16_t obj_x = cache->px[i];

        // the portal the player is in (t_px: process_sprite_logic): front column in the slots
        // reserved before the player
        uint8_t cols = COLS_ALL;
        if (front_free && (obj <= OBJ_UFO_PORTAL || obj == OBJ_GRAVITY_DOWN || obj == OBJ_GRAVITY_UP ||
                           obj == OBJ_MINI_PORTAL || obj == OBJ_GROW_PORTAL)
            && obj_x <= t_px + 16u && obj_x + 32u >= t_px) {
            cols = portal_front_cols(obj);
            front_free = 0;
            uint8_t fs = sp_front_slot;
            if (obj == OBJ_MINI_PORTAL || obj == OBJ_GROW_PORTAL)
                front_n = draw_oam_mini_portal(obj, fs, screen_x, screen_y, reversed, cols);
            else if (obj <= OBJ_UFO_PORTAL)
                front_n = draw_oam_3x3(famidash_sprite_table[obj], FAMIDASH_SPRITE_TILE_BASE, fs, screen_x, screen_y, reversed, cols);
            else
                front_n = draw_oam_2x3(famidash_sprite_table[obj], FAMIDASH_SPRITE_TILE_BASE, fs, screen_x, screen_y, reversed, cols);
            cols = (uint8_t)~cols;
        }

        if (obj == OBJ_MINI_PORTAL || obj == OBJ_GROW_PORTAL) {
            if (oam_start > MAX_HARDWARE_SPRITES - 7) break;
            oam_start += draw_oam_mini_portal(obj, oam_start, screen_x, screen_y, reversed, cols);
            continue;
        }

        if (obj == OBJ_COIN1 || obj == OBJ_COIN2 || obj == OBJ_COIN3) {
            if (cache->activated[i]) continue;
            if (oam_start > MAX_HARDWARE_SPRITES - 2) break;
            oam_start += draw_oam_coin(oam_start, screen_x, screen_y, coins_saved & coin_bit(obj));
            continue;
        }

        if (obj == OBJ_MIRROR_PORTAL || obj == OBJ_MIRROR_EXIT) {
            if (oam_start > MAX_HARDWARE_SPRITES - 12) break;
            oam_start += draw_oam_mirror_portal(obj, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
            continue;
        }

        if (obj >= 38) continue;   // decorations: cgb_object_loop

        if (oam_start > MAX_HARDWARE_SPRITES - 9) break;
        const metasprite_t *sprite = famidash_sprite_table[obj];
        if (sprite == 0) continue;

        if (obj >= 16 && obj <= 19) {
            oam_start += draw_oam_horizontal_portal(obj, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
        } else if (obj <= OBJ_UFO_PORTAL) {
            oam_start += draw_oam_3x3(sprite, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed, cols);
        } else if (obj == OBJ_GRAVITY_DOWN || obj == OBJ_GRAVITY_UP) {
            oam_start += draw_oam_2x3(sprite, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed, cols);
        } else {
            oam_start += draw_oam_2x1(sprite, FAMIDASH_SPRITE_TILE_BASE, oam_start, screen_x, screen_y, reversed);
        }
    }
    // reserved slots not used this frame: hidden
    if (sp_front_slot) {
        for (uint8_t k = front_n; k < PORTAL_FRONT_SLOTS; k++) shadow_OAM[sp_front_slot + k].y = 0;
        sp_front_slot = 0;
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
