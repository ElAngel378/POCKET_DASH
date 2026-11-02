// Pocket Dash - Game Gear proof of concept (Dry Out).
//
// Screen is 160x144, same as the Game Boy. The VDP nametable is 32x28 tiles
// (224px tall) but the level is 32 tile rows + 2 ground rows = 34 rows, so
// world tile row R lives in nametable row R % 28 and rows are streamed in as
// the camera moves vertically, the same way columns are streamed horizontally.

#include <gbdk/platform.h>
#include <stdint.h>
#include <string.h>

#include "gg_data.h"
#include "gg_music.h"
#include "player.h"

#define MAP_W            GG_LEVEL_WIDTH
#define level_map        dryout_map

// Visible screen: 160x144 on Game Gear, 256x192 on Master System.
#define SCREEN_W         DEVICE_SCREEN_PX_WIDTH
#define SCREEN_H         DEVICE_SCREEN_PX_HEIGHT
#ifdef __TARGET_sms
// The SMS screen is as wide as the 32-column nametable, so the VDP's left-column mask hides the
// leftmost 8 px. The remaining 248 px never span more than 16 metatile columns, so the column
// being replaced is always off screen.
#define VIEW_MT_W        16
#define LEFT_MASK_PX     8
#else
#define VIEW_MT_W        10
#define LEFT_MASK_PX     0
#endif
#define RING_MT_W        16      // 16 metatile columns = 32 tile columns = nametable width
#define RING_ROWS        28      // nametable height in tiles
#define WORLD_ROWS       34      // 32 map tile rows + 2 ground tile rows
#define MAP_H            16      // metatile rows
#define MAX_V0           (WORLD_ROWS - RING_ROWS)

#define CAM_PY_MAX       (256 + 16 - SCREEN_H)   // level + ground row - screen height
#define CAM_Y_TOP_ZONE   20
#define CAM_Y_BOTTOM_ZONE (SCREEN_H - 44)

// 8.8 fixed-point scroll speed, same value as the Game Boy build
#define SCROLL_SPEED_FP  714

#define BG_TRIGGER_LEAD_PX 160

// Level object types (same ids as the Game Boy build)
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
#define OBJ_LEVEL_END     15
#define OBJ_PAD_PINK      37

// Sprite slots 0/1 are the player, the rest are level objects
#define PLAYER_SPRITES   2
#define FIRST_OBJ_SPRITE 2

static Player player;
static uint16_t cam_px;
static uint16_t cam_py;
static uint16_t scroll_acc;
static uint16_t loaded_mc;       // last metatile column written to the nametable
static uint8_t  v0;              // first world tile row currently valid in the nametable
static uint16_t max_scroll_px;
static uint16_t cached_col = 0xFFFF;
static uint8_t  collision_columns[32];

static uint8_t  obj_done[GG_LOGIC_COUNT];   // activated flags (interactive objects)
static uint8_t  obj_lo;                   // first interactive object that can still matter
static uint8_t  vis_lo;                   // first drawable object that can still be on screen

static uint16_t bg_pal[16];

// Load a 16-colour palette given as 12-bit Game Gear colours (0x0BGR, 4 bits per channel).
static void load_palette(uint8_t which, const uint16_t *src) {
#ifdef __TARGET_sms
    static palette_color_t conv[16];
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t c = src[i];
        uint8_t r = (uint8_t)(c & 15), g = (uint8_t)((c >> 4) & 15), b = (uint8_t)((c >> 8) & 15);
        conv[i] = RGB((r * 3 + 7) / 15, (g * 3 + 7) / 15, (b * 3 + 7) / 15);
    }
    set_palette(which, 1, conv);
#else
    set_palette(which, 1, src);
#endif
}
static uint8_t  pal_dirty;

// DEBUG_PROFILE: each section of the main loop writes its id to gpmark. The headless
// Mesen script (tools/mesen_profile.lua) watches writes to it and records CPU cycles per section.
#ifdef DEBUG_PROFILE
volatile uint8_t gpmark;
#define PROF_MARK(n) (gpmark = (n))
#else
#define PROF_MARK(n)
#endif

// One metatile column in world-row order, interleaved (left tile, right tile) per tile row.
// A metatile's 4 tile ids {TL, TR, BL, BR} are exactly two such rows, so building this is a
// straight copy. col_v0 remembers the nametable row window the column was prepared for.
static uint8_t colw[WORLD_ROWS * 2];
static uint8_t col_v0;
static uint8_t row_buf[32];
static const uint8_t empty_col[MAP_H];

// ---------------------------------------------------------------- background streaming

static inline uint8_t ring_row(uint8_t r) {
    return (r >= RING_ROWS) ? (uint8_t)(r - RING_ROWS) : r;
}

// Build both tile columns of metatile column mc (no VRAM access) for row window v0.
static void prepare_column(uint16_t mc) {
    const uint8_t *mcol = (mc < MAP_W) ? &level_map[mc << 4] : empty_col;
    const uint8_t *mt = &gg_mt[0][0];
    uint8_t *d = colw;
    for (uint8_t r = MAP_H; r; r--) {
        const uint8_t *t = mt + ((uint16_t)(*mcol++) << 2);
        *d++ = *t++;
        *d++ = *t++;
        *d++ = *t++;
        *d++ = *t;
    }
    uint8_t xe = (uint8_t)(mc << 1) & 7;
    d[0] = gg_ground_top[xe];
    d[1] = gg_ground_top[xe + 1];
    d[2] = gg_ground_bot[xe];
    d[3] = gg_ground_bot[xe + 1];
    col_v0 = v0;
}

// Nametable entries are 2 bytes (tile, attribute), 64 bytes per row of 32 entries, 28 rows.
// On the Game Gear GBDK's tile coordinates are relative to the visible area, which starts
// DEVICE_SCREEN_X_OFFSET / Y_OFFSET tiles into the hardware nametable, wrapping at 32 x 28.
// nt_origin is the VDP write command for hardware nametable entry (0,0); it is derived from
// GBDK's own address for tile (0,0) so the base address is never assumed.
static uint16_t nt_origin;

// VDP write command for GBDK tile coordinates (x = 0, y = ring row)
static inline uint16_t nt_row_addr(uint8_t row) {
    row += DEVICE_SCREEN_Y_OFFSET;
    if (row >= RING_ROWS) row -= RING_ROWS;
    return nt_origin + ((uint16_t)row << 6);
}

#ifdef DEBUG_SLOWVDP
// Reference implementation through GBDK (used by the A/B screenshot test).
static void flush_column(uint16_t mc) {
    static uint8_t a[RING_ROWS], b[RING_ROWS];
    uint8_t x = (uint8_t)(mc << 1) & 31;
    for (uint8_t w = col_v0; w < col_v0 + RING_ROWS; w++) {
        a[ring_row(w)] = colw[w << 1];
        b[ring_row(w)] = colw[(w << 1) + 1];
    }
    set_bkg_tiles(x, 0, 1, RING_ROWS, a);
    set_bkg_tiles(x + 1, 0, 1, RING_ROWS, b);
}
#else
// World row w of the window lives in ring row w % 28, which is hardware nametable row
// (w + DEVICE_SCREEN_Y_OFFSET) % 28. The window's rows are consecutive in colw, so this writes
// them in order and wraps the VRAM address once. Both tile columns are adjacent in VRAM, so one
// address set per row writes both.
static void flush_column(uint16_t mc) {
    uint16_t base = nt_origin + ((uint16_t)(((uint8_t)(mc << 1) + DEVICE_SCREEN_X_OFFSET) & 31) << 1);
    uint8_t hw = col_v0 + DEVICE_SCREEN_Y_OFFSET;
    if (hw >= RING_ROWS) hw -= RING_ROWS;
    uint16_t addr = base + ((uint16_t)hw << 6);
    const uint8_t *p = &colw[col_v0 << 1];
    uint8_t n = RING_ROWS - hw;
    __asm__("di");
    do {
        VDP_CMD = (uint8_t)addr;
        VDP_CMD = (uint8_t)(addr >> 8);
        VDP_DATA = *p++;
        VDP_DATA = 0;
        VDP_DATA = *p++;
        VDP_DATA = 0;
        addr += 64;
    } while (--n);
    if (hw) {
        addr = base;
        n = hw;
        do {
            VDP_CMD = (uint8_t)addr;
            VDP_CMD = (uint8_t)(addr >> 8);
            VDP_DATA = *p++;
            VDP_DATA = 0;
            VDP_DATA = *p++;
            VDP_DATA = 0;
            addr += 64;
        } while (--n);
    }
    __asm__("ei");
}
#endif

// Build a full nametable row for world row r across the 16 loaded metatile columns.
static void prepare_row(uint8_t r) {
    uint16_t mc = loaded_mc - (RING_MT_W - 1);
    if (r >= 32) {
        uint8_t x = (uint8_t)(mc << 1);
        const uint8_t *g = (r == 32) ? gg_ground_top : gg_ground_bot;
        for (uint8_t i = 0; i < 32; i++, x++) row_buf[x & 31] = g[x & 7];
        return;
    }
    uint8_t half = r >> 1;
    uint8_t sub = (r & 1) << 1;
    uint8_t xi = (uint8_t)(mc << 1) & 31;
    const uint8_t *p = &level_map[(mc << 4) + half];
    for (uint8_t k = 0; k < RING_MT_W; k++) {
        const uint8_t *t = gg_mt[(mc < MAP_W) ? *p : 0];
        row_buf[xi] = t[sub];
        row_buf[xi + 1] = t[sub + 1];
        xi = (xi + 2) & 31;
        p += 16;
        mc++;
    }
}

#ifdef DEBUG_SLOWVDP
static void flush_row(uint8_t r) {
    set_bkg_tiles(0, ring_row(r), 32, 1, row_buf);
}
#else
static void flush_row(uint8_t r) {
    uint16_t addr = nt_row_addr(ring_row(r));
    const uint8_t *p = row_buf;
    __asm__("di");
    // entries 0..25 -> hardware columns 6..31, entries 26..31 wrap to columns 0..5
    addr += DEVICE_SCREEN_X_OFFSET << 1;
    VDP_CMD = (uint8_t)addr;
    VDP_CMD = (uint8_t)(addr >> 8);
    for (uint8_t i = 32 - DEVICE_SCREEN_X_OFFSET; i; i--) {
        VDP_DATA = *p++;
        VDP_DATA = 0;
    }
    addr = nt_row_addr(ring_row(r));
    VDP_CMD = (uint8_t)addr;
    VDP_CMD = (uint8_t)(addr >> 8);
    for (uint8_t i = DEVICE_SCREEN_X_OFFSET; i; i--) {
        VDP_DATA = *p++;
        VDP_DATA = 0;
    }
    __asm__("ei");
}
#endif

static void load_collision_columns(uint16_t mc) {
    const uint8_t *left = &level_map[mc << 4];
    const uint8_t *right = (mc + 1u < MAP_W) ? left + 16 : left;
    memcpy(collision_columns, left, 16);
    memcpy(collision_columns + 16, right, 16);
}

// ---------------------------------------------------------------- colour triggers

static void apply_g_trigger(uint8_t id) {
    uint8_t idx = (id == 31) ? 0x2C : (id == 46) ? 0x2A : (id & 0x3F);
    const uint16_t *g = gg_gnd_tab[idx];
    bg_pal[5] = g[1];
    bg_pal[6] = g[0];
    bg_pal[13] = g[0];
    bg_pal[14] = g[2];
    bg_pal[15] = g[3];
    pal_dirty = 1;
}

static void apply_bg_trigger(uint8_t id) {
    if (id == 46) {
        apply_g_trigger(46);
        return;
    }
    uint8_t idx = (id == 31) ? 0x2C : (id & 0x3F);
    uint16_t c = gg_sky_tab[idx][0];
    uint16_t d = gg_sky_tab[idx][1];
    bg_pal[0] = c;
    bg_pal[1] = d;
    bg_pal[4] = c;
    bg_pal[8] = c;
    bg_pal[9] = d;
    pal_dirty = 1;
}

// ---------------------------------------------------------------- level objects

static void process_objects(uint8_t joy) {
    uint16_t px = player.world_x;
    uint16_t py = player.world_y.b.h;
    uint16_t p_front = px + 15u;
    uint16_t p_bottom = py + PLAYER_SIZE;

    while (obj_lo < GG_LOGIC_COUNT && gg_logic[obj_lo].x + 48u < px) obj_lo++;

    for (uint8_t i = obj_lo; i < GG_LOGIC_COUNT; i++) {
        const GgObj *o = &gg_logic[i];
        uint16_t obj_x = o->x;
        if (obj_x > px + BG_TRIGGER_LEAD_PX) break;
        if (obj_done[i]) continue;

        uint8_t obj = o->type;

        if (obj >= 128 && obj <= 175) {
            apply_bg_trigger((uint8_t)(obj - 128));
            obj_done[i] = 1;
            continue;
        }
        if (obj >= 192 && obj <= 239) {
            apply_g_trigger((uint8_t)(obj - 192));
            obj_done[i] = 1;
            continue;
        }
        if (obj == OBJ_LEVEL_END || obj >= 38) continue;
        if (obj_x > px + 48u) continue;

        uint16_t obj_y = o->y;
        int16_t dy = (int16_t)py - (int16_t)obj_y;
        if (dy > 50 || dy < -20) continue;

        if (obj >= 16 && obj <= 19) {
            // 48-pixel wide horizontal gravity portal
            if (obj_x <= p_front && px <= obj_x + 48u && py <= obj_y + 14u && p_bottom >= obj_y) {
                uint8_t target_flipped = (obj >= 18);
                if (player.gravity_flipped != target_flipped) {
                    player.gravity_flipped = target_flipped;
                    player.vel_y.w = (player.vel_y.w >> 1);
                }
                obj_done[i] = 1;
            }
        } else if (obj_x <= p_front && px <= obj_x + 15u) {
            switch (obj) {
                case OBJ_CUBE_PORTAL:
                case OBJ_SHIP_PORTAL:
                case OBJ_BALL_PORTAL:
                    if (py <= obj_y + 49u && p_bottom >= (obj_y - 1u)) {
                        player.mode = (obj == OBJ_CUBE_PORTAL) ? MODE_CUBE
                                    : (obj == OBJ_SHIP_PORTAL) ? MODE_SHIP : MODE_BALL;
                        player.vel_y.w = (player.vel_y.w >> 1);
                        obj_done[i] = 1;
                    }
                    break;

                case OBJ_GRAVITY_DOWN:
                case OBJ_GRAVITY_UP:
                    if (py <= obj_y + 43u && p_bottom >= (obj_y + 5u)) {
                        uint8_t target_flipped = (obj == OBJ_GRAVITY_UP);
                        if (player.gravity_flipped != target_flipped) {
                            player.gravity_flipped = target_flipped;
                            player.vel_y.w = (player.vel_y.w >> 1) + (player.vel_y.w >> 3);
                        }
                        obj_done[i] = 1;
                    }
                    break;

                case OBJ_PAD_YELLOW:
                case OBJ_PAD_PINK:
                case OBJ_PAD_BLUE:
                case OBJ_PAD_YELLOW_UP:
                case OBJ_PAD_BLUE_UP: {
                    uint8_t is_ceiling = (obj == OBJ_PAD_YELLOW_UP || obj == OBJ_PAD_BLUE_UP);
                    uint16_t pad_top = is_ceiling ? obj_y : (obj_y + 13u);
                    uint16_t pad_bot = is_ceiling ? (obj_y + 3u) : (obj_y + 16u);
                    if (py <= pad_bot && p_bottom >= pad_top) {
                        obj_done[i] = 1;
                        if (obj == OBJ_PAD_BLUE) {
                            if (!player.gravity_flipped) {
                                player.gravity_flipped = 1;
                                player.vel_y.w = -BLUE_PAD_FORCE;
                                player.on_ground = 0;
                            }
                        } else if (obj == OBJ_PAD_BLUE_UP) {
                            if (player.gravity_flipped) {
                                player.gravity_flipped = 0;
                                player.vel_y.w = BLUE_PAD_FORCE;
                                player.on_ground = 0;
                            }
                        } else if (obj == OBJ_PAD_PINK) {
                            int16_t force = (player.mode == MODE_BALL) ? BALL_PINK_PAD : PINK_PAD_FORCE;
                            player.vel_y.w = player.gravity_flipped ? -force : force;
                            player.on_ground = 0;
                        } else {
                            int16_t force = (player.mode == MODE_BALL) ? BALL_YELLOW_PAD : PAD_JUMP_FORCE;
                            player.vel_y.w = player.gravity_flipped ? -force : force;
                            player.on_ground = 0;
                        }
                    }
                    break;
                }

                case OBJ_ORB_YELLOW:
                case OBJ_ORB_PINK:
                case OBJ_ORB_BLUE:
                    if ((joy & J_A) && (!(player.last_joy & J_A) || player.orb_buffered)
                        && py <= obj_y + 16u && p_bottom >= obj_y) {
                        obj_done[i] = 1;
                        player.orb_buffered = 0;
                        if (obj == OBJ_ORB_BLUE) {
                            player.gravity_flipped = !player.gravity_flipped;
                            int16_t force = (player.mode == MODE_BALL) ? BLUE_ORB_FORCE : BLUE_PAD_FORCE;
                            player.vel_y.w = player.gravity_flipped ? -force : force;
                        } else if (obj == OBJ_ORB_PINK) {
                            int16_t force = (player.mode == MODE_BALL) ? BALL_PINK_ORB : MAGENTA_JUMP_FORCE;
                            player.vel_y.w = player.gravity_flipped ? -force : force;
                        } else {
                            int16_t force = (player.mode == MODE_BALL) ? BALL_YELLOW_ORB : JUMP_FORCE;
                            player.vel_y.w = player.gravity_flipped ? -force : force;
                        }
                        player.on_ground = 0;
                    }
                    break;
            }
        }
    }
}

// ---------------------------------------------------------------- sprites

// Sprite attribute table layout (shadow_OAM): Y bytes at [n], then X/tile pairs at [0x40 + 2n].
// A Y value of 0xD0 ends the sprite list, so unused slots never need hiding one by one.
#define OAM_Y(n)      (((uint8_t *)shadow_OAM) + (n))
#define OAM_XT(n)     (((uint8_t *)shadow_OAM) + 0x40 + ((n) << 1))
#define SAT_TERM      0xD0

static inline void end_sprites(uint8_t used) {
    if (used < MAX_HARDWARE_SPRITES) *OAM_Y(used) = SAT_TERM;
}

// Metasprite item offsets are relative to the previous item: cube/ship halves are at dx -1 and +7.
// sx is 0..PLAYER_SCREEN_X and sy is clamped to 0..144, so 8-bit maths is enough.
static void draw_player(uint8_t sx, uint8_t sy) {
    uint8_t *y = OAM_Y(0);
    uint8_t *xt = OAM_XT(0);
    uint8_t t0, t1;
    if (player.mode == MODE_SHIP) {
        uint8_t g = player.gravity_flipped ? 1 : 0;
        sy += g ? 1 : (uint8_t)-1;
        t0 = gg_ship_tiles[g][0];
        t1 = gg_ship_tiles[g][1];
    } else {
        const uint8_t *f = gg_cube_frames[(player.gravity_flipped ? 25 : 0) + player.anim_frame];
        t0 = f[0];
        t1 = f[1];
    }
    sy += DEVICE_SPRITE_PX_OFFSET_Y;
    sx += DEVICE_SPRITE_PX_OFFSET_X;
    y[0] = sy;
    y[1] = sy;
    xt[0] = sx - 1;
    xt[1] = t0;
    xt[2] = sx + 7;
    xt[3] = t1;
}

// Object coordinates are converted to screen space biased by +64 so everything from -64 to 191
// fits in a uint8_t and all visibility checks are unsigned 8-bit compares. Sprite items (dx/dy
// from -16 to +48) that wrap past 255 land below the visible range, so they are culled too.
#define BIAS 64
static uint8_t draw_objects(uint16_t scroll_px, uint16_t cam_y) {
    uint8_t slot = FIRST_OBJ_SPRITE;
    uint8_t *oam_y = OAM_Y(slot);
    uint8_t *oam_xt = OAM_XT(slot);

    while (vis_lo < GG_OBJ_COUNT && gg_objs[vis_lo].x + 48u < scroll_px) vis_lo++;

    const GgObj *o = &gg_objs[vis_lo];
    for (uint8_t i = GG_OBJ_COUNT - vis_lo; i; i--, o++) {
        uint16_t dx = o->x - scroll_px;              // objects left of scroll_px - 48 were skipped above
        if (dx >= SCREEN_W + 1 && dx < 0x8000u) break;   // sorted by x: everything after is off to the right
#ifdef __TARGET_sms
        {
            int16_t sy = (int16_t)o->y - (int16_t)cam_y;
            if (sy < -48 || sy > SCREEN_H) continue;
            const GgObjDef *d = &gg_obj_defs[o->type];
            uint8_t n = d->count;
            if (slot + n > MAX_HARDWARE_SPRITES) break;
            const GgSprItem *it = &gg_spr_items[d->start];
            for (; n; n--, it++) {
                int16_t ix = (int16_t)dx + it->dx;
                int16_t iy = sy + it->dy;
                if (ix < 0 || ix > 255 || iy < -15 || iy >= SCREEN_H) continue;
                *oam_y++ = (uint8_t)(iy + DEVICE_SPRITE_PX_OFFSET_Y);
                *oam_xt++ = (uint8_t)ix;
                *oam_xt++ = it->tile;
                slot++;
            }
            continue;
        }
#endif
        uint16_t dy = (uint16_t)o->y + BIAS - cam_y;
        if (dy > 144 + BIAS) continue;               // also catches dy < 0 (wrapped)
        uint8_t bx = (uint8_t)dx + BIAS;
        uint8_t by = (uint8_t)dy;
        if (by < BIAS - 48) continue;

        const GgObjDef *d = &gg_obj_defs[o->type];
        uint8_t n = d->count;
        if (slot + n > MAX_HARDWARE_SPRITES) break;
        const GgSprItem *it = &gg_spr_items[d->start];
        for (; n; n--, it++) {
            uint8_t ix = bx + (uint8_t)it->dx;
            uint8_t iy = by + (uint8_t)it->dy;
            if (ix < BIAS - 7 || ix > BIAS + 159 || iy < BIAS - 15 || iy > BIAS + 143) continue;
            *oam_y++ = iy - BIAS + DEVICE_SPRITE_PX_OFFSET_Y;
            *oam_xt++ = ix - BIAS + DEVICE_SPRITE_PX_OFFSET_X;
            *oam_xt++ = it->tile;
            slot++;
        }
    }
    return slot;
}

// ---------------------------------------------------------------- level flow

static void set_scroll(uint16_t scroll_px, uint16_t py) {
    // Nametable pixel (0,0) is the top-left of the visible area, so no Game Gear
    // screen offset is applied (verified in Mesen).
    move_bkg((uint8_t)scroll_px, (uint8_t)py);
}

static void start_level(void) {
    DISPLAY_OFF;

    player_init(&player, 0, 240);
    cam_px = 0;
    cam_py = CAM_PY_MAX;
    scroll_acc = 0;
    cached_col = 0xFFFF;
    max_scroll_px = (uint16_t)(MAP_W - VIEW_MT_W) << 4;

    memset(obj_done, 0, sizeof(obj_done));
    obj_lo = 0;
    vis_lo = 0;
    memcpy(bg_pal, gg_bg_palette, sizeof(bg_pal));
    load_palette(0, bg_pal);
    pal_dirty = 0;

#ifdef DEBUG_START_PX
    cam_px = DEBUG_START_PX;   // debug builds can start part-way through the level
#endif
    uint16_t first_mc = cam_px >> 4;
    uint16_t scroll_px = (cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0;
    first_mc = scroll_px >> 4;

    // Fill the nametable: 16 columns from the left edge of the screen, rows 6..33 (covers camera Y 128)
    v0 = MAX_V0;
    loaded_mc = first_mc + RING_MT_W - 1;
    for (uint16_t mc = first_mc; mc < first_mc + RING_MT_W; mc++) {
        prepare_column(mc);
        flush_column(mc);
    }

    set_scroll(scroll_px, cam_py);
    end_sprites(0);
    DISPLAY_ON;
    music_start();
}

void main(void) {
    DISPLAY_OFF;
    // Everything in the fixed banks (0/1) must stay resident, so the 0x4000 window is
    // used for the startup-only tile data (bank 3) and then the level map (bank 2).
    // hardware nametable origin = GBDK's address for tile (0,0) minus the visible-area offset
    nt_origin = ((uint16_t)get_bkg_xy_addr(0, 0) - (DEVICE_SCREEN_Y_OFFSET * 64 + DEVICE_SCREEN_X_OFFSET * 2)) | 0x4000u;
    SWITCH_ROM(3);
    set_bkg_4bpp_data(0, GG_BG_TILE_COUNT, gg_bg_tiles);
    set_sprite_4bpp_data(0, GG_SPR_TILE_COUNT, gg_spr_tiles);
    SWITCH_ROM(2);
    load_palette(1, gg_spr_palette);
#ifdef __TARGET_sms
    HIDE_LEFT_COLUMN;
#endif
    SPRITES_8x16;
    SHOW_BKG;
    SHOW_SPRITES;

#ifndef DEBUG_NOMUSIC
    add_VBL(music_tick);
#endif
    start_level();

    uint8_t dead_timer = 0;

    while (1) {
        uint8_t joy = joypad();
        if (joy & (J_UP | J_B)) joy |= J_A;
#ifdef DEBUG_FREEZE
        {   // debug: stop the whole game after N frames so screenshots are deterministic
            static uint16_t dbg_frames;
            if (dbg_frames < DEBUG_FREEZE) dbg_frames++;
            else { wait_vbl_done(); continue; }
        }
#endif

        if (dead_timer) {
            end_sprites(0);
            wait_vbl_done();
            if (--dead_timer == 0) start_level();
            continue;
        }

        PROF_MARK(1);   // scroll + collision columns

        // --- scroll ---
        uint16_t px_prev = cam_px >> 4;
        uint8_t needs_col = 0;
        uint16_t need_mc = 0;
        uint16_t px_curr = px_prev;

        if (cam_px < max_scroll_px) {
            scroll_acc += SCROLL_SPEED_FP;
            cam_px += scroll_acc >> 8;
            scroll_acc &= 0xFF;
            px_curr = cam_px >> 4;
#ifdef __TARGET_sms
            {
                uint16_t s = (cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0;
                uint16_t need = ((s + LEFT_MASK_PX) >> 4) + (RING_MT_W - 1);
#else
            if (px_curr != px_prev) {
                uint16_t need = px_curr + VIEW_MT_W;
#endif
                if (need > loaded_mc && need < MAP_W) {
                    needs_col = 1;
                    need_mc = need;
                }
            }
        } else {
            // Reached the end of the level: restart
            music_stop();
            dead_timer = 30;
            continue;
        }

        player.world_x = cam_px;
        if (px_curr != cached_col) {
            load_collision_columns(px_curr);
            cached_col = px_curr;
        }

        PROF_MARK(2);   // object logic
        process_objects(joy);
        PROF_MARK(3);   // player physics
        uint8_t died = player_update(&player, joy, collision_columns, MAP_H);
#ifdef DEBUG_GODMODE
        if (died) {
            died = 0;
            player.dead = 0;
            player.world_y.w = (uint16_t)240 << 8;
            player.vel_y.w = 0;
        }
#endif

        PROF_MARK(4);   // camera + streaming decisions
        // --- camera follow (vertical) ---
        if (!died) {
            int16_t py = (int16_t)player.world_y.b.h - (int16_t)cam_py;
            int16_t target = (int16_t)cam_py;
            if (py < CAM_Y_TOP_ZONE) target = (int16_t)player.world_y.b.h - CAM_Y_TOP_ZONE;
            else if (py > CAM_Y_BOTTOM_ZONE) target = (int16_t)player.world_y.b.h - CAM_Y_BOTTOM_ZONE;
            if (target < 0) target = 0;
            if (target > CAM_PY_MAX) target = CAM_PY_MAX;
            cam_py = (uint16_t)target;
        }

        uint16_t scroll_px = (cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0;
        uint8_t sprite_x = (cam_px < PLAYER_SCREEN_X) ? (uint8_t)cam_px : PLAYER_SCREEN_X;
        int16_t final_py = (int16_t)player.world_y.b.h - (int16_t)cam_py;
        if (final_py < 0) final_py = 0;
        else if (final_py > SCREEN_H) final_py = SCREEN_H;

        // --- vertical streaming: decide which nametable row (if any) to load ---
        uint8_t row_pending = 0;
        uint8_t row_to_load = 0;
        uint8_t new_v0 = v0;
        {
            uint8_t cam_top = (uint8_t)(cam_py >> 3);
            uint8_t cam_bot = (uint8_t)((cam_py + SCREEN_H - 1) >> 3);
            if (cam_bot < WORLD_ROWS - 1) cam_bot++;          // 1 row of lookahead
            if (cam_top > 0) cam_top--;
            if (cam_top < v0) {
                new_v0 = v0 - 1;
                row_to_load = new_v0;
                row_pending = 1;
            } else if (cam_bot > v0 + RING_ROWS - 1 && v0 < MAX_V0) {
                row_to_load = v0 + RING_ROWS;
                new_v0 = v0 + 1;
                row_pending = 1;
            }
        }
        PROF_MARK(5);   // prepare row/column
        if (row_pending) prepare_row(row_to_load);

        if (needs_col) {
            // Columns are written for the row window that will be valid after this frame.
            uint8_t saved_v0 = v0;
            v0 = new_v0;
            prepare_column(need_mc);
            v0 = saved_v0;
        }

        // --- everything below touches VRAM/VDP registers: do it in vblank ---
        PROF_MARK(6);   // waiting for vblank
        wait_vbl_done();
        set_scroll(scroll_px, cam_py);
        PROF_MARK(7);   // VRAM writes (in vblank)

        if (pal_dirty) {
            load_palette(0, bg_pal);
            pal_dirty = 0;
        }
        if (row_pending) {
            flush_row(row_to_load);
            v0 = new_v0;
        }
        if (needs_col) {
            flush_column(need_mc);
            loaded_mc = need_mc;
        }

        PROF_MARK(8);   // sprites
        if (died) {
            end_sprites(0);
            music_stop();
            dead_timer = 30;
        } else {
            draw_player(sprite_x, (uint8_t)final_py);
            end_sprites(draw_objects(scroll_px, cam_py));
        }
    }
}
