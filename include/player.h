#ifndef PLAYER_H
#define PLAYER_H

#include <gb/gb.h>
#include <stdint.h>
#include "collision.h"

#define PLAYER_SCREEN_X          32
#define MIRROR_PLAYER_SCREEN_X   112   // 160 - PLAYER_SCREEN_X - 16 (32px from right edge)
#define PLAYER_SIZE       15    // Box size for wall/floor collision
#define PLAYER_HBOX       6     // Inset for spike/hazard collision

// Physics constants using 8.8 fixed-point (256 units = 1 pixel)
// The Game Boy refresh is ~59.7fps (not 60), so every per-frame value here is
// the Famidash 60fps value rescaled by x714/708 (same rate correction as
// SCROLL_SPEED_FP, ~59.5fps effective) to keep per-second motion accurate.
// Base 60fps Famidash value is shown in comments.
#define GRAVITY           108   // Famidash 0x006B (107)
#define JUMP_FORCE       -1436  // Famidash 0xFA70 (-1424)
#define MAGENTA_JUMP_FORCE -984  // Famidash 0xFC30 (-976)
#define PAD_JUMP_FORCE   -2001  // Famidash 0xF840 (-1984)
#define PINK_PAD_FORCE   -1307  // Famidash 0xFAF0 (-1296)
#define BLUE_PAD_FORCE     936   // Famidash 0x03A0 (928)
#define BLUE_ORB_FORCE     420   // Famidash 0x01A0 (416) - ONLY for Ball!
#define MAX_FALL_SPEED   1549  // Famidash 0x0600 (1536)

// Ball specific orb/pad forces
#define BALL_YELLOW_ORB  -1049  // (Famidash -1040)
#define BALL_PINK_ORB     -823  // (Famidash -816)
#define BALL_YELLOW_PAD  -1275  // (Famidash -1264)
#define BALL_PINK_PAD     -871  // (Famidash -864)

#define MODE_CUBE         0
#define MODE_SHIP         1
#define MODE_BALL         2
#define MODE_UFO          3

#define SHIP_THRUST       -42   // Famidash SHIP_GRAVITY_BASE 0x002A - holding, rising
#define SHIP_GRAVITY       34   // Famidash SHIP_GRAVITY 0x0022 - release, falling
#define SHIP_GRAVITY_AFTER_HOLD 50 // Famidash 0x0032 - release, rising
#define SHIP_GRAVITY_HOLD_FALL  52 // Famidash 0x0034 - holding, falling
#define SHIP_MAX_VEL_UP    880  // Famidash 0x0369 (873) x 714/708
#define SHIP_MAX_VEL_DOWN  1100 // Famidash 0x0443 (1091) x 714/708

#define BALL_GRAVITY      72    // Famidash 0x0047 (71)

// UFO (Famidash gamemode_ufo.h, 60fps tables): gravity, max fall speed; a press (on the ground or
// in the air) sets the jump speed, unless an orb took the press that frame
#define UFO_GRAVITY        50   // Famidash UFO_GRAVITY 0x0032 (50)
#define UFO_MAX_FALL      807   // Famidash UFO_MAX_FALLSPEED 0x0320 (800)
#define UFO_JUMP_VEL     -823   // Famidash UFO_JUMP_VEL 0xFCD0 (-816)
#define MINI_UFO_MAX_FALL 855   // 0x0350 (848); mini gravity is the same
#define MINI_UFO_JUMP_VEL -726  // 0xFD30 (-720)
#define BALL_SWITCH_VEL   516   // Famidash 0x0200 (512)

// Mini size (Famidash mini portal). Same 714/708 rate correction as above;
// Famidash 60fps value in comments.
#define MINI_GRAVITY          112   // CUBE_GRAVITY mini 1560.94 px/s^2 (111)
#define MINI_JUMP_FORCE     -1242   // JUMP_VEL mini -288.75 px/s (-1232)
#define MINI_BALL_GRAVITY      88   // BALL_GRAVITY mini 1223.44 px/s^2 (87)
#define MINI_BALL_SWITCH_VEL  290   // BALL_SWITCH_VEL mini 67.5 px/s (288)
#define MINI_BALL_MAX_FALL   1291   // BALL_MAX_FALLSPEED mini 300 px/s (1280)
#define MINI_SHIP_THRUST      -49   // SHIP_GRAVITY_BASE mini 693.55 (49)
#define MINI_SHIP_GRAVITY      39   // SHIP_GRAVITY mini 554.84 (39)
#define MINI_SHIP_GRAVITY_AFTER_HOLD 59 // 832.26 (59)
#define MINI_SHIP_GRAVITY_HOLD_FALL  62 // 866.93 (62)

// Player collision box (offsets from world_x / world_y). Normal: 16x16.
// Mini: Famidash CUBE_WIDTH/HEIGHT mini = 8x7, vertically centred ((16-7)/2 = 4).
#define MINI_BOX_TOP     4
#define MINI_BOX_BOTTOM 11   // first pixel below the box
#define MINI_BOX_RIGHT   8   // Famidash checks x, x+4, x+8

#define MAX_ACTIVATIONS 8

typedef union {
    uint16_t w;
    struct {
        uint8_t l;
        uint8_t h;
    } b;
} fixed16_t;

typedef union {
    int16_t w;
    struct {
        uint8_t l;
        int8_t h;
    } b;
} sfixed16_t;

typedef struct {
    uint16_t mx;
    uint8_t my;
} ActivatedTile;

typedef struct Player {
    uint16_t world_x;
    fixed16_t world_y; // 8.8 fixed point
    sfixed16_t vel_y;   // 8.8 fixed point
    uint8_t  on_ground;
    uint8_t  dead;
    uint8_t  gravity_flipped;
    uint8_t  mode;
    uint8_t  reversed;
    uint8_t  anim_frame;
    uint16_t anim_timer;
    uint8_t  last_joy;
    uint8_t  ball_switched;
    uint8_t  orb_buffered;
    uint8_t  touching_orb;
    uint8_t  level_complete;
    uint8_t  mini;         // 1 = mini size (mini portal)
    uint16_t level_end_x;
    uint16_t sp_idx;
    uint16_t y_base;       // world y of the collision window (world_y is relative to it)
    uint8_t  ufo_orbed;    // an orb took this frame's press (the UFO does not also jump)
} Player;

// Reset player state to starting position
void player_init(Player* p, uint16_t start_x, int16_t start_y) BANKED;

uint8_t player_update(
    Player* p,
    uint8_t joy,
    const uint8_t* collision_columns,
    uint16_t map_h
) BANKED;


#endif // PLAYER_H
