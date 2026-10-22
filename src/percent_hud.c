#pragma bank 11

#include <gb/gb.h>
#include <gb/cgb.h>
#include "percent_hud.h"
#include "settings.h"
#include "debug_mode.h"
#include "pause_buttons.h"

extern const unsigned char FontPusab[];

// Sprite tiles: sprites are 8x16, and there is no free tile space left in gameplay.
// The debug HUD's glyph pairs (sprite tiles 90..115) only use their upper tile, so
// the % digits live in the lower (odd) tiles: pair 90 + 2k, lower tile = digit k,
// k = 10 is '%'. The % sprites sit at the top edge (OAM y 8) so only their lower
// tile is on screen (rows 0..7); the debug HUD sits at the bottom edge and shows
// only its upper tiles.
#define HUD_PAIR_BASE   90
#define GLYPH_PERCENT   10
#define FONT_DIGIT0     3
#define FONT_PERCENT    1
#define HUD_Y           8
#define GLYPH_W         8

static uint8_t pct;
static uint8_t shown = 0xFF;     // value currently in OAM (0xFF: redraw)
// Next percent threshold = ceil(p * max / 100), kept exactly with integer steps:
// q + rem/100 = p * max / 100
static uint16_t thr, q, step_q;
static uint8_t rem, step_r;

// "ATTEMPT N" (like Geometry Dash): text in the level at the start position, scrolling
// away with it. Glyph pairs (letter / digit on top, blank below) over the pause menu's Play /
// Menu / Restart button tiles 36..65, which are only on screen in the pause menu: it reloads
// them (init_pause_tiles) and attempt_text_load_tiles puts the glyphs back on resume.
#define ATT_TILE_BASE   36
#define ATT_A 0
#define ATT_T 1
#define ATT_E 2
#define ATT_M 3
#define ATT_P 4
#define ATT_DIGIT0 5
#define ATT_GLYPHS 15
#define ATT_SCREEN_Y 40     // screen y of the text when the attempt starts
#define ATT_MAX_CHARS 12
static const uint8_t att_font[ATT_GLYPHS] = {
    13 + 0, 13 + 19, 13 + 4, 13 + 12, 13 + 15,   // A T E M P (FontPusab: 'A' = glyph 13)
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12               // 0-9
};
static const uint8_t att_word[7] = { ATT_A, ATT_T, ATT_T, ATT_E, ATT_M, ATT_P, ATT_T };

uint16_t attempt_count;
static uint8_t att_glyph[ATT_MAX_CHARS];   // 0xFF: space
static uint8_t att_n;
static uint8_t att_on;
static uint16_t att_s0;      // screen scroll (SCX, 16 bit) at which the text is at its start position
static uint16_t att_y0;      // camera y at the attempt start
static uint8_t att_sx0;      // OAM x of the first character at att_x0

void attempt_text_load_tiles(void) BANKED {
    static const uint8_t blank[16] = {0};
    // DMG: the glyphs overwrite the ship frames (tiles 36..63), so only while the text is on
    // (attempt_text_hide / attempt_text_draw put the ship back when it is off)
    if (_cpu != CGB_TYPE && !att_on) return;
    // CGB: VRAM bank 1 (free there), so the ship frames in bank 0 stay intact
    if (_cpu == CGB_TYPE) VBK_REG = 1;
    for (uint8_t k = 0; k < ATT_GLYPHS; k++) {
        uint8_t t = (uint8_t)(ATT_TILE_BASE + (k << 1));
        set_sprite_data(t, 1, &FontPusab[att_font[k] * 16]);
        set_sprite_data((uint8_t)(t + 1), 1, blank);
    }
    if (_cpu == CGB_TYPE) VBK_REG = 0;
}

// An attempt starts (level start or practice checkpoint; screen scroll scroll_x, camera cam_y):
// count it and show "ATTEMPT N" there. show = 0: counted, no text (DMG ship: the glyphs use its
// tiles). The text moves with the screen scroll, not the camera: at the level start the player
// runs to PLAYER_SCREEN_X before the screen scrolls.
void attempt_text_start(uint8_t show, uint16_t scroll_x, uint16_t cam_y) BANKED {
    if (attempt_count < 9999u) attempt_count++;
    if (!show) { attempt_text_hide(); return; }
    uint8_t n = 0;
    for (; n < 7; n++) att_glyph[n] = att_word[n];
    att_glyph[n++] = 0xFF;
    uint16_t v = attempt_count;
    uint8_t d[4], nd = 0;
    do { d[nd++] = (uint8_t)(v % 10u); v /= 10u; } while (v);
    while (nd) att_glyph[n++] = (uint8_t)(ATT_DIGIT0 + d[--nd]);
    att_n = n;
    att_sx0 = (uint8_t)(96 + 8 - ((n * GLYPH_W) >> 1));   // centred a bit right of the screen centre
    att_s0 = scroll_x;
    att_y0 = cam_y;
    att_on = 1;
    if (_cpu != CGB_TYPE) attempt_text_load_tiles();   // (CGB: loaded with the other HUD tiles)
}

void attempt_text_hide(void) BANKED {
    if (att_on && _cpu != CGB_TYPE) restore_ship_tiles();
    att_on = 0;
}

// Draws the text (if still on screen; screen scroll scroll_x) from OAM slot oam; returns the
// next free slot
uint8_t attempt_text_draw(uint8_t oam, uint16_t scroll_x, uint16_t cam_y) BANKED {
    if (!att_on) return oam;
    int16_t sx = (int16_t)att_sx0 - (int16_t)(scroll_x - att_s0);
    if (sx + (int16_t)(att_n * GLYPH_W) <= 0 || sx >= 168) { attempt_text_hide(); return oam; }   // scrolled away
    int16_t sy = (int16_t)(ATT_SCREEN_Y + 16) - (int16_t)(cam_y - att_y0);
    if (sy <= 0 || sy >= 160) return oam;
    uint8_t prop = (_cpu == CGB_TYPE) ? (7 | S_BANK) : 0;
    for (uint8_t i = 0; i < att_n && oam < MAX_HARDWARE_SPRITES; i++, sx += GLYPH_W) {
        uint8_t g = att_glyph[i];
        if (g == 0xFF || sx <= 0 || sx >= 168) continue;
        shadow_OAM[oam].y = (uint8_t)sy;
        shadow_OAM[oam].x = (uint8_t)sx;
        shadow_OAM[oam].tile = (uint8_t)(ATT_TILE_BASE + (g << 1));
        shadow_OAM[oam].prop = prop;
        oam++;
    }
    return oam;
}

void percent_hud_load_tiles(void) BANKED {
    uint8_t k;
    attempt_text_load_tiles();
    for (k = 0; k < 10; k++) {
        set_sprite_data((uint8_t)(HUD_PAIR_BASE + (k << 1) + 1), 1, &FontPusab[(FONT_DIGIT0 + k) * 16]);
    }
    set_sprite_data((uint8_t)(HUD_PAIR_BASE + (GLYPH_PERCENT << 1) + 1), 1, &FontPusab[FONT_PERCENT * 16]);
    shown = 0xFF;
}

static void next_threshold(void) {
    q += step_q;
    rem += step_r;
    if (rem >= 100) { rem -= 100; q++; }
    thr = rem ? (uint16_t)(q + 1) : q;
}

void percent_hud_reset(uint16_t max_scroll_px) BANKED {
    pct = 0;
    q = 0; rem = 0;
    step_q = max_scroll_px / 100u;
    step_r = (uint8_t)(max_scroll_px % 100u);
    next_threshold();
    shown = 0xFF;
}

void percent_hud_complete(void) BANKED {
    pct = 100;
}

void percent_hud_hide(void) BANKED {
    shadow_OAM[0].y = shadow_OAM[1].y = shadow_OAM[2].y = shadow_OAM[3].y = 0;
    shown = 0xFF;
}

static void put(uint8_t slot, uint8_t x, uint8_t glyph) {
    shadow_OAM[slot].y = HUD_Y;
    shadow_OAM[slot].x = x;
    shadow_OAM[slot].tile = (uint8_t)(HUD_PAIR_BASE + (glyph << 1));
    // CGB: sprite palette 7 = the colours of the pause menu's "PAUSED" text
    // (black outline, light blue shading, white). DMG: OBP0
    shadow_OAM[slot].prop = (_cpu == CGB_TYPE) ? 7 : 0;
}

// Same value as the saved progress: floor(cam_px * 100 / max_scroll_px)
void percent_hud_update(uint16_t cam_px) BANKED {
    while (pct < 100 && cam_px >= thr) {
        pct++;
        if (pct < 100) next_threshold();
    }
    if (!setting_show_percent || DEBUG_ON()) {
        if (shown != 0xFE) { percent_hud_hide(); shown = 0xFE; }
        return;
    }
    if (pct == shown) return;
    shown = pct;

    uint8_t tens = pct / 10u, ones = pct % 10u;   // once per percent, not per frame
    uint8_t n = (pct == 100) ? 4 : (pct >= 10) ? 3 : 2;
    uint8_t x = (uint8_t)(80 + 8 - ((n * GLYPH_W) >> 1));
    uint8_t s = 0;
    if (pct == 100) {
        put(s++, x, 1); x += GLYPH_W;
        put(s++, x, 0); x += GLYPH_W;
        put(s++, x, 0); x += GLYPH_W;
    } else {
        if (pct >= 10) { put(s++, x, tens); x += GLYPH_W; }
        put(s++, x, ones); x += GLYPH_W;
    }
    put(s++, x, GLYPH_PERCENT);
    while (s < 4) shadow_OAM[s++].y = 0;
}
