#pragma bank 28

// STATE_LEVEL_COMPLETE: play_level() returns after the end shake with the level still on
// screen. "LEVEL COMPLETE!" wipes in over it, then about 2 s later the stats box with RETRY / MENU.
// All of it is background tiles in the level's own map, in BG slots no visible cell uses.

#include <gb/gb.h>
#include "scratch.h"
#include "music.h"
#include <gb/cgb.h>
#include "states.h"
#include "settings_bg.h"
#include "level_complete.h"
#include "level_complete_gfx.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "hUGEDriver.h"
#include "fade.h"
#include "collision.h"
#include "settings.h"
#include "famidash_bg.h"

BANKREF(state_level_complete)

extern const unsigned char FontPusab[];
extern const uint8_t extra_font_tiles[4 * 16];   // state_settings.c (same bank)
extern const hUGESong_t menuloop;
extern volatile uint8_t current_song_bank;
extern uint8_t music_ready;

// layout, in screen cells (20 x 18)
#define TEXT_X             2     // the 16 x 4 text block
#define TEXT_Y             2
#define LEVEL_COLS         10    // LEVEL is 10 columns wide, centred over COMPLETE!
#define LEVEL_X_SHIFT      3
#define BOX_TOP            8     // like the settings board: border x 2..17, text columns 3..16
#define BOX_BOTTOM         16
#define BOX_ROW_ATTEMPTS   10
#define BOX_ROW_COINS      12
#define BOX_ROW_MODE       13
#define BOX_ROW_BUTTONS    15
#define PAL_BORDER         5     // CGB BG palettes 5..7 are unused in gameplay
#define PAL_BOARD          6
#define PAL_TEXT           7
#define TILE_BLANK         12    // the gameplay sheet's transparent tile: the board interior

uint8_t lc_dbg_free, lc_dbg_need;   // free / needed tile slots (tools: how tight is it)

static uint8_t skip;        // A pressed during the animation: the rest runs without waits
static uint8_t prev_joy;

static uint8_t map_x0, map_y0;                          // BG map cell at the top left of the screen
// in the shared scratch buffer while this screen is up (scratch.h)
#define text_slot   (scratch_buf)                                   // VRAM slots of the text's tiles
#define border_slot (scratch_buf + PRACTICE_COMPLETE_TEXT_UNIQUE)     // ... of the box border tiles
#define font_slot   (scratch_buf + PRACTICE_COMPLETE_TEXT_UNIQUE + 10) // ... of the font glyphs (0xFF: not loaded)

// settings_bg tiles of the box border, in border_slot order
static const uint8_t border_src[10] = { 2, 3, 4, 5, 6, 7, 8, 9, 0x18, 0x19 };
#define B_TOP    0
#define B_TL     1
#define B_TR     2
#define B_LEFT   3
#define B_RIGHT  4
#define B_BOTTOM 5
#define B_NOTCH0 6
#define B_NOTCH1 7
#define B_BL     8
#define B_BR     9

static void wait_frames(uint8_t n) {
    while (n--) {
        if (skip) return;
        wait_vbl_done();
        uint8_t joy = joypad();
        if ((joy & J_A) && !(prev_joy & J_A)) skip = 1;
        prev_joy = joy;
    }
}

static void put(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
    uint8_t mx = (uint8_t)((map_x0 + x) & 31), my = (uint8_t)((map_y0 + y) & 31);
    set_bkg_tile_xy(mx, my, tile);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_bkg_tile_xy(mx, my, attr);
        VBK_REG = 0;
    }
}

// Loads one tile. The text art uses colour 1 for its outline, 2 for the light fill and 3 for the
// shaded fill (CGB palette); on DMG the colours are renumbered to read dark outline, light fill.
static void load_tile(uint8_t slot, const uint8_t *src, uint8_t dmg_text) {
    if (!dmg_text || _cpu == CGB_TYPE) {
        set_bkg_data(slot, 1, src);
        return;
    }
    uint8_t buf[16], i;
    for (i = 0; i < 16; i += 2) {
        uint8_t lo = src[i], hi = src[i + 1];
        uint8_t p1 = lo & (uint8_t)~hi, p2 = (uint8_t)~lo & hi, p3 = lo & hi;
        buf[i] = p2 | p1;        // new colour 1 (old 2) and 3 (old 1)
        buf[i + 1] = p3 | p1;    // new colour 2 (old 3) and 3 (old 1)
    }
    set_bkg_data(slot, 1, buf);
}

// text

static uint8_t font_tile(char c) {
    if (c == ' ') return 0;
    if (c == '%') return 1;
    if (c == '/') return 2;
    if (c >= '0' && c <= '9') return (uint8_t)((c - '0') + 3);
    if (c >= 'A' && c <= 'Z') return (uint8_t)((c - 'A') + 13);
    if (c == '>') return 40;
    return 0;
}

static void need_glyphs(const char *s) {
    for (; *s; s++) {
        uint8_t t = font_tile(*s);
        if (t) font_slot[t] = 0;
    }
}

// Cell of the text block's tile i: LEVEL or PRACTICE (rows 0..1) is shifted to the middle
static uint8_t text_cell_x(uint8_t i) {
    uint8_t c = (uint8_t)(i & (LEVEL_COMPLETE_TEXT_COLS - 1));
    return (uint8_t)(TEXT_X + c + ((i < 2 * LEVEL_COMPLETE_TEXT_COLS) ? (lc_practice ? PRACTICE_X_SHIFT : LEVEL_X_SHIFT) : 0));
}
static uint8_t text_cell_y(uint8_t i) {
    return (uint8_t)(TEXT_Y + (i >> 4));
}

// Is cell (x, y) drawn over by the text (its art is not empty there)?
static uint8_t text_covers(uint8_t x, uint8_t y) {
    if (y < TEXT_Y || y >= TEXT_Y + LEVEL_COMPLETE_TEXT_ROWS) return 0;
    uint8_t r = (uint8_t)(y - TEXT_Y);
    int8_t c = (int8_t)x - TEXT_X - (r < 2 ? (lc_practice ? PRACTICE_X_SHIFT : LEVEL_X_SHIFT) : 0);
    if (c < 0 || c >= LEVEL_COMPLETE_TEXT_COLS) return 0;
    const uint8_t *map = lc_practice ? practice_complete_text_map : level_complete_text_map;
    return map[(uint8_t)(r * LEVEL_COMPLETE_TEXT_COLS + c)] != 0xFF;
}

// Picks VRAM slots no visible cell outside the text and the box shows, and loads the tiles there.
// Returns 0 when there are not enough free slots.
static uint8_t tiles_load(void) {
    uint8_t used[128];
    uint8_t x, y, i, t, n;
    for (i = 0; i < 128; i++) used[i] = 0;
    used[TILE_BLANK] = 1;
    for (y = 0; y < 18; y++) {
        for (x = 0; x < 20; x++) {
            if (y >= BOX_TOP && y <= BOX_BOTTOM && x >= 2 && x <= 17) continue;   // overwritten
            if (text_covers(x, y)) continue;
            uint8_t mx = (uint8_t)((map_x0 + x) & 31), my = (uint8_t)((map_y0 + y) & 31);
            if (_cpu == CGB_TYPE) {         // a cell showing a bank 1 tile (parallax, saws) uses no slot here
                VBK_REG = 1;
                t = get_bkg_tile_xy(mx, my);
                VBK_REG = 0;
                if (t & 8) continue;
            }
            t = get_bkg_tile_xy(mx, my);
            if (t < 128) used[t] = 1;
        }
    }
    for (i = 0; i < 44; i++) font_slot[i] = 0xFF;
    need_glyphs("ATTEMPTS COINS RETRY MENU PRACTICE RUN");
    need_glyphs("0123456789/>");
    uint8_t text_unique = lc_practice ? PRACTICE_COMPLETE_TEXT_UNIQUE : LEVEL_COMPLETE_TEXT_UNIQUE;
    const uint8_t *text_tiles = lc_practice ? practice_complete_text_tiles : level_complete_text_tiles;
    uint8_t need = (uint8_t)(text_unique + 10);
    for (i = 0; i < 44; i++) if (font_slot[i] == 0) need++;
    uint8_t free_n = 0;
    for (i = 0; i < 128; i++) if (!used[i]) free_n++;
    lc_dbg_free = free_n;
    lc_dbg_need = need;
    if (free_n < need) return 0;

    i = 0;
    for (n = 0; n < text_unique; n++) {
        while (used[i]) i++;
        text_slot[n] = i;
        used[i] = 1;
        load_tile(i, text_tiles + (uint16_t)n * 16u, 1);
    }
    for (n = 0; n < 10; n++) {
        while (used[i]) i++;
        border_slot[n] = i;
        used[i] = 1;
        load_tile(i, settings_bg_tiles + (uint16_t)border_src[n] * 16u, 0);
    }
    for (n = 0; n < 44; n++) {
        if (font_slot[n] != 0) continue;
        while (used[i]) i++;
        font_slot[n] = i;
        used[i] = 1;
        load_tile(i, n >= 39 ? extra_font_tiles + (uint16_t)(n - 39) * 16u : FontPusab + (uint16_t)n * 16u, 0);
    }
    return 1;
}

// One column of the text: LEVEL / PRACTICE (rows 0..1) or COMPLETE! (rows 2..3)
static void text_column(uint8_t row0, uint8_t c) {
    const uint8_t *map = lc_practice ? practice_complete_text_map : level_complete_text_map;
    for (uint8_t r = row0; r < (uint8_t)(row0 + 2); r++) {
        uint8_t i = (uint8_t)(r * LEVEL_COMPLETE_TEXT_COLS + c);
        uint8_t u = map[i];
        if (u != 0xFF) put(text_cell_x(i), text_cell_y(i), text_slot[u], PAL_TEXT);
    }
}

// box

static void box_text(uint8_t x, uint8_t y, const char *s) {
    for (; *s; s++, x++) {
        uint8_t t = font_tile(*s);
        put(x, y, t ? font_slot[t] : TILE_BLANK, PAL_BOARD);
    }
}

static void box_number(uint8_t right_x, uint8_t y, uint16_t v) {
    char buf[6];
    uint8_t n = 0;
    do { buf[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    uint8_t x = (uint8_t)(right_x + 1 - n);
    while (n) { n--; put(x, y, font_slot[3 + (buf[n] - '0')], PAL_BOARD); x++; }
}

static void box_row(uint8_t y) {
    uint8_t x;
    if (y == BOX_TOP) {
        put(2, y, border_slot[B_TL], PAL_BORDER);
        for (x = 3; x < 17; x++) put(x, y, border_slot[B_TOP], PAL_BORDER);
        put(17, y, border_slot[B_TR], PAL_BORDER);
    } else if (y == BOX_BOTTOM) {
        put(2, y, border_slot[B_BL], PAL_BORDER);
        for (x = 3; x < 17; x++) {
            if (x == 9 || x == 10) put(x, y, border_slot[x == 9 ? B_NOTCH0 : B_NOTCH1], PAL_BORDER);
            else put(x, y, border_slot[B_BOTTOM], PAL_BORDER);
        }
        put(17, y, border_slot[B_BR], PAL_BORDER);
    } else {
        put(2, y, border_slot[B_LEFT], PAL_BORDER);
        for (x = 3; x < 17; x++) put(x, y, TILE_BLANK, PAL_BOARD);
        put(17, y, border_slot[B_RIGHT], PAL_BORDER);
    }
}

static void draw_buttons(uint8_t sel) {
    box_text(4, BOX_ROW_BUTTONS, sel == 0 ? ">" : " ");
    box_text(5, BOX_ROW_BUTTONS, "RETRY");
    box_text(11, BOX_ROW_BUTTONS, sel == 1 ? ">" : " ");
    box_text(12, BOX_ROW_BUTTONS, "MENU");
}

static void draw_stats(void) {
    uint8_t coins = (uint8_t)((lc_coins & 1) + ((lc_coins >> 1) & 1) + ((lc_coins >> 2) & 1));
    box_text(4, BOX_ROW_ATTEMPTS, "ATTEMPTS");
    box_number(15, BOX_ROW_ATTEMPTS, lc_attempts);
    box_text(4, BOX_ROW_COINS, "COINS");
    put(13, BOX_ROW_COINS, font_slot[3 + coins], PAL_BOARD);
    put(14, BOX_ROW_COINS, font_slot[2], PAL_BOARD);
    put(15, BOX_ROW_COINS, font_slot[3 + 3], PAL_BOARD);
    if (lc_practice) box_text(4, BOX_ROW_MODE, "PRACTICE RUN");
}

// leaving

static void restore_menu_music(void) {
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    if (setting_music_enabled) {
        music_play_menu();
        TAC_REG = 0x04;
        music_ready = 1;
    } else {
        music_ready = 0;
    }
}

static GameState leave(GameState next) {
    if (setting_sfx_enabled) {
        // the end jingle may still be playing: let it finish, then the menu sound
        while (is_sample_playing()) wait_vbl_done();
        if (next == STATE_PLAY_LEVEL) play_sample_with_music(BANK_SFX_DATA, play_sound_data, PLAY_SOUND_LEN);
        else play_sample_with_music(BANK_SFX_DATA, quit_sound_data, QUIT_SOUND_LEN);
    }
    fade_to_black(2);
    while (is_sample_playing()) wait_vbl_done();
    stop_sample();
    HIDE_SPRITES;
    SCX_REG = 0;
    SCY_REG = 0;
    LCDC_REG &= (uint8_t)~LCDCF_BG9C00;   // a mirror portal may have left the level on the 0x9C00 map
    if (next != STATE_PLAY_LEVEL) restore_menu_music();
    return next;
}

// The colour the level shows around the text: the most common colour of the tile left of it
static palette_color_t level_colour(void) {
    if (_cpu != CGB_TYPE) return 0;
    uint8_t mx = (uint8_t)((map_x0 + 1) & 31), my = (uint8_t)((map_y0 + TEXT_Y + 1) & 31);
    uint8_t t = get_bkg_tile_xy(mx, my), buf[16], cnt[4] = { 0, 0, 0, 0 }, i, x, best = 0;
    VBK_REG = 1;
    uint8_t attr = get_bkg_tile_xy(mx, my);
    VBK_REG = 0;
    if (attr & 8) return shadow_bkg_palettes[(attr & 7) << 2];      // bank 1 tile: its palette's colour 0
    get_bkg_data(t, 1, buf);
    for (i = 0; i < 16; i += 2)
        for (x = 0; x < 8; x++) cnt[((buf[i + 1] >> x) & 1) << 1 | ((buf[i] >> x) & 1)]++;
    for (i = 1; i < 4; i++) if (cnt[i] > cnt[best]) best = i;
    return shadow_bkg_palettes[((attr & 7) << 2) + best];
}

GameState update_level_complete_state(void) BANKED {
    uint8_t c, y;
    skip = 0;
    prev_joy = joypad();

    // grid-lock the scroll like the pause menu does, then pick the tile slots
    uint8_t scx = (uint8_t)(SCX_REG & 0xF8u), scy = (uint8_t)(SCY_REG & 0xF8u);
    move_bkg(scx, scy);
    map_x0 = (uint8_t)(scx >> 3);
    map_y0 = (uint8_t)(scy >> 3);
    uint8_t have_gfx = tiles_load();
    if (_cpu == CGB_TYPE) {
        palette_color_t pal[4];
        pal[0] = level_colour();                       // around the letters
        if (lc_practice) {                             // PRACTICE COMPLETE! is blue
            pal[1] = RGB8(10, 40, 72);
            pal[2] = RGB8(120, 236, 244);
            pal[3] = RGB8(44, 170, 220);
        } else {
            pal[1] = RGB8(20, 48, 6);                  // outline
            pal[2] = RGB8(176, 226, 64);               // light fill
            pal[3] = RGB8(104, 178, 28);               // shaded fill
        }
        // All through the fade module (RAM copies: it is banked), so that leaving fades the
        // whole screen: set directly, the fade only dimmed the sprites. The level's palettes
        // too, as they are now (a colour trigger fade may have been running).
        fade_set_bkg_palette(0, 5, famidash_bg_palettes);
        fade_set_bkg_palette(PAL_TEXT, 1, pal);
        // the settings board's palettes 1 (border) and 2 (board + text)
        palette_color_t board[8];
        for (y = 0; y < 8; y++) board[y] = settings_bg_palettes[4 + y];
        fade_set_bkg_palette(PAL_BORDER, 2, board);   // PAL_BOARD = PAL_BORDER + 1
    }
    // the level's object sprites would be drawn over the text and the box
    for (c = 4; c < 40; c++) shadow_OAM[c].y = 0;

    uint8_t sel = 0;
    if (have_gfx) {
        wait_frames(4);
        // both lines grow from the middle outwards, a column on each side per step
        for (c = 0; c < LEVEL_COMPLETE_TEXT_COLS / 2; c++) {
            if (lc_practice) {
                // PRACTICE is 15 columns (0..14), expanding from center col 7
                text_column(0, (uint8_t)(7 - c));
                if (c > 0) text_column(0, (uint8_t)(7 + c));
            } else {
                if (c < LEVEL_COLS / 2) {
                    text_column(0, (uint8_t)(LEVEL_COLS / 2 - 1 - c));
                    text_column(0, (uint8_t)(LEVEL_COLS / 2 + c));
                }
            }
            text_column(2, (uint8_t)(LEVEL_COMPLETE_TEXT_COLS / 2 - 1 - c));
            text_column(2, (uint8_t)(LEVEL_COMPLETE_TEXT_COLS / 2 + c));
            wait_frames(3);
        }
        // 2 seconds with the text on the level (A skips the wait)
        for (y = 0; y < 120 && !skip; y++) wait_frames(1);
        skip = 0;
        for (y = BOX_TOP; y <= BOX_BOTTOM; y++) {
            box_row(y);
            wait_frames(2);
        }
        draw_stats();
        draw_buttons(sel);
    }
    // the A that may still be held from the level must not choose RETRY right away
    prev_joy = joypad();
    while (joypad() & (J_A | J_START | J_B)) wait_vbl_done();
    prev_joy = joypad();

    while (1) {
        wait_vbl_done();
        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;
        if (have_gfx && (pressed & (J_LEFT | J_RIGHT))) {
            sel ^= 1;
            draw_buttons(sel);
        }
        if (pressed & (J_A | J_START)) return leave(sel ? STATE_NEW_MENU_SELECT : STATE_PLAY_LEVEL);
        if (pressed & J_B) return leave(STATE_NEW_MENU_SELECT);
    }
}
