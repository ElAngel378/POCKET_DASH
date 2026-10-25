#pragma bank 10

#include <gb/gb.h>
#include "music.h"
#include <gbdk/font.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <string.h>

#include "gameplay.h"
#include "player.h"
#include "assets.h"
#include "famidash_sprites.h"
#include "famidash_bg.h"
#include "gbc_palettes.h"
#include "bg_tiles.h"
BANKREF_EXTERN(chr_gb)   // base level tile sheet (src/graphics/tileset.c)

#define DEBUG_MODE
#include "famidash_metatiles.h"
#include "hUGEDriver.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "level_complete_sfx.h"
#include "fade.h"
#include "death_effect.h"
#include "save_manager.h"
#include "icon_catalog.h"
#include "sp_draw.h"

// Player colours from the icon select menu (sprite palette 0: 2 primary = the outer fill,
// 1 secondary). The colour table is in bank 24: read it through the banked icon_color().
static void apply_custom_player_palette(void) {
    if (_cpu == CGB_TYPE) {
        palette_color_t custom_pal[4];
        custom_pal[0] = RGB8(255, 255, 255);
        custom_pal[1] = icon_color(selected_color_secondary);
        custom_pal[2] = icon_color(selected_color_primary);
        custom_pal[3] = RGB8(0, 0, 0);
        fade_set_sprite_palette(0, 1, custom_pal);
        famidash_set_bg_accent(custom_pal[2]);   // the background decorations too
    }
}
#include "pause_buttons.h"
#include "debug_mode.h"
#include "percent_hud.h"
#include "bg_parallax.h"
#include "cube_icon_frames.h"
#include "collision.h"
#include "settings.h"
#include "level_complete.h"


#define BKG_MT_W 16

// Famidash chooses the decoration art per level: only Xstep (index 9) uses DECOCLOUD
// (ground "spikes" drawn as round bushes); every other level uses DECO1.
#define LEVEL_XSTEP 9
#define LEVEL_CLUTTERFUNK 10
#define LEVEL_TOE 11
#define LEVEL_ELECTROMAN 12
#define LEVEL_DECO_CLOUD(idx) ((idx) == LEVEL_XSTEP || (idx) == LEVEL_CLUTTERFUNK || (idx) == LEVEL_TOE || (idx) == LEVEL_ELECTROMAN)
// ... and spike set B (background spikes drawn as round bushes, see mt_renderer.c)
#define LEVEL_SPIKES_B(idx) ((idx) == LEVEL_XSTEP)
#define BKG_MT_H 16
#define VIEW_MT_W 10
#define VIEW_MT_H 9

// Level end animation timings
#define LEVEL_END_SHAKE_FRAMES 120  // Screen shake duration (2s at 60fps)
#define LEVEL_END_PULL_FRAMES   72  // Magnetic pull towards end trigger (~1.2s)
#define LEVEL_END_OVERSHOOT_PX  14  // Y overshoot amplitude in pixels

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

// End-animation state — non-static so sp_draw.c can reference them via gameplay.h externs
uint8_t  end_anim_state;
uint8_t  end_anim_frame;
uint8_t  end_shake_timer;
uint8_t  end_trigger_requested;
uint16_t end_trigger_obj_x;
uint16_t end_trigger_obj_y;

static uint16_t locked_scroll_px;
static uint16_t locked_cam_py;
static int16_t end_start_x;
static int16_t end_start_y;
static int16_t end_target_x;
static int16_t end_target_y;
static uint8_t last_bg_phase = 0xFF;
static uint8_t bg_drift_px = 0;

// Scroll speed in 8.8 fixed point (pixels per frame)
// Example: 3.0 = 768, 3.5 = 896, 4.0 = 1024
// 714 = 708 x 714/708 = famidash 60fps speed corrected for the Game Boy's
// ~59.7fps refresh. Must match the physics rescale factor in include/player.h
// so vertical trajectories stay aligned with scroll (see "Better ship").
#define SCROLL_SPEED_FP 714

// Camera y. Cube: dead zone, the camera snaps to keep the player within screen rows
// CAM_Y_TOP_ZONE .. CAM_Y_BOTTOM_ZONE. Ship: the corridor is CAM_SHIP_H (10 blocks) around the
// ship portal, on the block grid, and on the ground when it would reach below it. The screen
// (9 blocks) slides through it with the ship, CAM_SHIP_MARGIN past both ends: with the ship at the
// top the screen starts that far above the corridor (a bit of the ceiling shows), at the bottom it
// ends that far below it.
// SHIP CAM: OLD in the settings gives the ship the cube camera instead.
// Ball: centred on the ball portal. Both ease there (1/4 of the distance per frame,
// 1..CAM_Y_MAX_STEP px) and never leave the player off screen.
#define CAM_Y_TOP_ZONE 20      // cube: dead zone (screen rows the player may move in)
#define CAM_Y_BOTTOM_ZONE 100
#define CAM_SHIP_H 160u
#define CAM_SHIP_MARGIN 8u     // ship: px of the level shown above / below the corridor
#define CAM_Y_MAX_STEP 8      // the VRAM band streams one map row (16px) per 2 frames
#define CAM_VIEW_H 144u
uint16_t cam_portal_y;

// Tall levels: maps are MAP_ROWS rows tall and bottom-aligned (a level made for 16 rows sits in
// rows MAP_ROWS-16 .. MAP_ROWS-1). World y is a 16-bit pixel position.
#define MAP_Y0 ((uint16_t)(MAP_ROWS - 16u) << 4)
// The player's physics stay 8.8 inside a 16-row collision window starting at player.y_base
// (world px, a multiple of 128). The window moves by 128px when the player gets within 48px of
// its top or bottom, so collision probes never leave it (physics code unchanged, same speed).
#define Y_BASE_MAX MAP_Y0
// The level's first row (rows above it are padding): the camera, the collision window and the
// VRAM band stay below it, and the player dies above it like at the top of a 16-row map before.
static uint16_t level_top_px;
#define PLAYER_WORLD_Y() ((uint16_t)(player.y_base + player.world_y.b.h))
// Drawn y offset: on a ceiling (flipped gravity) the player rests with its box in the ceiling's
// last rows, like Famidash's (player.c ceiling collision: cube / ball 2 px, ship / UFO 3, mini
// 1 less); drawn flush with it
#define PLAYER_DRAW_DY() (player.gravity_flipped ? (uint8_t)(((player.mode == MODE_SHIP || player.mode == MODE_UFO) ? 3u : 2u) - player.mini) : 0u)
// VRAM band (see mt_renderer.c): 16 rows around the (up to) 10 visible ones. It only moves when
// the screen comes within 2 rows of its edge, and then so that 3 rows are left on that side.
#define BAND_MAX ((_cpu == CGB_TYPE) ? (uint8_t)(GROUND_ROW - 15u) : (uint8_t)(MAP_ROWS - 16u))
static uint8_t band_for_cam(uint16_t cam_y, uint8_t band) {
    uint8_t vis_top = (uint8_t)(cam_y >> 4);
    uint8_t vis_bot = (uint8_t)((cam_y + 143u) >> 4);
    uint8_t band_min = (uint8_t)(level_top_px >> 4);
    if (band_min > BAND_MAX) band_min = BAND_MAX;
    if (vis_top < (uint8_t)(band + 2u)) {
        band = (vis_top > 3u) ? (uint8_t)(vis_top - 3u) : 0;
    } else if ((uint8_t)(vis_bot + 2u) > (uint8_t)(band + 15u)) {
        band = (uint8_t)(vis_bot - 12u);
    }
    if (band < band_min) band = band_min;
    if (band > BAND_MAX) band = BAND_MAX;
    return band;
}

// Index (0-3) of the default theme in bg_pals:
// bottom row (light), column 0 (gray). This is used at level start and after death.
extern uint8_t music_ready;

SpCache active_sp;
uint8_t collision_columns[32];

// Profiling build only (make PROFILE=1, see tools/profile.py): every section of the main loop
// writes its id here, and tools/mesen_profile.lua charges the CPU cycles to the sections.
#ifdef DEBUG_PROFILE
volatile uint8_t gpmark;
volatile uint16_t gpcamx;   // camera x, to say where in the level a frame was slow
volatile uint16_t gpcamy;   // camera y, player world y, mode | mini << 4 | gravity << 5 | dead << 7
volatile uint16_t gpplayy;  // (tools: camera / hitbox checks)
volatile uint8_t gpstate;
volatile uint8_t gpdeaths;  // deaths (in god mode: the ones that did not happen)
#define PROF_MARK(n) (gpmark = (n))
#else
#define PROF_MARK(n)
#endif

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

// A newly streamed map column is built and uploaded in COL_JOB_STEPS slices, one
// per frame (4 metatile rows built before VBlank, the matching 8 tile rows
// uploaded after it), so no single frame has to pay for a whole column. The
// column is written >= 6 frames before it can scroll into view.
#define COL_JOB_STEPS 4
// Row job: the map row that just entered the VRAM band is written over 2 frames,
// ROW_JOB_PER_FRAME ring positions per frame (as fast as the camera can move vertically).
#define ROW_JOB_PER_FRAME BG_RJ_SLOTS
static uint8_t row0_job_pos = 16; // == 16: idle
static uint8_t row_job_row;       // map row being written
static uint8_t band_cam_row = 0xFF; // camera row the band was last checked against
static uint8_t col_job_step = COL_JOB_STEPS; // == COL_JOB_STEPS: idle
static uint8_t col_job_slot;
static uint8_t col_job_issued;  // slice handed to the VBlank handler, not yet acknowledged
static uint8_t row0_job_issued;
static uint16_t col_job_col;    // column being / last written (columns up to loaded_r are wanted)
static uint16_t fix_saved_col = 0xFFFF;   // col_job_col while a broken block's column is rewritten
static uint16_t max_scroll_px;

// No column or row job: the ring holds the columns up to loaded_r and the band's rows
static void stream_jobs_reset(void) {
    col_job_col = loaded_r;
    col_job_step = COL_JOB_STEPS;
    fix_saved_col = 0xFFFF;
    row0_job_pos = 16;
    col_job_issued = row0_job_issued = 0; bg_cj_pending = bg_rj_pending = 0; bg_fj_rows = 0;
    band_cam_row = 0xFF;
}

static uint16_t scroll_acc;
static uint8_t prev_joy;
static uint8_t previous_oam_index;
static uint16_t sp_stream_idx;
static uint16_t sp_cache_col;
static uint8_t sp_fill_pending;
static uint16_t cached_collision_col;
static uint8_t view_rev;   // the orientation on screen (player.reversed: the one the game is in)

// CGB seamless mirror portal (mt_cgb_flip levels): the mirrored picture is built into the BG map
// that is not shown while the game runs (a few columns per frame, in the time left before
// VBlank), from when a mirror portal comes near; once the player is through it and the map holds
// the current column window, the VBlank handler switches LCDC to it together with the new
// scroll. Elsewhere (DMG) mirror_reload() redraws everything with the display off.
#define OBJ_MIRROR_PORTAL 126
#define OBJ_MIRROR_EXIT   121
#define MJ_LY_LIMIT 96u        // start a column / row only before this line (one takes ~45 lines)
#define MJ_MAX_WAIT 90u        // frames the switch may wait (then mirror_reload)
static uint8_t seamless;       // this level uses the seamless switch
static uint8_t mj_active;      // building the other map
static uint8_t mj_target;      // its orientation (!view_rev)
static uint8_t mj_map_hi;      // its address >> 8 (0x98 / 0x9C)
static uint16_t mj_next;       // next map column to build into it
static uint8_t mj_row_pending; // map row to write into it (the band moved), 0xFF: none
static uint8_t mj_wait;        // frames player.reversed != view_rev
static uint8_t mj_frame;       // bg_vbl_frames when this game loop iteration started
static uint8_t dmg_mirror_due; // DMG: mirror the picture right after the coming VBlank
static void bg_map_reset(void);
static uint8_t reduce_flash;
static uint8_t pause_suppress_jump;
#if ENABLE_DEBUG_MODE
static uint8_t debug_ly;      // scanline the game logic had finished at (last frame)
static uint8_t debug_max_ly;  // worst one since debug mode was switched on
#endif
static uint8_t target_bg_idx;
static uint8_t died;
static int16_t py;
Player player;   // the only player: player_update and the object logic use it directly (player.h)
#ifdef DEBUG_PROFILE
Player * volatile gpplayer = &player;   // tools: the bot reads the whole player state
#endif
static uint8_t practice_mode = 0;

// Player frames streamed from ROM (tools/make_cube_icon_frames.py): only the image shown is in
// VRAM, the VBlank handler copies it into sprite tiles 0..3 or 4..7, double buffered (the frame
// shown switches once the copy is done). The cube and the ship share the buffers.
// Cube (icon select 0..25, 0 = the original cube): 48 rotation steps of 7.5 degrees, 24 images,
// the other half drawn flipped both ways (the Famidash icons 1..25 too). The
// physics turn the cube in 24 steps (anim_frame); the half step in between comes from anim_timer.
// DMG: the player has its own sprite palette, OBP1 (dmg_player_obp).
// Ship: 31 images from 45 degrees nose down to 45 up in 3 degree steps, following its flight
// direction (ship_tilt_vy), eased.
// Ball: like the cube, 48 steps from 24 images, rolling all the time (anim_frame turns on the
// ground too).
// UFO: Famidash's, 5 images: 15 and 8 degrees nose down, level, 8 and 15 up, following its
// vertical speed (ufo_tilt_vy): it tilts nose down on a jump and up as it falls.
// Mini ship, ball and UFO: 8x8 images in the first tile of the buffer (the cube's 3 stay in VRAM).
// Frame ids: cube step 0..47, ship PL_SHIP + image, ball PL_BALL + step, UFO PL_UFO + image,
// mini PL_MINI + image (MINI_*_FIRST ..).
#define PL_SHIP 64
#define PL_BALL 128
#define PL_UFO 192
#define PL_MINI 200
// ship / UFO: tilted images, drawn mirrored (no half turn), 1 px higher than the cube
#define PL_TILTED(id) ((id) >= PL_UFO || ((id) >= PL_SHIP && (id) < PL_BALL))
static metasprite_t cube_ms[3];
static uint8_t cube_front;   // buffer shown: tiles 0..3 / 4..7
static uint8_t cube_shown;   // frame id of the image in it
static uint8_t cube_req;     // frame id being copied to the other buffer, 0xFF: none
static uint8_t cube_mirror;  // bit 0 gravity_flipped, bit 1 view_rev: the ship / UFO is drawn flipped Y / X,
                             // the cube and the ball spin the other way with one of them (cube_turn)
static uint8_t cube_img_mir; // cube_mirror the frame shown was chosen in
static uint8_t cube_req_mir; // ... and the one being copied
static uint8_t ship_tilt;    // ship image shown (eased towards the flight direction)
static uint8_t ufo_tilt;     // ... and the UFO's

// image (0..23) of a step: no division on the SM83
#define CUBE_IMG(st) ((uint8_t)((st) >= CUBE_ICON_FRAMES ? (st) - CUBE_ICON_FRAMES : (st)))
#define CUBE_MIRROR() ((uint8_t)(player.gravity_flipped | (view_rev << 1)))
// the image of a frame id (cube / ball steps 24 apart share one)
#define PL_KEY(id) ((id) >= PL_UFO ? (id) : (id) >= PL_BALL ? (uint8_t)(PL_BALL + CUBE_IMG((uint8_t)((id) - PL_BALL)))                     : (id) >= PL_SHIP ? (id) : CUBE_IMG(id))

static uint8_t cube_step(void) {
    uint8_t s = (uint8_t)(player.anim_frame << 1);
    if (player.anim_timer >= 11u) s++;   // halfway to the next step (in the air, or settling)
    return s;
}

static uint8_t ball_step(void) {
    return (uint8_t)((player.anim_frame << 1) + (player.anim_timer >= 11u ? 1u : 0u));
}

// The cube and the ball are never drawn mirrored (an icon that is not symmetric would turn into
// its mirror image): their angle on screen, in half steps clockwise, is the step, or minus the
// step on the ceiling or in mirror mode (one of them: they spin anticlockwise there).
static uint8_t cube_turn(uint8_t s) {
    return (cube_mirror == 1u || cube_mirror == 2u) ? (uint8_t)(s ? 48u - s : 0) : s;
}

static uint8_t player_frame_id(void) {
    if (player.mode == MODE_SHIP) return (uint8_t)(PL_SHIP + ship_tilt);
    if (player.mode == MODE_UFO) return (uint8_t)(PL_UFO + ufo_tilt);
    if (player.mode == MODE_BALL) return (uint8_t)(PL_BALL + cube_turn(ball_step()));
    return cube_turn(cube_step());
}

// Icons 1..45 (Famidash and custom, tools/make_cube_icon_frames.py): 24 frames like icon 0, 10 icons a bank
static const uint8_t *player_frame_src(uint8_t id) {
    if (id >= PL_MINI) return MINI_FRAME(id - PL_MINI);
    if (id >= PL_UFO) return UFO_FRAME(id - PL_UFO);
    if (id >= PL_BALL) return BALL_FRAME(CUBE_IMG((uint8_t)(id - PL_BALL)));
    if (id >= PL_SHIP) return SHIP_FRAME(id - PL_SHIP);
    if (selected_icon == 0) return CUBE_ICON0_FRAME(CUBE_IMG(id));
    uint8_t j = (uint8_t)(selected_icon - 1u);
    const uint8_t *base = cube_fd_frames_0;
    if (j >= CUBE_FD_PER_BANK) { j -= CUBE_FD_PER_BANK; base = cube_fd_frames_1; }
    if (j >= CUBE_FD_PER_BANK) { j -= CUBE_FD_PER_BANK; base = cube_fd_frames_2; }
    if (j >= CUBE_FD_PER_BANK) { j -= CUBE_FD_PER_BANK; base = cube_fd_frames_3; }
    if (j >= CUBE_FD_PER_BANK) { j -= CUBE_FD_PER_BANK; base = cube_fd_frames_4; }
    return base + ((uint16_t)j * CUBE_FD_FRAMES + CUBE_IMG(id)) * 64u;
}

static uint8_t player_frame_bank(uint8_t id) {
    if (id >= PL_SHIP || selected_icon == 0) return BANK(cube_icon_frames);
    if (selected_icon <= CUBE_FD_PER_BANK) return BANK(cube_fd_frames_0);
    if (selected_icon <= 2 * CUBE_FD_PER_BANK) return BANK(cube_fd_frames_1);
    if (selected_icon <= 3 * CUBE_FD_PER_BANK) return BANK(cube_fd_frames_2);
    return (selected_icon <= 4 * CUBE_FD_PER_BANK) ? BANK(cube_fd_frames_3) : BANK(cube_fd_frames_4);
}

// Display off, after load_gameplay_sprite_tiles and player init
static void cube_icon_reset(void) {
    cube_req = 0xFF;
    bg_cube_pending = 0;
    cube_mirror = cube_img_mir = CUBE_MIRROR();
    cube_front = 0;
    ship_tilt = SHIP_FRAME_LEVEL;
    ufo_tilt = UFO_FRAME_LEVEL;
    cube_shown = player_frame_id();
    cube_tiles_load_now(player_frame_src(cube_shown), player_frame_bank(cube_shown), 0);
}

// Ship: ease the tilt towards the flight direction, at most 2 images (6 degrees) a frame
static const uint16_t ship_tilt_vy[] = SHIP_TILT_VY;
static void ship_tilt_update(void) {
    int16_t vy = player.gravity_flipped ? -player.vel_y.w : player.vel_y.w;   // + toward the rest surface
    uint16_t a = (vy < 0) ? (uint16_t)-vy : (uint16_t)vy;
    uint8_t k = 0;
    while (k < (uint8_t)sizeof(ship_tilt_vy) / 2u && a >= ship_tilt_vy[k]) k++;
    uint8_t target = (vy > 0) ? (uint8_t)(SHIP_FRAME_LEVEL - k) : (uint8_t)(SHIP_FRAME_LEVEL + k);
    if (ship_tilt + 2u <= target) ship_tilt += 2;
    else if (ship_tilt < target) ship_tilt++;
    else if (ship_tilt >= target + 2u) ship_tilt -= 2;
    else if (ship_tilt > target) ship_tilt--;
}

// UFO: 2 steps (8, 15 degrees) from its speed, one image a frame: the other way from the ship, nose
// down on a jump, level around its top and on the ground, up as it falls
static const uint16_t ufo_tilt_vy[] = UFO_TILT_VY;
static void ufo_tilt_update(void) {
    int16_t vy = player.gravity_flipped ? -player.vel_y.w : player.vel_y.w;   // + toward the rest surface
    uint16_t a = (vy < 0) ? (uint16_t)-vy : (uint16_t)vy;
    uint8_t k = 0;
    while (k < (uint8_t)sizeof(ufo_tilt_vy) / 2u && a >= ufo_tilt_vy[k]) k++;
    uint8_t target = (vy > 0) ? (uint8_t)(UFO_FRAME_LEVEL + k) : (uint8_t)(UFO_FRAME_LEVEL - k);
    if (ufo_tilt < target) ufo_tilt++;
    else if (ufo_tilt > target) ufo_tilt--;
}

// Asks for frame want (copied into the other buffer if it is not the one shown) and returns the
// frame id shown this frame, in the buffer at sprite tile cube_front * 4
static uint8_t player_frame_show(uint8_t want) {
    if (cube_req != 0xFF && !bg_cube_pending) {
        cube_front ^= 1;
        cube_shown = cube_req;
        cube_img_mir = cube_req_mir;
        cube_req = 0xFF;
    }
    uint8_t same = (PL_KEY(want) == PL_KEY(cube_shown));
    if (same) cube_img_mir = cube_mirror;
    else if (cube_req == 0xFF) {
        bg_cube_src = player_frame_src(want);
        bg_cube_bank = player_frame_bank(want);
        bg_cube_dst = (uint8_t *)(0x8000u + ((uint16_t)(cube_front ^ 1u) << 6));
        cube_req = want;
        cube_req_mir = cube_mirror;
        bg_cube_pending = 4;   // tiles (DMG: copied a few per VBlank)
    }
    return same ? want : cube_shown;
}

static const metasprite_t *player_frame_metasprite(uint8_t want) {
    uint8_t show = player_frame_show(want);
    uint8_t t = (uint8_t)(cube_front << 2);
    // cube / ball, second half turn: the image of the step - 24, flipped both ways (halves swapped)
    uint8_t tilted = PL_TILTED(show);
    uint8_t turn = tilted ? 0 : (show >= PL_BALL) ? (uint8_t)(show - PL_BALL) : show;
    uint8_t flip = (turn >= CUBE_ICON_FRAMES) ? (S_FLIPX | S_FLIPY) : 0;
    uint8_t l = flip ? (uint8_t)(t + 2) : t, r = flip ? t : (uint8_t)(t + 2);
    // still the image from before a flip of the drawing (its copy is a frame late): undo that
    // flip, so for this frame the player looks exactly as on the last one
    uint8_t undo = tilted ? (uint8_t)(cube_img_mir ^ cube_mirror) : 0;
    if (undo & 1u) flip ^= S_FLIPY;
    if (undo & 2u) { uint8_t x = l; l = r; r = x; flip ^= S_FLIPX; }
    int8_t dy = tilted ? -1 : 0;                         // the ship and the UFO a pixel higher,
    int8_t dx = (show >= PL_BALL && !tilted) ? 0 : -1;   // the ball a pixel further right
    flip |= S_PALETTE;   // DMG: OBP1, the player's palette (CGB: ignored)
    cube_ms[0].dy = dy; cube_ms[0].dx = dx; cube_ms[0].dtile = l; cube_ms[0].props = flip;
    cube_ms[1].dy = 0;  cube_ms[1].dx = 8;  cube_ms[1].dtile = r; cube_ms[1].props = flip;
    cube_ms[2].dy = (int8_t)0x80;   // METASPR_TERM
    return cube_ms;
}

static const uint8_t bg_pals[] = {
    0xE4, // 0: Normal (W:W, LG:LG, DG:DG, B:B)
    0x39, // 1: Inverse (W:LG, LG:DG, DG:B, B:W)
    0x3E, // 2: Inverse (W:DG, LG:B, DG:B, B:W)
    0x3F  // 3: Inverse (W:B, LG:B, DG:B, B:W)
};

// DMG background theme (bg_pals index) a level starts with: the same mapping as the colour triggers
// in process_sprite_logic, applied to the level's starting background colour (NES colour id) so
// the first stretch before the first trigger already has the right shades.
static uint8_t dmg_start_bg_idx(void) {
    if (_cpu == CGB_TYPE) return 0;
    uint8_t c = l->bg_color;
    return (c < 16) ? ((c == 15) ? 3 : 2) : ((c < 32) ? 1 : 0);
}

// The theme actually shown: "gradient" off keeps the inverse themes (1, 2) on the normal one
static uint8_t dmg_shown_bg_idx(uint8_t idx) {
    return (reduce_flash && (idx == 1 || idx == 2)) ? 0 : idx;
}

// Sprite palette on a black DMG background (bg_pals[3]): shades 1 and 2 as normal, black -> white
#define DMG_OBP_ON_BLACK 0x24
// OBP1: the player (cube, ship, ball, mini), in the shades chosen in the icon select menu on every
// theme: colour 1 secondary, 2 primary, 3 outline. The outline is black, or white on the dark grey
// and black themes (2, 3), so the cube always stands out from the sky, also on a real DMG screen.
static uint8_t dmg_player_obp(uint8_t idx) {
    uint8_t outline = (idx >= 2) ? 0 : 3;
    return (uint8_t)((outline << 6) | (selected_dmg_primary << 4) | (selected_dmg_secondary << 2));
}

// Shared blank tile for SHOW BG off (solid areas instead of BG art).
// Lives in this bank (10), NOT in HOME/bank 0 which is 98% full.
static const uint8_t blank_bg_tile[16] = {0};

// Blank CGB parallax VRAM (Bank 1 tiles 0..47) so parallax areas render
// as solid sky color. Must be called with DISPLAY OFF. Bank-10 local so
// HOME stays untouched.
static void blank_parallax_vram(void) {
    if (_cpu != CGB_TYPE) return;
    VBK_REG = 1;
    for (uint8_t i = 0; i < 48; i++) {
        set_bkg_data(i, 1, blank_bg_tile);
    }
    VBK_REG = 0;
}

static void reload_level_state(uint8_t idx) {
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    disable_interrupts();
    DISPLAY_OFF;
    bg_map_reset();
    view_rev = 0;

    // Reload tileset and sprite data on respawn/restart
    load_level_bg_sheet(idx, 0, level_tiles, level_tile_count, level_tiles_bank);
    apply_level_bg_tiles(idx, 0);
    if (!setting_show_bg_enabled) {
        // Hide BG: blank DMG tile 12 so empty areas are solid
        set_bkg_data(12, 1, blank_bg_tile);
    }

    load_gameplay_sprite_tiles(LEVEL_DECO_CLOUD(idx));   // sprite_tiles.png
    reapply_bg_shared_slots(idx, 0);   // DMG: saw tiles in sprite tiles it overwrote
    if (practice_mode) load_checkpoint_tiles();
    debug_load_hud_tiles();
    percent_hud_load_tiles();
    bg_drift_px = 0;
    if (_cpu == CGB_TYPE) {
        if (setting_show_bg_enabled) init_bg_parallax();
        else blank_parallax_vram();
        last_bg_phase = 0;
        load_menu_ground_tiles();
        cam_py = MAP_Y0 + 128u;
    } else {
        cam_py = MAP_Y0 + 112u;
    }
    mt_band = band_for_cam(cam_py, BAND_MAX);

    cam_px = 0;
    scroll_acc = 0;
    loaded_r = BKG_MT_W - 1;
    stream_jobs_reset();
    broken_reset();   // broken blocks come back
    target_bg_idx = dmg_start_bg_idx();
    pause_suppress_jump = 0;
    end_anim_state = END_ANIM_INACTIVE;
    end_anim_frame = 0;
    end_shake_timer = 0;
    end_trigger_requested = 0;
    player_init(&player, 0, 240);
    player.y_base = Y_BASE_MAX;
    cube_icon_reset();
    sp_cache_reset(&active_sp, &sp_stream_idx);
    coins_reset();
    coins_saved = saved_level_coins(level_save_slot[idx]);
    percent_hud_reset(max_scroll_px);
    attempt_text_start(1, 0, cam_py);
    sp_cache_col = 0xFFFF;
    sp_fill_pending = 0;
    previous_oam_index = MAX_HARDWARE_SPRITES;
    cached_collision_col = 0xFFFF;
    move_bkg(0, (uint8_t)cam_py);
    fade_scx = fade_psx = 0;   // (the player starts at the screen's left edge)
    BGP_REG = bg_pals[dmg_shown_bg_idx(target_bg_idx)];
    if (_cpu == CGB_TYPE) {
        famidash_reset_bg_palettes(idx);
    }
    fill_scroll_bg(level_map, level_map_w, level_map_bank, 0);
    DISPLAY_ON;
    if (setting_music_enabled) {
        // the Furnace song, else the hUGETracker one (Theory of Everything only has a Furnace song)
        uint8_t playing = music_play_level_fur(idx);
        if (!playing && level_songs[idx]) {
            init_music_banked(level_songs[idx], song_bank[idx], l->timer_divider);
            current_song_bank = song_bank[idx];
            playing = 1;
        }
        if (playing) {
            TAC_REG = 0x04;
            music_ready = 1;
        }
    }
    enable_interrupts();
}

extern const hUGESong_t practice;
// StayInsideMe(WIP).uge plays on VBlank (timer off) at 7 ticks/row: ~59.7 Hz from the
// 4096 Hz timer is 69 counts, TMA = 256 - 69
#define PRACTICE_MUSIC_DIVIDER 187

#define MAX_PRACTICE_CHECKPOINTS 10

typedef struct {
    Player   player;
    uint16_t cam_px;
    uint16_t cam_py;
    uint16_t scroll_acc;
    uint8_t  bg_drift_px;
    uint8_t  target_bg_idx;
    uint8_t  active;
    uint16_t world_x;
    uint16_t world_y;
    palette_color_t bg_palettes[20];
    uint16_t cam_portal_y;
} PracticeCheckpoint;

static PracticeCheckpoint practice_checkpoints[MAX_PRACTICE_CHECKPOINTS];
static uint8_t practice_cp_count = 0;
static uint16_t last_cp_cam_px = 0;

static void practice_clear_checkpoints(void) {
    uint8_t i;
    practice_cp_count = 0;
    last_cp_cam_px = 0;
    for (i = 0; i < MAX_PRACTICE_CHECKPOINTS; i++) {
        practice_checkpoints[i].active = 0;
    }
}

static void practice_add_checkpoint(void) {
    uint8_t i;
    PracticeCheckpoint *cp;
    if (player.dead || end_anim_state != END_ANIM_INACTIVE || player.level_complete) return;
    if (practice_cp_count >= MAX_PRACTICE_CHECKPOINTS) {
        for (i = 0; i < MAX_PRACTICE_CHECKPOINTS - 1; i++) {
            practice_checkpoints[i] = practice_checkpoints[i + 1];
        }
        practice_cp_count = MAX_PRACTICE_CHECKPOINTS - 1;
    }
    cp = &practice_checkpoints[practice_cp_count];
    cp->player = player;
    cp->cam_px = cam_px;
    cp->cam_py = cam_py;
    cp->cam_portal_y = cam_portal_y;
    cp->scroll_acc = scroll_acc;
    cp->bg_drift_px = bg_drift_px;
    cp->target_bg_idx = target_bg_idx;
    cp->world_x = cam_px;
    cp->world_y = PLAYER_WORLD_Y();
    cp->active = 1;
    if (_cpu == CGB_TYPE) {
        memcpy(cp->bg_palettes, famidash_bg_target, 20 * sizeof(palette_color_t));
    }
    practice_cp_count++;
    last_cp_cam_px = cam_px;
}

static void practice_remove_checkpoint(void) {
    if (practice_cp_count > 1) {
        practice_cp_count--;
        practice_checkpoints[practice_cp_count].active = 0;
        last_cp_cam_px = cam_px;
    } else if (practice_cp_count == 1) {
        practice_checkpoints[0].cam_px = 0;
        if (_cpu == CGB_TYPE) {
            practice_checkpoints[0].cam_py = MAP_Y0 + 128u;
        } else {
            practice_checkpoints[0].cam_py = MAP_Y0 + 112u;
        }
        practice_checkpoints[0].scroll_acc = 0;
        practice_checkpoints[0].bg_drift_px = 0;
        practice_checkpoints[0].target_bg_idx = dmg_start_bg_idx();
        practice_checkpoints[0].world_x = 0;
        practice_checkpoints[0].world_y = practice_checkpoints[0].cam_py;
        player_init(&practice_checkpoints[0].player, 0, 240);
        practice_checkpoints[0].player.y_base = Y_BASE_MAX;
        last_cp_cam_px = cam_px;
    }
}

static uint8_t practice_has_checkpoint(void) {
    return (practice_cp_count > 0 && practice_checkpoints[practice_cp_count - 1].active);
}

static void practice_init_checkpoint(void) {
    if (practice_cp_count == 0) {
        practice_add_checkpoint();
    }
}

static uint8_t practice_draw_checkpoints(uint8_t oam_index, uint16_t cam_x, uint16_t cam_y, uint8_t reversed) {
    uint8_t cpi;
    for (cpi = 0; cpi < practice_cp_count; cpi++) {
        PracticeCheckpoint *cp = &practice_checkpoints[cpi];
        if (!cp->active) continue;
        int16_t dist_x = (int16_t)cp->world_x - (int16_t)cam_x;
        int16_t ox;
        if (reversed) {
            ox = (int16_t)MIRROR_PLAYER_SCREEN_X - dist_x + 8;
        } else {
            ox = (cam_x < PLAYER_SCREEN_X) ? ((int16_t)cp->world_x + 8) : (dist_x + (int16_t)PLAYER_SCREEN_X + 8);
        }
        int16_t oy = (int16_t)cp->world_y - (int16_t)cam_y + 16;
        if (ox >= 0 && ox <= 168 && oy >= 0 && oy <= 160 && oam_index <= 38) {
            shadow_OAM[oam_index].x = (uint8_t)ox;
            shadow_OAM[oam_index].y = (uint8_t)oy;
            shadow_OAM[oam_index].tile = CHECKPOINT_TILE_BASE;
            shadow_OAM[oam_index].prop = (_cpu == CGB_TYPE) ? S_PAL(1) : 0;
            oam_index++;

            shadow_OAM[oam_index].x = (uint8_t)(ox + 8);
            shadow_OAM[oam_index].y = (uint8_t)oy;
            shadow_OAM[oam_index].tile = CHECKPOINT_TILE_BASE + 2;
            shadow_OAM[oam_index].prop = (_cpu == CGB_TYPE) ? S_PAL(1) : 0;
            oam_index++;
        }
    }
    return oam_index;
}

// play_level helpers: the rare paths (pause, level complete, mirror portal, death, level end).
// They are out of play_level because SDCC's register allocator is very slow on one huge
// function. Per-frame code stays inline: moving it out cost frames on DMG.

// Pause menu (Start). Returns 1 when the player chose to leave the level.
static void practice_respawn(uint8_t idx);

static uint8_t pause_menu(uint8_t idx) {
    music_gate_off();
    wait_vbl_done();

    // Pause music and mute active sound
    uint8_t saved_music_ready = music_ready;
    music_ready = 0;
    TAC_REG = 0x00; // Stop hardware timer to prevent music drift/desync
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
    wait_vbl_done();
    move_bkg((uint8_t)(saved_scx - fine_scx), (uint8_t)(saved_scy - fine_scy));

    if (_cpu == CGB_TYPE) {
        fade_apply_pause_box_palettes();
        apply_pause_box_attributes(1);
        static const palette_color_t pause_pal[4] = {
            RGB8(0, 0, 0), RGB8(0, 0, 0), RGB8(180, 215, 255), RGB8(255, 255, 255)
        };
        set_sprite_palette(7, 1, pause_pal);

        // Play / Menu / Restart: white ring, light green body (top sprites) and dark green body
        // (bottom sprites), yellow / cyan icon with a white border
        static const palette_color_t play_btn_pal[4] = {
            RGB8(0, 0, 0), RGB8(255, 255, 255), RGB8(176, 232, 96), RGB8(255, 235, 20)
        };
        static const palette_color_t play_btn_pal_dark[4] = {
            RGB8(0, 0, 0), RGB8(255, 255, 255), RGB8(116, 196, 52), RGB8(255, 235, 20)
        };
        static const palette_color_t misc_btn_pal[4] = {
            RGB8(0, 0, 0), RGB8(255, 255, 255), RGB8(176, 232, 96), RGB8(30, 245, 255)
        };
        static const palette_color_t misc_btn_pal_dark[4] = {
            RGB8(0, 0, 0), RGB8(255, 255, 255), RGB8(116, 196, 52), RGB8(30, 245, 255)
        };
        set_sprite_palette(6, 1, play_btn_pal);
        set_sprite_palette(2, 1, play_btn_pal_dark);
        set_sprite_palette(5, 1, misc_btn_pal);
        set_sprite_palette(1, 1, misc_btn_pal_dark);

        // Practice Button: White rim & diamond outline, 2-tone green body
        static const palette_color_t practice_btn_pal[4] = {
            RGB8(0, 0, 0), RGB8(255, 255, 255), RGB8(176, 232, 96), RGB8(116, 196, 52)
        };
        set_sprite_palette(4, 1, practice_btn_pal);
        // the inside of its diamond: pale green on top, bright green below
        static const palette_color_t practice_diamond_pal[4] = {
            RGB8(0, 0, 0), RGB8(176, 244, 176), RGB8(92, 240, 56), RGB8(255, 255, 255)
        };
        set_sprite_palette(3, 1, practice_diamond_pal);
    } else {
        BGP_REG = dim_dmg_byte(saved_bgp, 1);
        OBP0_REG = 0x90;
        OBP1_REG = 0x1C;
    }

    // Hide all gameplay and level sprites, then load the pause tiles: they borrow the death
    // effect and checkpoint tiles, which must be off screen while they are overwritten
    for (uint8_t i = 0; i < 40; i++) {
        shadow_OAM[i].y = 0;
    }
    wait_vbl_done();
    init_pause_tiles();

    uint8_t selected_btn = PAUSE_BTN_PLAY;
    draw_pause_menu_sprites(selected_btn);
#if ENABLE_DEBUG_MODE
    if (debug_mode) debug_draw_hud(0, 0, 0);
#endif
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
            if (selected_btn == 0) selected_btn = 3;
            else selected_btn--;
            draw_pause_menu_sprites(selected_btn);
        } else if (p_pressed & J_RIGHT) {
            if (selected_btn >= 3) selected_btn = 0;
            else selected_btn++;
            draw_pause_menu_sprites(selected_btn);
        } else if (p_pressed & (J_UP | J_DOWN)) {
            if (selected_btn == PAUSE_BTN_PRACTICE) selected_btn = PAUSE_BTN_PLAY;
            else selected_btn = PAUSE_BTN_PRACTICE;
            draw_pause_menu_sprites(selected_btn);
#if ENABLE_DEBUG_MODE
        } else if (p_pressed & J_B) {
            // B toggles debug mode (noclip + scanline readout); START resumes
            debug_mode ^= 1;
            debug_max_ly = 0;
            if (debug_mode) debug_draw_hud(0, 0, 0);
            else debug_hide_hud();
        } else if (p_pressed & J_START) {
            break;
#else
        } else if ((p_pressed & J_START) || (p_pressed & J_B)) {
            break;
#endif
        } else if (p_pressed & J_A) {
            if (selected_btn == PAUSE_BTN_PLAY) {
                break;
            } else if (selected_btn == PAUSE_BTN_MENU) {
                exit_level = 1;
                break;
            } else if (selected_btn == PAUSE_BTN_RESTART) {
                restart_level = 1;
                break;
            } else if (selected_btn == PAUSE_BTN_PRACTICE) {
                if (!practice_mode) {
                    practice_mode = 1;
                    practice_init_checkpoint();
                    if (setting_music_enabled) {
                        init_music_banked(&practice, 212, PRACTICE_MUSIC_DIVIDER);
                        current_song_bank = 212;
                        saved_music_ready = 1;
                    }
                    break;
                } else {
                    practice_mode = 0;
                    practice_clear_checkpoints();
                    restart_level = 1;
                    break;
                }
            }
        }
    }

    // Hide all pause menu UI sprites immediately
    for (uint8_t i = 0; i < 34; i++) {
        shadow_OAM[i].y = 0;
    }
    percent_hud_hide();   // redrawn next frame (the pause menu used its slots)

    if (_cpu == CGB_TYPE) {
        wait_vbl_done();
        apply_pause_box_attributes(0);
        fade_reset_view();   // fading objects: the pause box drew them full
        fade_restore_pause_box_palettes();
        set_sprite_palette(0, 8, gbc_sprite_palettes);
        apply_custom_player_palette();
        flush_ground_row();
    } else {
        BGP_REG = saved_bgp;
        OBP0_REG = saved_obp0;
        OBP1_REG = saved_obp1;
    }

    // Restore fractional background scroll
    move_bkg(saved_scx, saved_scy);

    if (exit_level) {
        practice_mode = 0;
        practice_clear_checkpoints();
        return 1;
    }

    // Practice mode: Restart goes back to the last checkpoint and the practice music keeps
    // going (resumed below like a normal unpause)
    if (restart_level && practice_mode) {
        practice_respawn(idx);
        restart_level = 0;
    }

    if (restart_level) {
        practice_clear_checkpoints();
        reload_level_state(idx);
        if (practice_mode) practice_init_checkpoint();
        prev_joy = joypad();
        if (prev_joy & J_UP) prev_joy |= J_A;
        player.last_joy = prev_joy;
        return 0;
    }

    NR30_REG = 0x80;
    music_reload_wave();

    wait_vbl_done();

    // The pause tiles borrowed these (exit and restart reload all sprite tiles)
    restore_death_tiles();
    restore_ship_tiles();
    if (practice_mode) load_checkpoint_tiles();
    attempt_text_load_tiles();

    // Synchronize music timer on VBLANK to prevent desync
    TIMA_REG = TMA_REG;
    IF_REG &= ~TIM_IFLAG;
    cgb_music_tick = 0;
    TAC_REG = 0x04;
    music_ready = saved_music_ready;

    prev_joy = joypad();
    if (prev_joy & J_UP) prev_joy |= J_A;
    player.last_joy = prev_joy;
    pause_suppress_jump = 1;
    return 0;
}

uint8_t  lc_pending;
uint8_t  lc_level;
uint8_t  lc_practice;
uint8_t  lc_coins;
uint16_t lc_attempts;

// The level was completed (after the end animation): save it and hand the result to
// STATE_LEVEL_COMPLETE, which shows the "LEVEL COMPLETE!" screen once play_level() returns
static void level_complete_record(uint8_t idx) {
    uint8_t slot = level_save_slot[idx];
    if (practice_mode) {
        record_level_progress(slot, 100, 1);
    } else {
        record_level_progress(slot, 100, 0);
        record_level_coins(slot, coins_collected);
    }
    lc_level = idx;
    lc_practice = practice_mode;
    lc_coins = coins_collected;
    lc_attempts = attempt_count;
    lc_pending = 1;
}

// The level end object was reached: start the pull-to-the-edge animation
static void start_end_anim(void) {
    percent_hud_complete();
    end_anim_state = END_ANIM_PULL;
    end_anim_frame = 0;
    locked_scroll_px = view_rev
        ? (uint16_t)(-(int16_t)cam_px - MIRROR_PLAYER_SCREEN_X)
        : ((cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0);
    locked_cam_py = cam_py;
    end_start_x = view_rev ? MIRROR_PLAYER_SCREEN_X : ((cam_px < PLAYER_SCREEN_X) ? (uint8_t)cam_px : PLAYER_SCREEN_X);
    end_start_y = (int16_t)PLAYER_WORLD_Y() + PLAYER_DRAW_DY() - (int16_t)cam_py;

    if (!view_rev) {
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

// First map column of the 16 the VRAM ring holds for camera column px (as mirror_reload loads them)
#define RING_COL0(px) ((px) >= 4u ? (uint16_t)((px) - 4u) : 0u)

// Display off: back to the 0x9800 map, no seamless switch in progress
static void bg_map_reset(void) {
    LCDC_REG &= (uint8_t)~LCDCF_BG9C00;
    bg_lcdc_map = 0xFF;
    mj_active = 0;
    mj_wait = 0;
    dmg_mirror_due = 0;
}

// A mirror portal that would change the view is (nearly) on screen
static uint8_t mirror_portal_ahead(void) {
    uint8_t want = view_rev ? OBJ_MIRROR_EXIT : OBJ_MIRROR_PORTAL;
    for (uint8_t i = 0; i < MAX_ACTIVE_SP_OBJECTS; i++) {
        if (active_sp.active[i] && active_sp.obj[i] == want && !active_sp.activated[i]) {
            uint16_t x = active_sp.px[i];
            if (x + 64u > cam_px && x < cam_px + 320u) return 1;
        }
    }
    return 0;
}

// Per frame, after the frame's work (CGB seamless levels): start, cancel or advance the build
// of the other map. Stops when VBlank comes near.
static void mirror_job(uint16_t px) {
    uint8_t needed = (player.reversed != view_rev);
    if (!mj_active) {
        if (!needed && !mirror_portal_ahead()) return;
        mj_active = 1;
        mj_target = !view_rev;
        mj_map_hi = (LCDC_REG & LCDCF_BG9C00) ? 0x98u : 0x9Cu;
        mj_next = 0;
        mj_row_pending = 0xFF;
    } else if (!needed && !mirror_portal_ahead()) {
        mj_active = 0;   // the portal was missed
        return;
    }
    uint16_t col0 = RING_COL0(px);
    uint16_t end = col0 + 16u;
    if (end > level_map_w) end = level_map_w;
    if (mj_next < col0) mj_next = col0;
    while (bg_vbl_frames == mj_frame && LY_REG < MJ_LY_LIMIT) {
        if (mj_row_pending != 0xFF) {
            mj_build_row(mj_row_pending, mj_next ? mj_next - 1u : 0, level_map, level_map_w, level_map_bank, mj_target, mj_map_hi);
            mj_row_pending = 0xFF;
        } else if (mj_next < end) {
            mj_build_column(mj_next, level_map, level_map_bank, mj_target, mj_map_hi);
            mj_next++;
        } else {
            break;
        }
    }
}

// The other map holds the window of camera column px and no row is missing
static uint8_t mirror_job_ready(uint16_t px) {
    uint16_t end = RING_COL0(px) + 16u;
    if (end > level_map_w) end = level_map_w;
    return mj_active && mj_row_pending == 0xFF && mj_next >= end;
}

// Show the other map from the next VBlank on, and stream in the new orientation
static void mirror_switch(uint16_t px) {
    view_rev = mj_target;
    mj_active = 0;
    mj_wait = 0;
    loaded_r = RING_COL0(px) + 15u;
    stream_jobs_reset();
    fade_reset_view();   // the other map's fading objects were built full: fade_update redraws them
    bg_lcdc_map = (mj_map_hi == 0x9Cu) ? LCDCF_BG9C00 : LCDCF_BG9800;
}

// Mirror portal: reload the (mirrored) tileset and redraw the visible columns
static void mirror_reload(uint8_t idx, uint8_t enable_display) {
    stream_jobs_reset();
    DISPLAY_OFF;
    bg_map_reset();
    view_rev = player.reversed;
    PROF_MARK(14);   // mirror: tileset

    // CGB flip mode: the normal sheet, the X flip attribute mirrors it
    const uint8_t* target_tiles = (player.reversed && !mt_cgb_flip)
        ? l->tiles_rev
        : level_tiles;
    load_level_bg_sheet(idx, player.reversed && !mt_cgb_flip, target_tiles, level_tile_count, level_tiles_bank);
    PROF_MARK(15);   // mirror: level tiles
    apply_level_bg_tiles(idx, player.reversed);
    if (!setting_show_bg_enabled) {
        set_bkg_data(12, 1, blank_bg_tile);
    }
    PROF_MARK(16);   // mirror: columns

    uint16_t init_scroll_px = player.reversed
        ? (uint16_t)(-(int16_t)cam_px - MIRROR_PLAYER_SCREEN_X)
        : ((cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0);
    fade_scx = (uint8_t)init_scroll_px;   // the columns' fading objects: for this view
    fade_psx = player.reversed ? MIRROR_PLAYER_SCREEN_X : ((cam_px < PLAYER_SCREEN_X) ? (uint8_t)cam_px : PLAYER_SCREEN_X);
    int32_t col_start = (int32_t)(cam_px >> 4) - 4;
    if (col_start < 0) col_start = 0;
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t curr_col = (uint16_t)(col_start + i);
        if (curr_col < level_map_w) {
            uint8_t vram_slot = (uint8_t)(curr_col & 15);
            if (player.reversed) vram_slot = (uint8_t)(-(int8_t)vram_slot & 15);
            prepare_mt_column(curr_col, level_map, level_map_bank, player.reversed);
            PROF_MARK(19);   // mirror: column flush
            flush_mt_column(vram_slot);
            PROF_MARK(16);
        }
    }

    PROF_MARK(17);   // mirror: sprite tiles
    // The tileset reload overwrote sprite tiles 128..159 (shared VRAM); no other sprite changes
    reload_bg_shared_sprite_tiles();
    PROF_MARK(18);   // mirror: rest

    move_bkg((uint8_t)init_scroll_px, (uint8_t)cam_py);

    if (enable_display) {
        SHOW_BKG;
        SHOW_SPRITES;
        SPRITES_8x16;
        DISPLAY_ON;
    }

    loaded_r = (uint16_t)(col_start + 15);
    col_job_col = loaded_r;
}

// DMG mirror portal, right after VBlank (so turning the display off does not wait): the
// mirrored sheet, and the map mirrored in place (see bg_map_reflect) instead of rebuilt. The
// ring holds the same columns either way; a column or row job still in progress was writing the
// other orientation, so it starts again.
static void mirror_reflect(uint8_t idx) {
    // Still in VBlank: switch the LCD off now. display_off() waits for the start of a VBlank,
    // which here would be the next one (a whole frame). Outside VBlank (a late frame) it is the
    // safe way: turning a DMG LCD off while it draws can damage it.
    disable_interrupts();
    if (LY_REG >= 144u && LY_REG <= 152u) LCDC_REG &= (uint8_t)~LCDCF_ON;
    enable_interrupts();
    DISPLAY_OFF;   // returns at once when it is off
    PROF_MARK(14);   // mirror: tileset
    load_level_bg_sheet(idx, player.reversed, player.reversed ? l->tiles_rev : level_tiles, level_tile_count, level_tiles_bank);
    PROF_MARK(15);   // mirror: level tiles
    apply_level_bg_tiles(idx, player.reversed);
    if (!setting_show_bg_enabled) set_bkg_data(12, 1, blank_bg_tile);
    PROF_MARK(16);   // mirror: columns (map mirrored in place)
    bg_map_reflect();
    PROF_MARK(17);   // mirror: sprite tiles
    // the tileset overwrote sprite tiles 128..159 (shared VRAM)
    reload_bg_shared_sprite_tiles();
    PROF_MARK(18);   // mirror: rest
    view_rev = player.reversed;
    // A column or row job in progress was writing the other orientation: finish it now, whole
    // (restarting it would delay it and the columns waiting behind it)
    bg_cj_pending = bg_rj_pending = 0;
    col_job_issued = row0_job_issued = 0;
    if (col_job_step < COL_JOB_STEPS) {
        col_job_slot = (uint8_t)(col_job_col & 15);
        if (view_rev) col_job_slot = (uint8_t)(-(int8_t)col_job_slot & 15);
        prepare_mt_column(col_job_col, level_map, level_map_bank, view_rev);
        flush_mt_column(col_job_slot);
        col_job_step = COL_JOB_STEPS;
    }
    if (row0_job_pos < 16) {
        flush_row_slots(0, BG_RJ_SLOTS, row_job_row, loaded_r, level_map, level_map_w, level_map_bank, view_rev);
        flush_row_slots(BG_RJ_SLOTS, BG_RJ_SLOTS, row_job_row, loaded_r, level_map, level_map_w, level_map_bank, view_rev);
        row0_job_pos = 16;
    }
    uint16_t scroll_px = view_rev
        ? (uint16_t)(-(int16_t)cam_px - MIRROR_PLAYER_SCREEN_X)
        : ((cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0);
    move_bkg((uint8_t)scroll_px, (uint8_t)cam_py);
    fade_reset_view();   // fading objects: the map moved, fade_update redraws them
    SHOW_BKG;
    SHOW_SPRITES;
    SPRITES_8x16;
    DISPLAY_ON;
}

static void practice_respawn(uint8_t idx) {
    uint8_t apply_idx;
    PracticeCheckpoint *cp = &practice_checkpoints[practice_cp_count - 1];

    cam_px = cp->cam_px;
    cam_py = cp->cam_py;
    cam_portal_y = cp->cam_portal_y;
    scroll_acc = cp->scroll_acc;
    bg_drift_px = cp->bg_drift_px;
    target_bg_idx = cp->target_bg_idx;
    last_cp_cam_px = cp->cam_px;

    player = cp->player;
    player.dead = 0;

    mt_band = band_for_cam(cam_py, BAND_MAX);
    band_cam_row = 0xFF;

    mirror_reload(idx, 0);

    uint16_t scroll_px = player.reversed
        ? (uint16_t)(-(int16_t)cam_px - MIRROR_PLAYER_SCREEN_X)
        : ((cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0);

    if (_cpu == CGB_TYPE) {
        uint8_t cur_bg_phase = player.reversed
            ? (uint8_t)(scroll_px + bg_drift_px) & 63u
            : (uint8_t)(scroll_px - bg_drift_px) & 63u;
        if (setting_show_bg_enabled) set_bg_parallax_phase(cur_bg_phase);
        else blank_parallax_vram();
        last_bg_phase = cur_bg_phase;
        load_menu_ground_tiles();
        flush_ground_row();
        famidash_bg_set_now(cp->bg_palettes);
        set_bkg_palette(0, 8, shadow_bkg_palettes);
        fade_set_sprite_palette(0, 8, gbc_sprite_palettes);
        apply_custom_player_palette();
    } else {
        apply_idx = target_bg_idx;
        if (reduce_flash && (apply_idx == 1 || apply_idx == 2)) apply_idx = 0;
        BGP_REG = bg_pals[apply_idx];
        OBP0_REG = (apply_idx == 3) ? DMG_OBP_ON_BLACK : BGP_REG;
        OBP1_REG = dmg_player_obp(apply_idx);
    }

    load_gameplay_sprite_tiles(LEVEL_DECO_CLOUD(idx));
    reapply_bg_shared_slots(idx, 0);   // DMG: saw tiles in sprite tiles it overwrote
    cube_icon_reset();
    load_checkpoint_tiles();
    debug_load_hud_tiles();
    percent_hud_load_tiles();

    sp_cache_reset(&active_sp, &sp_stream_idx);
    sp_cache_fill(l, cam_px, &sp_stream_idx);
    sp_cache_col = 0xFFFF;
    sp_fill_pending = 0;
    cached_collision_col = 0xFFFF;
    previous_oam_index = MAX_HARDWARE_SPRITES;

    percent_hud_reset(max_scroll_px);
    percent_hud_update(cam_px);
    attempt_text_start(_cpu == CGB_TYPE || player.mode != MODE_SHIP, scroll_px, cam_py);

    SHOW_BKG;
    SHOW_SPRITES;
    SPRITES_8x16;
    DISPLAY_ON;

    // Like a normal restart: a held jump acts right away (prev_joy only keeps B/SELECT
    // from placing or removing a checkpoint on the first frame)
    prev_joy = joypad();
    player.last_joy = 0;
    pause_suppress_jump = 0;
}

static void handle_death(uint8_t idx, uint8_t sprite_x_final, int16_t final_py, uint16_t scroll_px) {
    music_gate_off();   // the music runs freely until the loop starts again (practice: it keeps playing)
    record_level_progress_from_cam(level_save_slot[idx], cam_px, max_scroll_px, practice_mode);
    uint8_t removes = 0;
    if (setting_effects_enabled) {
        removes = play_death_animation(sprite_x_final, (uint8_t)final_py, (uint8_t)scroll_px, (uint8_t)cam_py, practice_mode);
    } else {
        if (setting_sfx_enabled) {
            NR41_REG = 0x00;
            NR42_REG = 0xF2;
            NR43_REG = 0x43;
            NR44_REG = 0x80;
        }
        for (uint8_t i = 0; i < 40; i++) shadow_OAM[i].y = 0;
        move_bkg((uint8_t)scroll_px, (uint8_t)cam_py);
        wait_vbl_done();
    }
    while (removes > 0) {
        practice_remove_checkpoint();
        removes--;
    }
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    if (practice_mode && practice_has_checkpoint()) {
        practice_respawn(idx);
    } else {
        reload_level_state(idx);
    }
}

static void update_camera_y(void) {
    uint16_t wy = PLAYER_WORLD_Y();
    uint16_t t;

    if (player.mode == MODE_CUBE) {
        // Cube: the dead zone camera with slight easing (softens the edge transitions)
        int16_t py = (int16_t)wy - (int16_t)cam_py;
        int16_t c;
        if (py < CAM_Y_TOP_ZONE) c = (int16_t)wy - CAM_Y_TOP_ZONE;
        else if (py > CAM_Y_BOTTOM_ZONE) c = (int16_t)wy - CAM_Y_BOTTOM_ZONE;
        else return;
        if (c < (int16_t)level_top_px) c = (int16_t)level_top_px;
        if ((uint16_t)c > cam_py_max) c = (int16_t)cam_py_max;

        uint16_t target = (uint16_t)c;
        if (target > cam_py) {
            uint16_t step = (target - cam_py) >> 1;
            if (!step) step = 1;
            else if (step > CAM_Y_MAX_STEP) step = CAM_Y_MAX_STEP;
            cam_py += step;
        } else if (target < cam_py) {
            uint16_t step = (cam_py - target) >> 1;
            if (!step) step = 1;
            else if (step > CAM_Y_MAX_STEP) step = CAM_Y_MAX_STEP;
            cam_py -= step;
        }
        if (cam_py > wy) cam_py = wy;
        if (cam_py + (CAM_VIEW_H - 16u) < wy) cam_py = wy - (CAM_VIEW_H - 16u);
        // the level's bounds last: a mini cube on the floor is 5 px lower than a cube, and the
        // rule above pushed the camera 5 px past the floor every time it landed (it bounced,
        // and the rows below the level showed)
        if (cam_py < level_top_px) cam_py = level_top_px;
        if (cam_py > cam_py_max) cam_py = cam_py_max;
        return;
    }

    // the UFO flies in the same corridors as the ship: the ship's camera
    uint8_t ship_cam = (player.mode == MODE_SHIP || player.mode == MODE_UFO);
    if (ship_cam && setting_old_ship_cam) {
        // Ship / UFO with SHIP CAM: OLD: the original dead zone camera that snaps directly
        int16_t py = (int16_t)wy - (int16_t)cam_py;
        int16_t c;
        if (py < CAM_Y_TOP_ZONE) c = (int16_t)wy - CAM_Y_TOP_ZONE;
        else if (py > CAM_Y_BOTTOM_ZONE) c = (int16_t)wy - CAM_Y_BOTTOM_ZONE;
        else return;
        if (c < (int16_t)level_top_px) c = (int16_t)level_top_px;
        if ((uint16_t)c > cam_py_max) c = (int16_t)cam_py_max;
        cam_py = (uint16_t)c;
        return;
    }

    if (ship_cam) {
        // the corridor: its centre on the portal's (48px tall), rounded down to the block grid
        uint16_t top = (cam_portal_y > 40u) ? ((cam_portal_y - 40u) & 0xFFF0u) : 0;
        uint16_t ground = level_map_h << 4;
        if (top + CAM_SHIP_H > ground) top = ground - CAM_SHIP_H;
        uint16_t d = (wy > top) ? (wy - top) : 0;   // 0 .. 144: the ship at the top .. bottom
        if (d > CAM_VIEW_H) d = CAM_VIEW_H;
        // top - margin .. top + 16 + margin: 32 px of sliding over 144 (d * 2 / 9)
        t = top + (uint8_t)((uint16_t)(d << 1) / 9u);
        t = (t > CAM_SHIP_MARGIN) ? (t - CAM_SHIP_MARGIN) : 0;
    } else {
        t = (cam_portal_y > CAM_VIEW_H / 2u - 24u) ? (cam_portal_y - (CAM_VIEW_H / 2u - 24u)) : 0;
    }
    // the player on screen whatever happens (outside the corridor, or the portal far away)
    if (t > wy) t = wy;
    if (t + (CAM_VIEW_H - 16u) < wy) t = wy - (CAM_VIEW_H - 16u);
    if (t < level_top_px) t = level_top_px;
    if (t > cam_py_max) t = cam_py_max;

    if (t > cam_py) {
        uint16_t step = (t - cam_py) >> 2;
        if (!step) step = 1;
        else if (step > CAM_Y_MAX_STEP) step = CAM_Y_MAX_STEP;
        cam_py += step;
    } else if (t < cam_py) {
        uint16_t step = (cam_py - t) >> 2;
        if (!step) step = 1;
        else if (step > CAM_Y_MAX_STEP) step = CAM_Y_MAX_STEP;
        cam_py -= step;
    }
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

    cam_px = 0;
    // CGB shows the ground strip (16px) below the map, DMG ends at the map's last row
    if (_cpu == CGB_TYPE) {
        cam_py = MAP_Y0 + 128u;
        cam_py_max = (level_map_h << 4) - 128u;
    } else {
        cam_py = MAP_Y0 + 112u;
        cam_py_max = (level_map_h << 4) - 144u;
    }
    // two steps: SDCC compiled (uint16_t)l->map_top << 4 with the high byte cleared (fine while
    // map_top < 16; a 27-row level in a 64-row map has map_top 37)
    level_top_px = l->map_top;
    level_top_px <<= 4;
    mt_band = band_for_cam(cam_py, BAND_MAX);
    loaded_r = BKG_MT_W - 1;
    stream_jobs_reset();
    broken_reset();
    max_scroll_px = ((level_map_w - VIEW_MT_W) << 4);

    reduce_flash = setting_dmg_gradient ? 0 : 1;
    target_bg_idx = dmg_start_bg_idx();
    level_max_fall_7 = game_levels[idx]->max_fall_7;
    player_init(&player, 0, 240);
    player.y_base = Y_BASE_MAX;

    DISPLAY_OFF;
    bg_map_reset();
    view_rev = 0;
    bg_level_set_flip_mode(idx);
    seamless = mt_cgb_flip;
#ifdef DEBUG_NO_SEAMLESS
    seamless = 0;   // test builds: CGB mirror portals through mirror_reload
#endif
    load_level_bg_sheet(idx, 0, level_tiles, level_tile_count, level_tiles_bank);
    apply_level_bg_tiles(idx, 0);
    if (!setting_show_bg_enabled) {
        set_bkg_data(12, 1, blank_bg_tile);
    }
    load_gameplay_sprite_tiles(LEVEL_DECO_CLOUD(idx));   // sprite_tiles.png
    reapply_bg_shared_slots(idx, 0);   // DMG: saw tiles in sprite tiles it overwrote
    cube_icon_reset();
    debug_load_hud_tiles();
    percent_hud_load_tiles();
    bg_drift_px = 0;
    if (_cpu == CGB_TYPE) {
        if (setting_show_bg_enabled) init_bg_parallax();
        else blank_parallax_vram();
        last_bg_phase = 0;
        load_menu_ground_tiles();
        famidash_reset_bg_palettes(idx);
        fade_set_sprite_palette(0, 8, gbc_sprite_palettes);
        apply_custom_player_palette();
    }
    move_bkg(0, (uint8_t)cam_py);
    fade_scx = fade_psx = 0;
    fill_scroll_bg(level_map, level_map_w, level_map_bank, 0);

    {
        uint8_t si = dmg_shown_bg_idx(target_bg_idx);
        uint8_t sobp0 = (si == 3) ? DMG_OBP_ON_BLACK : bg_pals[si];
        fade_set_dmg_palettes(bg_pals[si], sobp0, dmg_player_obp(si));
    }
    fade_set_black();
    // nothing left in OAM from the previous screen (the level complete's "100%" faded in with
    // the next attempt): the loop draws this level's sprites
    for (uint8_t i = 0; i < 40; i++) shadow_OAM[i].y = 0;

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

    if (setting_music_enabled) {
        // the Furnace song, else the hUGETracker one (Theory of Everything only has a Furnace song)
        uint8_t playing = music_play_level_fur(idx);
        if (!playing && level_songs[idx]) {
            init_music_banked(level_songs[idx], song_bank[idx], l->timer_divider);
            current_song_bank = song_bank[idx];
            playing = 1;
        }
        if (playing) {
            TAC_REG = 0x04;
            music_ready = 1;
        }
    }

    scroll_acc = 0;
    prev_joy = 0;
    previous_oam_index = MAX_HARDWARE_SPRITES;
    sp_stream_idx = 0;
    sp_cache_col = 0xFFFF;
    sp_fill_pending = 0;
    cached_collision_col = 0xFFFF;
    reduce_flash = setting_dmg_gradient ? 0 : 1;
    pause_suppress_jump = 0;
    end_anim_state = END_ANIM_INACTIVE;
    end_anim_frame = 0;
    end_shake_timer = 0;
    end_trigger_requested = 0;
    sp_cache_reset(&active_sp, &sp_stream_idx);
    practice_mode = 0;
    lc_pending = 0;
    practice_clear_checkpoints();
    coins_reset();
    coins_saved = saved_level_coins(level_save_slot[idx]);
    percent_hud_reset(max_scroll_px);
    attempt_count = 0;
    attempt_text_start(1, 0, cam_py);
    bg_parallax_isr_start();
    while (1) {
        PROF_MARK(1);   // input, scrolling, object cache
        // one frame of music time per iteration: a dropped frame holds the music back too
        if (music_gate) music_grant();
        else music_gate_on();
        mj_frame = bg_vbl_frames;
#ifdef DEBUG_PROFILE
        gpcamx = cam_px;
        gpcamy = cam_py;
        gpplayy = PLAYER_WORLD_Y();
        gpstate = (uint8_t)(player.mode | (player.mini << 4) | (player.gravity_flipped << 5) | (player.dead << 7));
#endif
        uint8_t joy = joypad();
        if (joy & J_UP) joy |= J_A;

        if (pause_suppress_jump) {
            if (!(joy & J_A)) {
                pause_suppress_jump = 0;
            } else {
                joy &= ~J_A;
            }
        }

        // Pause game on Start press
        if (!player.dead && end_anim_state == END_ANIM_INACTIVE && (joy & J_START) && !(prev_joy & J_START)) {
            if (pause_menu(idx)) break;
            continue;
        }

        if (player.level_complete) {
            level_complete_record(idx);
            break;
        }

        if (practice_mode) {
            if ((joy & J_B) && !(prev_joy & J_B)) {
                practice_add_checkpoint();
            }
            if (((joy & J_SELECT) && !(prev_joy & J_SELECT)) ||
                ((joy & J_DOWN) && !(prev_joy & J_DOWN))) {
                practice_remove_checkpoint();
            }

            if (setting_auto_checkpoints && !player.dead && end_anim_state == END_ANIM_INACTIVE && !player.level_complete) {
                if (cam_px > last_cp_cam_px + 140) {
                    if (player.mode == MODE_SHIP) {
                        practice_add_checkpoint();
                    } else if (player.on_ground) {
                        practice_add_checkpoint();
                    }
                }
            }
        } else {
            if ((joy & J_SELECT) && !(prev_joy & J_SELECT)) {
                reduce_flash = !reduce_flash;
                setting_dmg_gradient = !reduce_flash;
                save_game_data();
            }
        }
        prev_joy = joy;

        uint16_t px_prev = cam_px >> 4;
        uint8_t needs_render = 0;
        uint16_t need_col = 0;
        uint16_t px_curr = px_prev;

        if (end_anim_state == END_ANIM_INACTIVE && cam_px < max_scroll_px) {
            scroll_acc += SCROLL_SPEED_FP;
            cam_px += scroll_acc >> 8;
            // the parallax drifts against the scrolling: not while the camera is still (level
            // start: the player first runs to PLAYER_SCREEN_X)
            if (view_rev || cam_px > PLAYER_SCREEN_X) bg_drift_px++;
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
        // Object cache: retire behind the camera, then load ahead on the next frame
        // (never both in one frame; at level start / respawn both at once)
        if (sp_fill_pending) {
            sp_cache_fill(l, cam_px, &sp_stream_idx);
            sp_fill_pending = 0;
        } else if (sp_col != sp_cache_col) {
            sp_cache_retire(cam_px);
            if (sp_cache_col == 0xFFFF) sp_cache_fill(l, cam_px, &sp_stream_idx);
            else sp_fill_pending = 1;
            sp_cache_col = sp_col;
        }

        PROF_MARK(2);   // object logic
        // Buffer an airborne press for the orbs (Famidash check_for_cube_data_2_set, before
        // sprite_collide): set here, before the orbs run, an orb that takes this press clears it.
        // Set after them, a press on the frame an orb took (or one taken on the ground) stayed
        // buffered and fired the next orb while held.
        if ((joy & J_A) && !(player.last_joy & J_A) && player.vel_y.w != 0 &&
            player.mode != MODE_UFO && player.mode != MODE_SHIP) {
            player.orb_buffered = 1;
        }
        process_sprite_logic(&active_sp, cam_px, &player, joy, &target_bg_idx);
        PROF_MARK(3);   // collision columns, player physics

        if (end_trigger_requested && end_anim_state == END_ANIM_INACTIVE) start_end_anim();

        uint8_t mj_switched = 0;
        if (player.reversed != view_rev) {
            if (_cpu != CGB_TYPE) {
                dmg_mirror_due = 1;
            } else if (!seamless) {
                mirror_reload(idx, 1);
            } else if (mirror_job_ready(px_curr)) {
                mirror_switch(px_curr);
                needs_render = 0;
                mj_switched = 1;
            } else if (++mj_wait > MJ_MAX_WAIT) {
                mirror_reload(idx, 1);
            }
        }

        // Move the collision window when the player nears its top or bottom
        {
            uint8_t ly = player.world_y.b.h;
            if (ly < 48u && player.y_base > level_top_px) {
                uint8_t d = (player.y_base - level_top_px > 128u) ? 128u : (uint8_t)(player.y_base - level_top_px);
                player.y_base -= d;
                player.world_y.b.h = (uint8_t)(ly + d);
                cached_collision_col = 0xFFFF;
            } else if (ly >= 208u && player.y_base < Y_BASE_MAX) {
                uint8_t d = (Y_BASE_MAX - player.y_base > 128u) ? 128u : (uint8_t)(Y_BASE_MAX - player.y_base);
                player.y_base += d;
                player.world_y.b.h = (uint8_t)(ly - d);
                cached_collision_col = 0xFFFF;
            }
        }
        if (px_curr != cached_collision_col) {
            load_collision_columns(px_curr, level_map, level_map_w,
                                   level_map_bank, collision_columns, (uint8_t)(player.y_base >> 4));
            cached_collision_col = px_curr;
        }

        if (end_anim_state == END_ANIM_INACTIVE) {
            died = player_update(&player, joy, collision_columns, 16);
            if (broken_redraw != 0xFFFF) cached_collision_col = 0xFFFF;   // a block broke: reload the columns
#ifdef DEBUG_PROFILE
            if (died) gpdeaths++;
#endif
#if ENABLE_DEBUG_MODE
            if (debug_mode) { died = 0; player.dead = 0; } // noclip: hazards and walls can't kill
#ifdef DEBUG_GODMODE
            died = 0; player.dead = 0;
#endif
#endif
        } else {
            died = 0;
        }

        PROF_MARK(4);   // camera, end animation
        if (end_anim_state == END_ANIM_INACTIVE) {
            if (!died) update_camera_y();
        } else {
            cam_py = locked_cam_py;
        }

        uint16_t scroll_px;
        uint8_t sprite_x_final;
        int16_t final_py;

        if (end_anim_state == END_ANIM_INACTIVE) {
            if (view_rev) {
                // Mirror Mode: SCX decreases as progress advances
                scroll_px = (uint16_t)(-(int16_t)cam_px - MIRROR_PLAYER_SCREEN_X);
                sprite_x_final = MIRROR_PLAYER_SCREEN_X; // Mirrored player position (112)
            } else {
                scroll_px = (cam_px > PLAYER_SCREEN_X) ? (cam_px - PLAYER_SCREEN_X) : 0;
                sprite_x_final = (cam_px < PLAYER_SCREEN_X) ? (uint8_t)cam_px : PLAYER_SCREEN_X;
            }
            final_py = (int16_t)PLAYER_WORLD_Y() + PLAYER_DRAW_DY() - (int16_t)cam_py;
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
                if (view_rev) {
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
                // the shake moves the view down up to 2 px: ground below the ground rows too
                ground_below_fill((uint8_t)cam_py);
                play_sample_with_music(BANK_LEVEL_COMPLETE_SFX, level_complete_sfx_data, LEVEL_COMPLETE_SFX_LEN);
            }
        } else {
            // END_ANIM_SHAKE
            scroll_px = locked_scroll_px;
            sprite_x_final = 0;
            final_py = 0;
        }

        PROF_MARK(5);   // player sprite
        // Player sprite
        percent_hud_update(cam_px);
        uint8_t oam_index = PERCENT_HUD_OAM;   // slots 0..3: % display
        // CGB: slots before the player in OAM (drawn over it) for the front half of the portal
        // it is in, which draw_sprites fills, so the player passes through the ring. Not while
        // the level end shakes the level sprites (those are drawn with the shake offset).
        if (_cpu == CGB_TYPE && end_anim_state != END_ANIM_SHAKE && sp_front_want) {
            sp_front_slot = oam_index;
            oam_index += PORTAL_FRONT_SLOTS;
        }

        // Gravity portal / blue orb / mirror portal: the cube spins the other way from now on
        // (cube_turn). Count the step so the picture on screen stays exactly as it is: the step
        // becomes minus the step when the spin direction changes (half steps, 48 per turn).
        {
            uint8_t mir = CUBE_MIRROR();
            if (mir != cube_mirror) {
                uint8_t s0 = cube_step();
                uint8_t rev0 = (cube_mirror == 1u || cube_mirror == 2u), rev1 = (mir == 1u || mir == 2u);
                uint8_t s1 = (rev0 != rev1 && s0) ? (uint8_t)(48u - s0) : s0;
                player.anim_frame = (uint8_t)(s1 >> 1);   // the half step bit is kept (parity)
                // the ship's tilt is counted toward the rest surface: drawn flipped upside down
                // from now on, the same tilt on screen is the mirrored one
                if ((mir ^ cube_mirror) & 1u) ship_tilt = (uint8_t)(SHIP_FRAME_COUNT - 1u - ship_tilt);
                cube_mirror = mir;
            }
        }

        if (end_anim_state != END_ANIM_SHAKE && player.mini) {
            // Mini size: one 8x16 sprite, image in its top half, drawn at box top - 1
            // like Famidash (x .. x+7, y+3 .. y+10). Cube: 3 rotation images + mirror, in VRAM.
            // Ship, ball, UFO: their turned images (mini_frames.png) streamed like the big player's
            // into the buffer (its tiles are free while mini); until the first one is copied, the
            // level image in VRAM.
            static const uint8_t mini_cube_img[6] = { 0, 1, 1, 2, 1, 0 };
            // ship image (0 steep down .. 30 steep up) -> mini image (30, 15, 0, -15, -30 degrees)
            static const uint8_t mini_ship_img[SHIP_FRAME_COUNT] = {
                0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };
            uint8_t tile, prop = S_PALETTE;   // DMG: OBP1, the player's palette
            uint8_t mirrored = 1;             // drawn flipped with the gravity / mirror mode
            if (player.mode == MODE_CUBE) {
                uint8_t q = (uint8_t)(player.anim_frame % 6u);
                tile = MINI_PLAYER_TILE_BASE + (uint8_t)(mini_cube_img[q] << 1);
                if (q == 4) prop ^= S_FLIPX;
            } else {
                uint8_t first, n, want;
                if (player.mode == MODE_SHIP) {
                    ship_tilt_update();
                    first = MINI_SHIP_FIRST; n = MINI_SHIP_FRAMES;
                    want = (uint8_t)(PL_MINI + MINI_SHIP_FIRST + mini_ship_img[ship_tilt]);
                    tile = MINI_PLAYER_TILE_BASE + 6;
                } else if (player.mode == MODE_UFO) {
                    // like the big UFO: 15, 8 degrees nose down, level; nose up: those mirrored
                    ufo_tilt_update();
                    first = MINI_UFO_FIRST; n = MINI_UFO_FRAMES;
                    uint8_t d = (ufo_tilt > UFO_FRAME_LEVEL) ? (uint8_t)(ufo_tilt - UFO_FRAME_LEVEL)
                                                             : (uint8_t)(UFO_FRAME_LEVEL - ufo_tilt);
                    want = (uint8_t)(PL_MINI + MINI_UFO_FIRST + d);
                    tile = MINI_UFO_TILE;
                } else {
                    // like the big ball: never mirrored, it rolls the other way on the ceiling;
                    // 16 images a turn (a step of 22.5 degrees is 3 half steps)
                    mirrored = 0;
                    first = MINI_BALL_FIRST; n = MINI_BALL_FRAMES;
                    uint8_t k = (uint8_t)(cube_turn(ball_step()) / 3u);
                    if (k >= MINI_BALL_FRAMES) { k -= MINI_BALL_FRAMES; prop ^= S_FLIPX | S_FLIPY; }
                    want = (uint8_t)(PL_MINI + MINI_BALL_FIRST + k);
                    tile = MINI_PLAYER_TILE_BASE + 8;
                }
                uint8_t show = player_frame_show(want);
                uint8_t k = (uint8_t)(show - PL_MINI - first);
                if (show >= PL_MINI && k < n) {
                    tile = (uint8_t)(cube_front << 2);
                    if (player.mode == MODE_UFO && ufo_tilt > UFO_FRAME_LEVEL) prop ^= S_FLIPX;
                    // still the image from before a flip of the drawing (copied a frame late):
                    // undo that flip, like the big ship's
                    if (mirrored) {
                        uint8_t undo = (uint8_t)(cube_img_mir ^ cube_mirror);
                        if (undo & 1u) prop ^= S_FLIPY;
                        if (undo & 2u) prop ^= S_FLIPX;
                    }
                } else {
                    prop &= (uint8_t)~(S_FLIPX | S_FLIPY);   // the level image in VRAM
                }
            }
            if (mirrored) {
                if (player.gravity_flipped) prop ^= S_FLIPY;
                if (view_rev) prop ^= S_FLIPX;
            }
            uint8_t oy = (uint8_t)(final_py + 16 + 3);
            if (prop & S_FLIPY) oy -= 8;   // the image in the bottom half
            uint8_t ox = (uint8_t)(sprite_x_final + 8);
            if (view_rev) ox += 8;
            shadow_OAM[oam_index].y = oy;
            shadow_OAM[oam_index].x = ox;
            shadow_OAM[oam_index].tile = tile;
            shadow_OAM[oam_index].prop = prop;
            oam_index++;
        } else if (end_anim_state != END_ANIM_SHAKE) {
            if (player.mode == MODE_SHIP || player.mode == MODE_UFO) {
                // the ship, or Famidash's UFO (1 px above the cube's place, like Famidash's UFO_0):
                // tilted images, upside down when the gravity is flipped, mirrored in mirror mode
                uint8_t id;
                if (player.mode == MODE_SHIP) { ship_tilt_update(); id = (uint8_t)(PL_SHIP + ship_tilt); }
                else { ufo_tilt_update(); id = (uint8_t)(PL_UFO + ufo_tilt); }
                const metasprite_t *ship_ms = player_frame_metasprite(id);
                if (player.gravity_flipped) {
                    if (view_rev) oam_index += move_metasprite_hvflip(ship_ms, 0, oam_index, sprite_x_final + 24, final_py + 24);
                    else oam_index += move_metasprite_hflip(ship_ms, 0, oam_index, sprite_x_final + 8, final_py + 32);
                } else {
                    if (view_rev) oam_index += move_metasprite_vflip(ship_ms, 0, oam_index, sprite_x_final + 24, final_py + 16);
                    else oam_index += move_metasprite(ship_ms, 0, oam_index, sprite_x_final + 8, final_py + 16);
                }
            } else if (player.mode == MODE_BALL) {
                // like the cube: never mirrored, it rolls the other way on the ceiling
                const metasprite_t *ball = player_frame_metasprite(player_frame_id());
                oam_index += move_metasprite(ball, 0, oam_index, sprite_x_final + 8, final_py + 16);
            } else {
                // never mirrored (cube_turn); in mirror mode 2 px further right, where the mirrored
                // drawing had it (the image's 1 px left offset mirrored)
                const metasprite_t *cube = player_frame_metasprite(player_frame_id());
                oam_index += move_metasprite(cube, 0, oam_index, (uint8_t)(sprite_x_final + (view_rev ? 10 : 8)), final_py + 16);
            }
        }

        int8_t cur_shake_x = 0;
        int8_t cur_shake_y = 0;
        if (end_anim_state == END_ANIM_SHAKE) {
            if (end_shake_timer > 0) {
                end_shake_timer--;
                if (setting_effects_enabled) {
                    uint8_t r = DIV_REG;
                    cur_shake_x = (int8_t)((r % 5) - 2);
                    cur_shake_y = (int8_t)(((r >> 3) % 5) - 2);
                    if (cur_shake_x == 0 && cur_shake_y == 0) {
                        cur_shake_x = (r & 1) ? 1 : -1;
                    }
                }
            } else {
                player.level_complete = 1;
            }
        }

        PROF_MARK(6);   // level sprites
        // Level sprites: drawn as if the player were at PLAYER_SCREEN_X. At the level start the
        // screen stays put until the player gets there (scroll_px 0): the sprites too.
        uint16_t sp_cam_px = (!view_rev && cam_px < PLAYER_SCREEN_X) ? PLAYER_SCREEN_X : cam_px;
        oam_index = draw_sprites(
            &active_sp, (uint16_t)((int16_t)sp_cam_px + cur_shake_x), (uint16_t)((int16_t)cam_py + cur_shake_y),
            view_rev, oam_index
        );
        if (practice_mode) {
            oam_index = practice_draw_checkpoints(oam_index, sp_cam_px, cam_py, view_rev);
        }
        // DMG: the text's glyphs are in the ship's tiles (a ship portal while it is shown)
        if (_cpu != CGB_TYPE && player.mode == MODE_SHIP) attempt_text_hide();
        oam_index = attempt_text_draw(oam_index, scroll_px, cam_py);
        if (oam_index < previous_oam_index) {
            uint8_t *oam_ptr = (uint8_t *)&shadow_OAM[oam_index];
            while (oam_index < previous_oam_index) {
                *oam_ptr = 0;
                oam_ptr += 4;
                oam_index++;
            }
        }
        previous_oam_index = oam_index;
#if ENABLE_DEBUG_MODE
        if (debug_mode) debug_draw_hud(1, debug_ly, debug_max_ly);
#endif

        PROF_MARK(7);   // column job
        fade_scx = (uint8_t)scroll_px;   // fading objects: frames for this frame's view
        fade_psx = sprite_x_final;
        // The frame that switches maps issues no map job: it could reach the VBlank handler
        // before the switch does and land in the map going out.
        if (!mj_switched) {
        // VRAM uploads (DMG too): the VBlank handler writes them the moment VBlank starts. Done
        // by this thread after waking up, they could land outside VBlank when the music timer
        // interrupt ran first. A slice the handler has uploaded counts as done.
        if (col_job_issued && !bg_cj_pending) { col_job_issued = 0; col_job_step++; }
        if (needs_render) loaded_r = need_col;
        // One column at a time, in order: a column needed while the previous one is still being
        // written waits for it. Restarting the job left that column half written, and when the
        // last slice was still unacknowledged its acknowledgement skipped the new job's step 0
        // (the map fetch): the new ring column showed the previous column. At 2.8 px a frame a
        // column comes every 5-6 frames, so 1-2 slices put off by late VBlanks were enough.
        // A broken block's column (break_block) is rewritten before the next new one
        if (col_job_step >= COL_JOB_STEPS && fix_saved_col != 0xFFFF) {
            col_job_col = fix_saved_col;   // that rewrite is done: back to the new columns
            fix_saved_col = 0xFFFF;
        }
        if (col_job_step >= COL_JOB_STEPS && broken_redraw != 0xFFFF) {
            if (broken_redraw <= col_job_col && broken_redraw + 15u >= col_job_col) {   // in the ring
                fix_saved_col = col_job_col;
                col_job_col = broken_redraw;
                col_job_slot = (uint8_t)(col_job_col & 15);
                if (view_rev) col_job_slot = (uint8_t)(-(int8_t)col_job_slot & 15);
                col_job_step = 0;
            }
            broken_redraw = 0xFFFF;
        }
        else if (col_job_step >= COL_JOB_STEPS && col_job_col < loaded_r) {
            col_job_col++;
            col_job_slot = (uint8_t)(col_job_col & 15);
            if (view_rev) col_job_slot = (uint8_t)(-(int8_t)col_job_slot & 15);
            col_job_step = 0;
        }
        if (col_job_step < COL_JOB_STEPS && !col_job_issued) {
            prepare_mt_column_slice(col_job_col, level_map, level_map_bank, view_rev, col_job_step);
            request_mt_column_slice(col_job_slot, col_job_step);
            col_job_issued = 1;
        }
        }

        PROF_MARK(8);   // band, parallax, palettes, row job requests
        uint8_t parallax_needed = 0;
        uint8_t bg_phase = 0;

        // Vertical streaming: move the VRAM band one row towards the camera when the row job is
        // idle. The new row is BAND_MARGIN rows away from the screen, and a row takes 2 frames
        // (16px) while the camera moves at most ~8px per frame.
        // A row half the VBlank handler has uploaded counts as done: checked before the band so a
        // finished row lets the next one start this frame (2 frames a row, 8px a frame: the camera's
        // top speed; checked after it, a row took 3 frames and a long climb outran the band)
        if (row0_job_issued && !bg_rj_pending) { row0_job_issued = 0; row0_job_pos += ROW_JOB_PER_FRAME; }
        uint8_t cam_row = (uint8_t)(cam_py >> 4);
        if (row0_job_pos >= 16 && cam_row != band_cam_row) {
            uint8_t target_band = band_for_cam(cam_py, mt_band);
            if (target_band == mt_band) band_cam_row = cam_row;   // settled: skip until the camera row changes
            if (target_band != mt_band) {
                if (target_band > mt_band) {
                    row_job_row = (uint8_t)(mt_band + 16u);
                    mt_band++;
                } else {
                    mt_band--;
                    row_job_row = mt_band;
                }
                row0_job_pos = 0;
                row0_job_issued = 0;
                bg_rj_pending = 0;
                fade_band_moved(row_job_row);
                if (mj_active) {
                    // the other map needs the row too (two moves before it got the first: rebuild)
                    if (mj_row_pending != 0xFF) mj_next = 0;
                    mj_row_pending = row_job_row;
                }
                // a column slice already built is uploaded before the row job, which fixes it
                if (col_job_step < COL_JOB_STEPS) refetch_mt_column(level_map, level_map_bank);
            }
        }

        if (_cpu == CGB_TYPE) {
            if (setting_show_bg_enabled && setting_parallax_enabled) {
                bg_phase = view_rev
                    ? (uint8_t)(scroll_px + bg_drift_px) & 63u
                    : (uint8_t)(scroll_px - bg_drift_px) & 63u;
                if (bg_phase != last_bg_phase) {
                    last_bg_phase = bg_phase;
                    // Full 60Hz update. The GDMA MUST run inside VBlank (before
                    // it, tile data changes mid-scanout and tears the parallax
                    // into "broken puzzle" pieces), which the VBlank ISR ensures.
                    parallax_needed = 1;
                }
            }
        }

        uint8_t apply_idx = target_bg_idx;
        if (reduce_flash && (apply_idx == 1 || apply_idx == 2)) {
            apply_idx = 0;
        }

        uint8_t final_bgp = bg_pals[apply_idx];
        uint8_t final_obp0, final_obp1;

        // DMG: OBP0 follows the background palette, except on a black background (idx 3, every
        // shade -> black) where the player and objects keep their shades and only their black
        // outline turns white, so they stay visible. OBP1: the player (dmg_player_obp).
        final_obp0 = (_cpu != CGB_TYPE && apply_idx == 3) ? DMG_OBP_ON_BLACK : final_bgp;
        final_obp1 = dmg_player_obp(apply_idx);

        uint8_t final_scx = (uint8_t)((int16_t)scroll_px + cur_shake_x);
        uint8_t final_scy = (uint8_t)((int16_t)cam_py + cur_shake_y);

        // The parallax GDMA (768 bytes, ~1.9k dots) is executed by the VBlank
        // interrupt itself the instant VBlank starts (see bg_parallax_phases.c),
        // so it never depends on how late this thread wakes up.
        if (row0_job_pos < 16 && !row0_job_issued && !mj_switched) {
            request_row_slots(row0_job_pos, row_job_row, loaded_r, level_map, level_map_w, level_map_bank, view_rev);
            row0_job_issued = 1;
        }
        // fading objects whose frame changed with the scroll
        if (fade_count && !mj_switched) {
            fade_update(loaded_r, level_map, level_map_w, level_map_bank, view_rev,
                        (col_job_step < COL_JOB_STEPS) ? col_job_slot : 0xFFu);
        }
        if (bg_gdma_isr_on) {
            famidash_bg_fade_step();
            if (famidash_bkg_palettes_dirty) bg_pal_request = 1;
        }
        if (parallax_needed) request_bg_parallax(bg_phase);
        saw_anim_request();
        // Same for the scroll registers and the DMG palettes
        if (_cpu != CGB_TYPE) request_bg_dmg_pals(final_bgp, final_obp0, final_obp1);
        request_bg_scroll(final_scx, final_scy);
        if (seamless && !mj_switched && end_anim_state == END_ANIM_INACTIVE && !died) mirror_job(px_curr);
#if ENABLE_DEBUG_MODE
        if (debug_mode) {
            debug_ly = LY_REG;
            if (debug_ly > debug_max_ly) debug_max_ly = debug_ly;
        }
#endif
        PROF_MARK(9);   // waiting for VBlank
        bg_wait_vbl();
        PROF_MARK(10);  // after VBlank: DMG scroll / VRAM writes, saw animation
        if (dmg_mirror_due && !died) {
            dmg_mirror_due = 0;
            mirror_reflect(idx);
            PROF_MARK(10);
        }

        if (died) handle_death(idx, sprite_x_final, final_py, scroll_px);
    }

    music_gate_off();
    bg_parallax_isr_stop();
    if (lc_pending) {
        // Level complete: the level stays on screen (sprites included) and the song keeps
        // playing into STATE_LEVEL_COMPLETE until it finishes
        redraw = 1;
        return;
    } else {
        music_ready = 0;
        TAC_REG = 0x00;
        play_sample(BANK_SFX_DATA, quit_sound_data, QUIT_SOUND_LEN);
        fade_to_black(2);
        while (is_sample_playing()) wait_vbl_done();
        stop_sample();
        LCDC_REG &= (uint8_t)~LCDCF_BG9C00;   // a mirror portal may have left the level on the 0x9C00 map
    }

    HIDE_SPRITES;
    move_bkg(0, 0);
    waitpadup();
    disable_interrupts();
    setup_menu_font();
    enable_interrupts();
    redraw = 1;
}
