#pragma bank 28
// The extra levels (the wrench in the main menu): a GD-style list on the hanging table of the
// settings (settings_bg.c, this bank). A row per level: its difficulty face (sprites), its name on
// up to two lines and the best normal-mode percent, the rows lighter and darker by turns.
// UP/DOWN move, A plays, B goes back.

#include "states.h"
#include "gameplay.h"
#include "assets.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "fade.h"
#include "save_manager.h"
#include "settings_bg.h"
#include "scratch.h"
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>

#include "graphics/difficulty_faces.h"

extern uint8_t selected;
extern uint8_t music_ready;
extern const uint8_t extra_font_tiles[4 * 16];   // state_settings.c (this bank)

#define LIST_ROWS   4       // levels on screen: 2 tile rows each, a separator row between them
#define ROW_Y(r)    ((uint8_t)(3u + (r) * 3u))
#define COL0        2       // the inside of the table: columns 2..17
#define COLS        16
#define NAME_AT     3       // in a row: cursor, face (2), name
#define NAME_COLS   13
#define CURSOR_TILE 27      // and 28 below it
#define PAL_ROW     2
#define PAL_SEL     4       // the selected row (CGB; the chains use 5 here)
// The darker rows: the font and the cursor again with colour 1 (the dark brown, DMG light grey)
// as their background, made from the loaded ones. Tiles 0x80.. (free on this screen)
#define DARK_FONT   0x80u                 // font tiles 0..42
#define DARK_CURSOR (DARK_FONT + 43u)     // and 44
#define GAP_LD      (DARK_FONT + 45u)     // a row between two: the light one above, the dark below
#define GAP_DL      (DARK_FONT + 46u)
#define IS_DARK(idx) (((uint8_t)((idx) - NUM_MAIN_LEVELS)) & 1u)

// tiles 26 (row separator), 27/28 (the cursor, 8 x 16)
static const uint8_t list_tiles[48] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xe0,0x40,0xf0,0x60,0xf8,0x70,0xfc,0x78,0xfe,0x7c,0xfe,
    0x7c,0xfe,0x78,0xfe,0x70,0xfc,0x60,0xf8,0x40,0xf0,0x00,0xe0,0x00,0x00,0x00,0x00,
};
// the settings' table (settings_bg.c) widened to columns 1..18, the back arrow a row up
static const uint8_t list_map[360] = {
    0x0a,0x0b,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,
    0x0c,0x0d,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,
    0x00,0x03,0x02,0x02,0xe1,0xf4,0xf0,0xee,0xdd,0xd0,0xe8,0xe1,0xf2,0xe1,0xe8,0xef,0x02,0x02,0x04,0x00,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,
    0x00,0x05,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x06,0x00,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,
    0x00,0x05,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x06,0x00,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,
    0x00,0x05,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x1a,0x06,0x00,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,
    0x00,0x18,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x08,0x09,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x19,0x00,
    0x0e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,
    0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x11,
    0x12,0x13,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x15,0x16,0x17,
};
static const uint8_t list_attr[360] = {
    0x05,0x05,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,
    0x05,0x05,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,
    0x00,0x07,0x01,0x01,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x01,0x01,0x07,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x01,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x00,
    0x00,0x07,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x07,0x07,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x07,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,
};
// No statics or big locals here: WRAM is full and the stack ends just above the variables
// (scratch.h). Everything goes in scratch_buf.
#define line1    (scratch_buf)                              // a row: its 2 lines of tiles
#define line2    (scratch_buf + COLS)
#define pal_buf  ((palette_color_t *)(scratch_buf + 32))    // 32 colours
#define num_buf  (scratch_buf + 96)                         // 8 bytes

static uint8_t char_tile(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(FONT_PUSAB_START + 3 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(FONT_PUSAB_START + 13 + (c - 'A'));
    if (c == '%') return (uint8_t)(FONT_PUSAB_START + 1);
    if (c == '/') return (uint8_t)(FONT_PUSAB_START + 2);
    return 0;   // the blank of the table
}

// n (0..255) as digits into p, returns how many
static uint8_t put_number(uint8_t *p, uint8_t n) {
    uint8_t k = 0;
    if (n >= 100u) {
        p[k++] = (uint8_t)(FONT_PUSAB_START + 3 + n / 100u);
        n %= 100u;
        p[k++] = (uint8_t)(FONT_PUSAB_START + 3 + n / 10u);
        n %= 10u;
    } else if (n >= 10u) {
        p[k++] = (uint8_t)(FONT_PUSAB_START + 3 + n / 10u);
        n %= 10u;
    }
    p[k++] = (uint8_t)(FONT_PUSAB_START + 3 + n);
    return k;
}

// List row r shows level idx (none: MAX_LEVELS or more). faces: also the tiles and colours of
// its face
static void draw_row(uint8_t r, uint8_t idx, uint8_t sel, uint8_t faces) {
    uint8_t y = ROW_Y(r);
    uint8_t spr = (uint8_t)(r << 2);
    memset(line1, 0, COLS);
    memset(line2, 0, COLS);
    if (idx < MAX_LEVELS) {
        const char *name = game_levels[idx]->name;
        uint8_t len = 0;
        while (name[len]) len++;
        // two lines: break at the last space that leaves both short enough
        uint8_t split = len, skip = 0;
        if (len > NAME_COLS) {
            split = NAME_COLS;
            for (uint8_t i = 1; i <= NAME_COLS; i++) {
                if (name[i] == ' ' && (uint8_t)(len - i - 1u) <= NAME_COLS) { split = i; skip = 1; }
            }
        }
        uint8_t i = 0;
        for (; i < split; i++) line1[NAME_AT + i] = char_tile(name[i]);
        uint8_t len2 = 0;
        for (i = (uint8_t)(split + skip); i < len && len2 < NAME_COLS; i++) line2[NAME_AT + len2++] = char_tile(name[i]);
        // the best normal run, at the right of the second line when it fits
        uint8_t *num = num_buf;
        uint8_t n = put_number(num, saved_level_progress(level_save_slot[idx], 0));
        num[n++] = char_tile('%');
        if ((uint8_t)(len2 + (len2 ? 1u : 0u) + n) <= NAME_COLS) memcpy(line2 + COLS - n, num, n);
        if (sel) { line1[0] = CURSOR_TILE; line2[0] = CURSOR_TILE + 1; }

        if (faces) {
            uint8_t diff = level_difficulty[idx];
            if (diff >= NUM_DIFFICULTY_FACES) diff = 8;   // NA
            if (_cpu == CGB_TYPE) {
                palette_color_t *pals = pal_buf;   // fade_set_sprite_palette is banked: a RAM copy
                memcpy(pals, difficulty_face_pals[diff], 8 * sizeof(palette_color_t));
                set_sprite_data(spr, 4, difficulty_face_cgb_tiles[diff]);
                fade_set_sprite_palette((uint8_t)(r << 1), 2, pals);
            } else {
                set_sprite_data(spr, 4, difficulty_face_dmg_tiles[diff]);
            }
            uint8_t px = (uint8_t)((COL0 + 1u) * 8u + 8u - 2u), py = (uint8_t)(y * 8u + 16u);
            for (uint8_t k = 0; k < 4; k++) {
                uint8_t s = (uint8_t)(spr + k);
                set_sprite_tile(s, s);
                set_sprite_prop(s, (_cpu == CGB_TYPE) ? (uint8_t)((r << 1) + (k >> 1)) : 0u);
                move_sprite(s, (uint8_t)(px + ((k & 1u) << 3)), (uint8_t)(py + ((k & 2u) << 2)));
            }
        }
    } else if (faces) {
        for (uint8_t k = 0; k < 4; k++) hide_sprite((uint8_t)(spr + k));
    }
    if (IS_DARK(idx)) {
        for (uint8_t i = 0; i < 2u * COLS; i++) {
            uint8_t t = line1[i];             // (line2 follows line1)
            if (t >= FONT_PUSAB_START) t = (uint8_t)(t - FONT_PUSAB_START + DARK_FONT);
            else if (t >= CURSOR_TILE) t = (uint8_t)(t - CURSOR_TILE + DARK_CURSOR);
            else t = DARK_FONT;               // blank
            line1[i] = t;
        }
    }
    set_bkg_tiles(COL0, y, COLS, 1, line1);
    set_bkg_tiles(COL0, (uint8_t)(y + 1u), COLS, 1, line2);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(COL0, y, COLS, 2, sel ? PAL_SEL : PAL_ROW);
        VBK_REG = 0;
    }
}

// "3/9" under the table, white on black like the title
static void draw_counter(void) {
    uint8_t *buf = num_buf;
    memset(buf, 0, 8);
    uint8_t n = put_number(buf + 1, (uint8_t)(selected - NUM_MAIN_LEVELS + 1u));
    buf[1 + n++] = char_tile('/');
    n += put_number(buf + 1 + n, (uint8_t)(MAX_LEVELS - NUM_MAIN_LEVELS));
    // 3 to 5 letters, in columns 7..13 (the first and last stay blank)
    uint8_t *from = (n >= 5u) ? buf + 1 : buf;
    set_bkg_tiles(7, 16, 7, 1, from);
    if (_cpu == CGB_TYPE) {
        for (uint8_t i = 0; i < 7u; i++) from[i] = from[i] ? 3u : 0u;
        VBK_REG = 1;
        set_bkg_tiles(7, 16, 7, 1, from);
        VBK_REG = 0;
    }
}

static void draw_list(uint8_t top, uint8_t faces) {
    for (uint8_t r = 0; r < LIST_ROWS; r++) {
        uint8_t idx = (uint8_t)(top + r);
        draw_row(r, idx, idx == selected, faces);
        // the row under it: half its shade, half the next one's
        if (r + 1u < LIST_ROWS)
            fill_bkg_rect(COL0, (uint8_t)(ROW_Y(r) + 2u), COLS, 1, IS_DARK(idx) ? GAP_DL : GAP_LD);
    }
    draw_counter();
}

GameState update_level_select_state(void) BANKED {
    // load with the display on (off shows white); fade_from_black shows the palettes
    fade_set_black();
    fade_hold = 1;
    HIDE_SPRITES;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
    HIDE_WIN;
    SCX_REG = 0;
    SCY_REG = 0;
    SPRITES_8x8;

    fill_bkg_rect(0, 0, 32, 32, 0);
    set_bkg_data(0, SETTINGS_BG_TILE_COUNT, settings_bg_tiles);
    set_bkg_data(SETTINGS_BG_TILE_COUNT, 3, list_tiles);
    setup_menu_font();
    set_bkg_data((uint8_t)(FONT_PUSAB_START + 39), 4, extra_font_tiles);
    set_bkg_tiles(0, 0, 20, 18, list_map);
    // the dark copies: colour 0 -> 1 (a pixel with neither bit set gets the low one)
    for (uint8_t t = 0; t < 43u; t = (uint8_t)(t + 16u)) {
        uint8_t n = (43u - t < 16u) ? (uint8_t)(43u - t) : 16u;
        get_bkg_data((uint8_t)(FONT_PUSAB_START + t), n, scratch_buf);
        for (uint16_t i = 0; i < (uint16_t)n * 16u; i += 2u) scratch_buf[i] |= (uint8_t)~(scratch_buf[i] | scratch_buf[i + 1u]);
        set_bkg_data((uint8_t)(DARK_FONT + t), n, scratch_buf);
    }
    memcpy(scratch_buf, list_tiles + 16, 32);
    for (uint8_t i = 0; i < 32u; i += 2u) scratch_buf[i] |= (uint8_t)~(scratch_buf[i] | scratch_buf[i + 1u]);
    for (uint8_t i = 0; i < 16u; i += 2u) {   // the two gap tiles: 4 rows of each shade
        uint8_t top = (i < 8u) ? 0x00u : 0xFFu;
        scratch_buf[32u + i] = top;
        scratch_buf[48u + i] = (uint8_t)~top;
        scratch_buf[33u + i] = 0;
        scratch_buf[49u + i] = 0;
    }
    set_bkg_data(DARK_CURSOR, 4, scratch_buf);

    if (_cpu == CGB_TYPE) {
        palette_color_t *pals = pal_buf;   // fade_set_bkg_palette is banked: a RAM copy
        memcpy(pals, settings_bg_palettes, 32 * sizeof(palette_color_t));
        // 2: colour 1 (the letters' shadow) is the darker rows' brown too; 4: the selected row
        pals[8 + 1] = RGB8(119, 61, 0);
        pals[16 + 0] = RGB8(214, 126, 24);
        pals[16 + 1] = RGB8(168, 94, 14);
        pals[16 + 2] = RGB8(0, 0, 0);
        pals[16 + 3] = RGB8(255, 255, 255);
        fade_set_bkg_palette(0, 8, pals);
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0);
        VBK_REG = 0;
        set_bkg_attributes(0, 0, 20, 18, list_attr);
    }
    fade_set_dmg_palettes(0xE4, 0xE0, 0xE4);

    if (selected < NUM_MAIN_LEVELS || selected >= MAX_LEVELS) selected = NUM_MAIN_LEVELS;
    uint8_t top = NUM_MAIN_LEVELS;
    if (selected >= (uint8_t)(top + LIST_ROWS)) top = (uint8_t)(selected - LIST_ROWS + 1u);
    draw_list(top, 1);

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;
    fade_from_black(2);

    uint8_t prev_joy = joypad();
    uint8_t held = 0;            // frames UP or DOWN has been held (it repeats)
    while (1) {
        wait_vbl_done();
        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;

        if (joy & (J_UP | J_DOWN)) {
            if (++held >= 24u) { held = 18; pressed |= joy & (J_UP | J_DOWN); }
        } else {
            held = 0;
        }

        uint8_t old = selected;
        if (pressed & J_UP) {
            if (selected > NUM_MAIN_LEVELS) selected--;
        } else if (pressed & J_DOWN) {
            if (selected < (uint8_t)(MAX_LEVELS - 1u)) selected++;
        }
        if (selected != old) {
            uint8_t old_top = top;
            if (selected < top) top = selected;
            else if (selected >= (uint8_t)(top + LIST_ROWS)) top = (uint8_t)(selected - LIST_ROWS + 1u);
            if (top != old_top) {
                draw_list(top, 1);
            } else {
                draw_row((uint8_t)(old - top), old, 0, 0);
                draw_row((uint8_t)(selected - top), selected, 1, 0);
                draw_counter();
            }
        }

        if (pressed & (J_A | J_START)) {
            music_ready = 0;
            TAC_REG = 0x00;
            play_sample(BANK_SFX_DATA, play_sound_data, PLAY_SOUND_LEN);
            fade_capture_current();
            fade_to_black(2);
            HIDE_SPRITES;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            return STATE_PLAY_LEVEL;
        }
        if (pressed & J_B) {
            fade_capture_current();
            fade_to_black(2);
            HIDE_SPRITES;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            return STATE_MENU;
        }
    }
}
