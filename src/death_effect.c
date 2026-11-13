#pragma bank 12

#include "death_effect.h"
#include "music.h"
#include <gb/gb.h>
#include <gb/cgb.h>
#include "settings.h"
#include "hUGEDriver.h"
#include "end_fx_discs.h"
#include "fade.h"

// Practice mode: the crash takes the noise channel from the music this long, then the
// music gets it back (cut before the crash fully decays so the drums come back cleanly)
#define DEATH_SFX_FRAMES 16

// Circle offsets for 24 frames of expansion [24][12][2]
static const int8_t circle_offsets[24][12][2] = {
    {{4, 0}, {3, 2}, {2, 3}, {0, 4}, {-2, 3}, {-3, 2}, {-4, 0}, {-3, -2}, {-2, -3}, {0, -4}, {2, -3}, {3, -2}},
    {{6, 0}, {5, 3}, {3, 5}, {0, 6}, {-3, 5}, {-5, 3}, {-6, 0}, {-5, -3}, {-3, -5}, {0, -6}, {3, -5}, {5, -3}},
    {{8, 0}, {7, 4}, {4, 7}, {0, 8}, {-4, 7}, {-7, 4}, {-8, 0}, {-7, -4}, {-4, -7}, {0, -8}, {4, -7}, {7, -4}},
    {{10, 0}, {9, 5}, {5, 9}, {0, 10}, {-5, 9}, {-9, 5}, {-10, 0}, {-9, -5}, {-5, -9}, {0, -10}, {5, -9}, {9, -5}},
    {{12, 0}, {11, 6}, {6, 11}, {0, 12}, {-6, 11}, {-11, 6}, {-12, 0}, {-11, -6}, {-6, -11}, {0, -12}, {6, -11}, {11, -6}},
    {{14, 0}, {12, 7}, {7, 12}, {0, 14}, {-7, 12}, {-12, 7}, {-14, 0}, {-12, -7}, {-7, -12}, {0, -14}, {7, -12}, {12, -7}},
    {{16, 0}, {14, 8}, {8, 14}, {0, 16}, {-8, 14}, {-14, 8}, {-16, 0}, {-14, -8}, {-8, -14}, {0, -16}, {8, -14}, {14, -8}},
    {{18, 0}, {16, 9}, {9, 16}, {0, 18}, {-9, 16}, {-16, 9}, {-18, 0}, {-16, -9}, {-9, -16}, {0, -18}, {9, -16}, {16, -9}},
    {{20, 0}, {17, 10}, {10, 17}, {0, 20}, {-10, 17}, {-17, 10}, {-20, 0}, {-17, -10}, {-10, -17}, {0, -20}, {10, -17}, {17, -10}},
    {{22, 0}, {19, 11}, {11, 19}, {0, 22}, {-11, 19}, {-19, 11}, {-22, 0}, {-19, -11}, {-11, -19}, {0, -22}, {11, -19}, {19, -11}},
    {{23, 0}, {20, 12}, {12, 20}, {0, 23}, {-12, 20}, {-20, 12}, {-23, 0}, {-20, -12}, {-12, -20}, {0, -23}, {12, -20}, {20, -12}},
    {{25, 0}, {21, 12}, {12, 21}, {0, 25}, {-12, 21}, {-21, 12}, {-25, 0}, {-21, -12}, {-12, -21}, {0, -25}, {12, -21}, {21, -12}},
    {{26, 0}, {23, 13}, {13, 23}, {0, 26}, {-13, 23}, {-23, 13}, {-26, 0}, {-23, -13}, {-13, -23}, {0, -26}, {13, -23}, {23, -13}},
    {{28, 0}, {24, 14}, {14, 24}, {0, 28}, {-14, 24}, {-24, 14}, {-28, 0}, {-24, -14}, {-14, -24}, {0, -28}, {14, -24}, {24, -14}},
    {{29, 0}, {25, 14}, {14, 25}, {0, 29}, {-14, 25}, {-25, 14}, {-29, 0}, {-25, -14}, {-14, -25}, {0, -29}, {14, -25}, {25, -14}},
    {{30, 0}, {26, 15}, {15, 26}, {0, 30}, {-15, 26}, {-26, 15}, {-30, 0}, {-26, -15}, {-15, -26}, {0, -30}, {15, -26}, {26, -15}},
    {{31, 0}, {27, 16}, {16, 27}, {0, 31}, {-16, 27}, {-27, 16}, {-31, 0}, {-27, -16}, {-16, -27}, {0, -31}, {16, -27}, {27, -16}},
    {{32, 0}, {28, 16}, {16, 28}, {0, 32}, {-16, 28}, {-28, 16}, {-32, 0}, {-28, -16}, {-16, -28}, {0, -32}, {16, -28}, {28, -16}},
    {{33, 0}, {29, 17}, {17, 29}, {0, 33}, {-17, 29}, {-29, 17}, {-33, 0}, {-29, -17}, {-17, -29}, {0, -33}, {17, -29}, {29, -17}},
    {{34, 0}, {29, 17}, {17, 29}, {0, 34}, {-17, 29}, {-29, 17}, {-34, 0}, {-29, -17}, {-17, -29}, {0, -34}, {17, -29}, {29, -17}},
    {{35, 0}, {30, 17}, {17, 30}, {0, 35}, {-17, 30}, {-30, 17}, {-35, 0}, {-30, -17}, {-17, -30}, {0, -35}, {17, -30}, {30, -17}},
    {{35, 0}, {31, 18}, {18, 31}, {0, 35}, {-18, 31}, {-31, 18}, {-35, 0}, {-31, -18}, {-18, -31}, {0, -35}, {18, -31}, {31, -18}},
    {{36, 0}, {31, 18}, {18, 31}, {0, 36}, {-18, 31}, {-31, 18}, {-36, 0}, {-31, -18}, {-18, -31}, {0, -36}, {18, -31}, {31, -18}},
    {{36, 0}, {31, 18}, {18, 31}, {0, 36}, {-18, 31}, {-31, 18}, {-36, 0}, {-31, -18}, {-18, -31}, {0, -36}, {18, -31}, {31, -18}},
};

// Particle trajectories for 12 particles over 36 frames [12][36][2]
static const int8_t particle_trajectories[12][36][2] = {
    {
        {0, 0}, {4, 1}, {7, 2}, {10, 3}, {13, 4}, {16, 4},
        {19, 5}, {21, 6}, {23, 6}, {25, 7}, {27, 7}, {29, 8},
        {30, 8}, {32, 9}, {33, 9}, {35, 9}, {36, 10}, {37, 10},
        {38, 10}, {39, 11}, {40, 11}, {41, 11}, {42, 11}, {43, 11},
        {43, 12}, {44, 12}, {44, 12}, {45, 12}, {46, 12}, {46, 12},
        {46, 12}, {47, 13}, {47, 13}, {48, 13}, {48, 13}, {48, 13},
    },
    {
        {0, 0}, {2, 2}, {3, 3}, {5, 5}, {6, 6}, {8, 8},
        {9, 9}, {10, 10}, {11, 11}, {12, 12}, {13, 13}, {14, 14},
        {15, 15}, {15, 15}, {16, 16}, {17, 17}, {17, 17}, {18, 18},
        {18, 18}, {19, 19}, {19, 19}, {20, 20}, {20, 20}, {20, 20},
        {21, 21}, {21, 21}, {21, 21}, {22, 22}, {22, 22}, {22, 22},
        {22, 22}, {23, 23}, {23, 23}, {23, 23}, {23, 23}, {23, 23},
    },
    {
        {0, 0}, {1, 4}, {2, 8}, {3, 11}, {4, 15}, {5, 18},
        {5, 20}, {6, 23}, {7, 26}, {7, 28}, {8, 30}, {9, 32},
        {9, 34}, {9, 35}, {10, 37}, {10, 38}, {11, 40}, {11, 41},
        {11, 42}, {12, 43}, {12, 44}, {12, 45}, {12, 46}, {13, 47},
        {13, 48}, {13, 49}, {13, 49}, {13, 50}, {13, 50}, {14, 51},
        {14, 51}, {14, 52}, {14, 52}, {14, 53}, {14, 53}, {14, 53},
    },
    {
        {0, 0}, {-1, 3}, {-1, 5}, {-2, 8}, {-3, 10}, {-3, 12},
        {-4, 14}, {-4, 15}, {-5, 17}, {-5, 19}, {-5, 20}, {-6, 21},
        {-6, 22}, {-6, 24}, {-7, 25}, {-7, 26}, {-7, 27}, {-7, 27},
        {-8, 28}, {-8, 29}, {-8, 30}, {-8, 30}, {-8, 31}, {-8, 31},
        {-9, 32}, {-9, 32}, {-9, 33}, {-9, 33}, {-9, 34}, {-9, 34},
        {-9, 34}, {-9, 35}, {-9, 35}, {-9, 35}, {-9, 35}, {-10, 36},
    },
    {
        {0, 0}, {-2, 2}, {-5, 5}, {-7, 7}, {-9, 9}, {-11, 11},
        {-12, 12}, {-14, 14}, {-16, 16}, {-17, 17}, {-18, 18}, {-19, 19},
        {-21, 21}, {-22, 22}, {-23, 23}, {-23, 23}, {-24, 24}, {-25, 25},
        {-26, 26}, {-26, 26}, {-27, 27}, {-28, 28}, {-28, 28}, {-29, 29},
        {-29, 29}, {-30, 30}, {-30, 30}, {-30, 30}, {-31, 31}, {-31, 31},
        {-31, 31}, {-32, 32}, {-32, 32}, {-32, 32}, {-32, 32}, {-33, 33},
    },
    {
        {0, 0}, {-2, 1}, {-4, 1}, {-6, 2}, {-8, 2}, {-9, 2},
        {-11, 3}, {-12, 3}, {-13, 4}, {-15, 4}, {-16, 4}, {-17, 4},
        {-18, 5}, {-19, 5}, {-19, 5}, {-20, 5}, {-21, 6}, {-22, 6},
        {-22, 6}, {-23, 6}, {-23, 6}, {-24, 6}, {-24, 6}, {-25, 7},
        {-25, 7}, {-25, 7}, {-26, 7}, {-26, 7}, {-26, 7}, {-27, 7},
        {-27, 7}, {-27, 7}, {-27, 7}, {-28, 7}, {-28, 7}, {-28, 7},
    },
    {
        {0, 0}, {-4, -1}, {-7, -2}, {-11, -3}, {-14, -4}, {-17, -5},
        {-19, -5}, {-22, -6}, {-24, -7}, {-26, -7}, {-28, -8}, {-30, -8},
        {-32, -9}, {-34, -9}, {-35, -9}, {-37, -10}, {-38, -10}, {-39, -10},
        {-40, -11}, {-41, -11}, {-42, -11}, {-43, -12}, {-44, -12}, {-45, -12},
        {-46, -12}, {-46, -12}, {-47, -13}, {-47, -13}, {-48, -13}, {-48, -13},
        {-49, -13}, {-49, -13}, {-50, -13}, {-50, -13}, {-51, -14}, {-51, -14},
    },
    {
        {0, 0}, {-2, -2}, {-4, -4}, {-5, -5}, {-7, -7}, {-8, -8},
        {-10, -10}, {-11, -11}, {-12, -12}, {-13, -13}, {-14, -14}, {-15, -15},
        {-16, -16}, {-17, -17}, {-17, -17}, {-18, -18}, {-19, -19}, {-19, -19},
        {-20, -20}, {-20, -20}, {-21, -21}, {-21, -21}, {-22, -22}, {-22, -22},
        {-22, -22}, {-23, -23}, {-23, -23}, {-23, -23}, {-24, -24}, {-24, -24},
        {-24, -24}, {-24, -24}, {-25, -25}, {-25, -25}, {-25, -25}, {-25, -25},
    },
    {
        {0, 0}, {-1, -3}, {-2, -7}, {-3, -10}, {-3, -13}, {-4, -15},
        {-5, -18}, {-5, -20}, {-6, -22}, {-6, -24}, {-7, -26}, {-7, -27},
        {-8, -29}, {-8, -30}, {-8, -32}, {-9, -33}, {-9, -34}, {-9, -35},
        {-10, -36}, {-10, -37}, {-10, -38}, {-10, -39}, {-11, -40}, {-11, -40},
        {-11, -41}, {-11, -42}, {-11, -42}, {-11, -43}, {-12, -43}, {-12, -44},
        {-12, -44}, {-12, -44}, {-12, -45}, {-12, -45}, {-12, -45}, {-12, -46},
    },
    {
        {0, 0}, {1, -2}, {1, -4}, {2, -6}, {2, -8}, {3, -10},
        {3, -12}, {4, -13}, {4, -15}, {4, -16}, {5, -17}, {5, -18},
        {5, -19}, {5, -20}, {6, -21}, {6, -22}, {6, -23}, {6, -23},
        {6, -24}, {7, -25}, {7, -25}, {7, -26}, {7, -26}, {7, -27},
        {7, -27}, {7, -28}, {8, -28}, {8, -28}, {8, -29}, {8, -29},
        {8, -29}, {8, -30}, {8, -30}, {8, -30}, {8, -30}, {8, -31},
    },
    {
        {0, 0}, {3, -3}, {6, -6}, {8, -8}, {10, -10}, {13, -13},
        {15, -15}, {16, -16}, {18, -18}, {20, -20}, {21, -21}, {23, -23},
        {24, -24}, {25, -25}, {26, -26}, {27, -27}, {28, -28}, {29, -29},
        {30, -30}, {31, -31}, {32, -32}, {32, -32}, {33, -33}, {34, -34},
        {34, -34}, {35, -35}, {35, -35}, {36, -36}, {36, -36}, {36, -36},
        {37, -37}, {37, -37}, {37, -37}, {38, -38}, {38, -38}, {38, -38},
    },
    {
        {0, 0}, {3, -1}, {5, -1}, {7, -2}, {9, -2}, {11, -3},
        {13, -3}, {14, -4}, {16, -4}, {17, -5}, {19, -5}, {20, -5},
        {21, -6}, {22, -6}, {23, -6}, {24, -6}, {25, -7}, {25, -7},
        {26, -7}, {27, -7}, {27, -7}, {28, -8}, {29, -8}, {29, -8},
        {30, -8}, {30, -8}, {30, -8}, {31, -8}, {31, -8}, {32, -8},
        {32, -9}, {32, -9}, {32, -9}, {33, -9}, {33, -9}, {33, -9},
    },
};

uint8_t play_death_animation(uint8_t screen_x, uint8_t screen_y, uint8_t scroll_px, uint8_t cam_py, uint8_t keep_music) BANKED {
    // cut the music (not in practice mode), crash noise on
    if (!keep_music) {
        TAC_REG = 0x00;
        NR52_REG = 0x00;
        NR52_REG = 0x80;
        NR51_REG = 0xFF;
        NR50_REG = 0x77;
    }

    // hUGE stops writing the noise channel so it can't cut the crash off
    uint8_t noise_borrowed = keep_music && setting_sfx_enabled && setting_music_enabled;
    if (noise_borrowed) music_borrow(8);

    if (setting_sfx_enabled) {
        NR41_REG = 0x00;
        NR42_REG = 0xF2;
        NR43_REG = 0x43;
        NR44_REG = 0x80;
    }

    uint8_t prev_pad = joypad();
    uint8_t removes = 0;

    // Center of death in hardware OAM coordinates (screen + 8x, + 16y)
    int16_t center_x = (int16_t)screen_x + 8 + 8;
    int16_t center_y = (int16_t)screen_y + 8 + 16;

    // Screen shake offsets for first 6 frames
    static const int8_t shake_x[6] = {2, -2, 1, -1, 1, 0};
    static const int8_t shake_y[6] = {1, -1, -1, 1, 0, 0};

    for (uint8_t frame = 0; frame < 38; frame++) {
        if (keep_music) {
            uint8_t pad = joypad();
            if (((pad & J_SELECT) && !(prev_pad & J_SELECT)) ||
                ((pad & J_DOWN) && !(prev_pad & J_DOWN))) {
                removes++;
            }
            prev_pad = pad;
        }

        if (noise_borrowed && frame == DEATH_SFX_FRAMES) {
            NR42_REG = 0x00;   // DAC off: silences the crash
            NR44_REG = 0x80;
            music_give_back(8);
            noise_borrowed = 0;
        }

        if (setting_effects_enabled && frame < 6) {
            int16_t sy = (int16_t)cam_py + shake_y[frame];
            if (sy < 0) sy = 0;
            move_bkg((uint8_t)(scroll_px + shake_x[frame]), (uint8_t)sy);
        } else if (frame == 6) {
            move_bkg((uint8_t)scroll_px, (uint8_t)cam_py);
        }

        uint8_t oam_idx = 0;

        // expanding circle, frames 0..23
        if (frame < 24) {
            uint8_t circle_tile = DEATH_TILE_CIRCLE_P1;
            if (frame >= 16) {
                circle_tile = DEATH_TILE_CIRCLE_P3; // 25% sparse stipple
            } else if (frame >= 8) {
                circle_tile = DEATH_TILE_CIRCLE_P2; // 50% checkerboard dither
            }

            for (uint8_t i = 0; i < 12; i++) {
                int16_t px = center_x + circle_offsets[frame][i][0];
                int16_t py = center_y + circle_offsets[frame][i][1];

                if (px > 0 && px < 168 && py > 8 && py < 168) {
                    shadow_OAM[oam_idx].x = (uint8_t)px;
                    shadow_OAM[oam_idx].y = (uint8_t)py;
                    shadow_OAM[oam_idx].tile = circle_tile;
                    shadow_OAM[oam_idx].prop = 0; // Player palette (Cyan)
                    oam_idx++;
                }
            }
        }

        // particles, frames 0..35
        if (frame < 36) {
            for (uint8_t i = 0; i < 12; i++) {
                int16_t px = center_x + particle_trajectories[i][frame][0];
                int16_t py = center_y + particle_trajectories[i][frame][1];

                if (px > 0 && px < 168 && py > 8 && py < 168) {
                    uint8_t part_tile;
                    if (frame >= 24) {
                        part_tile = DEATH_TILE_PART_TINY; // 1x1 dot
                    } else if (frame >= 13) {
                        part_tile = (i & 1) ? DEATH_TILE_PART_SMALL_GREEN : DEATH_TILE_PART_SMALL_CYAN;
                    } else {
                        part_tile = (i & 1) ? DEATH_TILE_PART_GREEN : DEATH_TILE_PART_CYAN;
                    }

                    shadow_OAM[oam_idx].x = (uint8_t)px;
                    shadow_OAM[oam_idx].y = (uint8_t)py;
                    shadow_OAM[oam_idx].tile = part_tile;
                    shadow_OAM[oam_idx].prop = 0;
                    oam_idx++;
                }
            }
        }

        // y = 0 hides a sprite
        while (oam_idx < 40) {
            shadow_OAM[oam_idx].y = 0;
            oam_idx++;
        }

        wait_vbl_done();
    }

    for (uint8_t i = 0; i < 40; i++) {
        shadow_OAM[i].y = 0;
    }
    move_bkg((uint8_t)scroll_px, (uint8_t)cam_py);
    wait_vbl_done();
    return removes;
}

// Level complete: a disc (END_FX_SIZES sizes, 3 frames each) at a random spot, the next starting
// just before it is gone (two at once then, one above the other); the particles of the death effect burst from the disc's centre as it starts. Once the text is
// in, a splash of stars flies a little way out of it, slowing down, twinkling, gone in half a second. Every tile is loaded once (end_fx_init): nothing is uploaded per frame,
// and the per-frame code stays on 8-bit maths and pointer walks (it ran 50..120 lines a frame on DMG
// with int16 maths, modulos, 2D indexing and a 256-byte upload every 3 frames: dropped frames).
#define END_FX_STEP_FRAMES 3
#define END_FX_PART_FRAMES 36     // frames per particle in particle_trajectories
#define END_FX_CYCLE_FRAMES 28    // a disc's frames (its burst's are cut there: tiny dots by then)
#define END_FX_NEXT_AT 18         // the next disc starts at the newest one's frame 18..21 (of 24 drawn)
#define FX_TEXT_X 88              // OAM x, y of the text's centre: the stars fly away from it
#define FX_TEXT_Y 50
#define END_FX_PAL 6            // CGB sprite palette of the discs and stars (free on this screen)
#define FX_SPARKS 12

// two discs: the newest (fx_new) and the one before it, finishing
typedef struct { uint8_t frame, size, step, cx, cy; } fx_disc_t;   // frame 0xFF: idle
static fx_disc_t discs[2];
static uint8_t fx_new;
static uint8_t fx_next_at;
static uint8_t fx_rng;
static uint8_t fx_prop;     // the discs' and stars' sprite attributes (CGB: END_FX_PAL)
static uint8_t fx_used;     // sprites in use after the last frame (the rest are hidden already)

static uint8_t fx_rand(void) {
    fx_rng = (uint8_t)(fx_rng * 5u + 17u + DIV_REG);
    return fx_rng;
}

// the stars of the splash: a life counts down, 0 = gone
typedef struct { uint8_t x, y, life; int8_t vx, vy; } fx_spark_t;
static fx_spark_t sparks[FX_SPARKS];

// twinkle: dot, small star, big star, small star
static const uint8_t spark_tiles[4] = {
    DEATH_TILE_PART_TINY,
    END_FX_TILE_BASE + END_FX_STAR_SMALL,
    END_FX_TILE_BASE + END_FX_STAR_BIG,
    END_FX_TILE_BASE + END_FX_STAR_SMALL,
};

void end_fx_init(void) BANKED {
    fx_prop = 0;
    if (_cpu == CGB_TYPE) {
        palette_color_t pal[4];
        pal[0] = RGB8(0, 0, 0);
        pal[1] = RGB8(255, 255, 255);     // disc, star arms
        pal[2] = RGB8(255, 240, 140);     // star centre
        pal[3] = RGB8(170, 236, 255);
        wait_vbl_done();                  // palette writes are lost while the LCD reads them
        fade_set_sprite_palette(END_FX_PAL, 1, pal);
        fx_prop = END_FX_PAL;
    }
    set_sprite_data(END_FX_TILE_BASE, END_FX_TILE_COUNT, end_fx_tiles);
    fx_spark_t *s = sparks;
    for (uint8_t i = 0; i < FX_SPARKS; i++, s++) s->life = 0;
    discs[0].frame = discs[1].frame = 0xFF;
    discs[0].cy = 68;
    fx_new = 0;
    fx_next_at = 0;
    fx_used = 40;
    fx_rng = DIV_REG;
}

void end_fx_splash(void) BANKED {
    fx_spark_t *s = sparks;
    for (uint8_t i = 0; i < FX_SPARKS; i++, s++) {
        // spread along the text (OAM x 24..151, y 36..67), flying away from its centre: sideways
        // unless near the middle, up or down unless near the ends
        uint8_t x = (uint8_t)(24u + i * 10u + (fx_rand() & 7u));
        uint8_t y = (uint8_t)(36u + (fx_rand() & 31u));
        s->x = x;
        s->y = y;
        int8_t dx = (int8_t)(x - FX_TEXT_X);
        s->vx = (dx > 12) ? 1 : ((dx < -12) ? -1 : 0);
        s->vy = (dx > 52 || dx < -52) ? 0 : ((y < FX_TEXT_Y) ? -1 : 1);
        if (!s->vx && !s->vy) s->vy = -1;
        s->life = (uint8_t)(26u + (fx_rand() & 7u));    // about half a second
    }
}

// Draws a disc and its burst from o on, as far as the 40 sprites go; advances it a frame
static OAM_item_t *draw_disc(fx_disc_t *d, OAM_item_t *o, uint8_t prop) {
    uint8_t i;
    if (d->frame == 0xFF) return o;
    // (o's low byte is 4 x its sprite index: shadow_OAM is page aligned)
    if (d->size < END_FX_SIZES && (uint8_t)(uint16_t)o <= (uint8_t)((40u - 8u) * 4u)) {
        // 4 x 2 sprites of one quarter, mirrored: columns 0 1 | 1' 0', the lower row flipped; the
        // mirrored ones 1 px left / up, sharing the centre column and row
        uint8_t t0 = (uint8_t)(END_FX_TILE_BASE + (d->size << 2)), t1 = (uint8_t)(t0 + 2u);
        uint8_t dx = (uint8_t)(d->cx - 8u), dy = d->cy, p = prop;
        for (i = 0; i < 2; i++) {
            o->y = dy; o->x = dx;                  o->tile = t0; o->prop = p;                   o++;
            o->y = dy; o->x = (uint8_t)(dx + 8u);  o->tile = t1; o->prop = p;                   o++;
            o->y = dy; o->x = (uint8_t)(dx + 15u); o->tile = t1; o->prop = (uint8_t)(p | S_FLIPX); o++;
            o->y = dy; o->x = (uint8_t)(dx + 23u); o->tile = t0; o->prop = (uint8_t)(p | S_FLIPX); o++;
            dy += 15u;
            p |= S_FLIPY;
        }
    }
    {
        // the burst, from the centre (OAM x, y). No clipping needed: x 32..159 +- 48 stays under 256
        // (below 0 wraps to 240.., off the screen) and y 84..147 + 48 does too
        uint8_t f = d->frame, ta, tb;
        if (f >= 24u) ta = tb = DEATH_TILE_PART_TINY;
        else if (f >= 13u) { ta = DEATH_TILE_PART_SMALL_CYAN; tb = DEATH_TILE_PART_SMALL_GREEN; }
        else { ta = DEATH_TILE_PART_CYAN; tb = DEATH_TILE_PART_GREEN; }
        const int8_t *p = &particle_trajectories[0][f][0];
        uint8_t cx = (uint8_t)(d->cx + 8u), cy = (uint8_t)(d->cy + 16u);
        for (i = 0; i < 6; i++) {
            if ((uint8_t)(uint16_t)o > (uint8_t)((40u - 2u) * 4u)) break;
            o->y = (uint8_t)(cy + p[1]); o->x = (uint8_t)(cx + p[0]); o->tile = ta; o->prop = 0; o++;
            p += END_FX_PART_FRAMES * 2;
            o->y = (uint8_t)(cy + p[1]); o->x = (uint8_t)(cx + p[0]); o->tile = tb; o->prop = 0; o++;
            p += END_FX_PART_FRAMES * 2;
        }
    }
    if (++d->step == END_FX_STEP_FRAMES) {
        d->step = 0;
        d->size++;
    }
    if (++d->frame >= END_FX_CYCLE_FRAMES) d->frame = 0xFF;
    return o;
}

uint8_t end_fx_draw(uint8_t oam_idx) BANKED {
    OAM_item_t *o = &shadow_OAM[oam_idx];
    uint8_t i, l, r, prop = fx_prop;

    fx_spark_t *s = sparks;
    for (i = 0; i < FX_SPARKS; i++, s++) {
        l = s->life;
        if (!l) continue;
        s->life = --l;
        // slowing down: every frame, then every 2nd, then every 4th (about 12 px in all)
        if (l >= 22u || (l >= 12u ? !(l & 1u) : !(l & 3u))) {
            s->x = (uint8_t)(s->x + s->vx);
            s->y = (uint8_t)(s->y + s->vy);
        }
        if (l > 6u || (l & 1u)) {   // blinks out at the end
            o->y = s->y;
            o->x = s->x;
            o->tile = spark_tiles[((uint8_t)(l + (i << 1)) >> 2) & 3u];
            o->prop = prop;
            o++;
        }
    }

    {
        fx_disc_t *n = &discs[fx_new];
        if (n->frame == 0xFF || n->frame >= fx_next_at) {
            // the next disc, in the other slot (idle: the older one is long done), fully above or
            // below the one finishing (OAM y 68..131), so the two never share lines (10 sprites a line)
            uint8_t y = n->cy;
            if (y < 100u) {
                y = (uint8_t)(y + 31u + (fx_rand() & 15u));
                if (y > 131u) y = 131u;
            } else {
                y = (uint8_t)(y - 31u - (fx_rand() & 15u));
                if (y < 68u) y = 68u;
            }
            fx_new ^= 1u;
            n = &discs[fx_new];
            n->cx = (uint8_t)(24u + (fx_rand() & 127u));
            n->cy = y;
            n->frame = n->size = n->step = 0;
            fx_next_at = (uint8_t)(END_FX_NEXT_AT + (fx_rand() & 3u));
        }
        // the newest first: it keeps all its sprites when the stars take some
        o = draw_disc(n, o, prop);
        o = draw_disc(&discs[fx_new ^ 1u], o, prop);
    }

    // shadow_OAM is page aligned: the index is the address's low byte / 4
    l = (uint8_t)((uint8_t)(uint16_t)o >> 2);
    r = l;
    while (r < fx_used) {
        o->y = 0;
        o++;
        r++;
    }
    fx_used = l;
    return l;
}
