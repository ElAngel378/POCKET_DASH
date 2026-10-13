#pragma bank 28

#include <gb/gb.h>
#include "music.h"
#include <gb/cgb.h>
#include "states.h"
#include "settings_bg.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "hUGEDriver.h"
#include "save_manager.h"
#include "collision.h"
#include "fade.h"

BANKREF(state_settings)

#define FONT_PUSAB_START 0xD0
extern const unsigned char FontPusab[];
extern const hUGESong_t menuloop;
extern volatile uint8_t current_song_bank;
extern uint8_t music_ready;

void setup_menu_font(void) BANKED;

// Custom font tiles: < (39), > (40), - (41), : (42)
const uint8_t extra_font_tiles[4 * 16] = {  // also used by state_level_complete.c (same bank)
    // 39: Left arrow <
    0x00,0x08, 0x08,0x1C, 0x18,0x3C, 0x38,0x7C, 0x18,0x3C, 0x08,0x1C, 0x00,0x08, 0x00,0x00,
    // 40: Right arrow cursor > (White body color 3, dark outline color 2)
    0x00,0x00, 0x20,0x30, 0x38,0x3C, 0x3E,0x3F, 0x3E,0x3F, 0x38,0x3C, 0x20,0x30, 0x00,0x00,
    // 41: Dash -
    0x00,0x00, 0x00,0x00, 0x00,0x7C, 0x38,0x7C, 0x00,0x7C, 0x00,0x00, 0x00,0x00, 0x00,0x00,
    // 42: Colon :
    0x00,0x00, 0x00,0x30, 0x10,0x38, 0x00,0x30, 0x00,0x30, 0x10,0x38, 0x00,0x30, 0x00,0x00
};

// Setting item definitions
typedef enum {
    SETTING_TYPE_TOGGLE = 0,
    SETTING_TYPE_CHOICE,
    SETTING_TYPE_ACTION
} SettingType;

typedef struct {
    const char *name;
    SettingType type;
    uint8_t *val_ptr;
    uint8_t min_val;
    uint8_t max_val;
    const char * const *val_labels;
    void (*on_change)(uint8_t new_val);
    void (*on_action)(void);
} SettingItem;

#include "settings.h"

static const char * const toggle_labels[] = { "OFF", "ON" };
static const char * const ship_cam_labels[] = { "NEW", "OLD" };

static uint8_t action_feedback_timer = 0;

static void stop_music_safe(void) {
    disable_interrupts();
    music_ready = 0;
    NR12_REG = 0; NR14_REG = 0x80;
    NR22_REG = 0; NR24_REG = 0x80;
    NR30_REG = 0;
    NR42_REG = 0; NR44_REG = 0x80;
    enable_interrupts();
}

static void on_music_toggle(uint8_t new_val) {
    if (!new_val) {
        stop_music_safe();
    } else {
        music_play_menu();
        music_ready = 1;
    }
    save_game_data();
}

static void on_legacy_toggle(uint8_t new_val) {
    (void)new_val;
    if (setting_music_enabled) {   // the menu theme of the other driver
        music_play_menu();
        music_ready = 1;
    }
    save_game_data();
}

static void on_sfx_toggle(uint8_t new_val) {
    if (new_val) {
        play_sample_with_music(BANK_SFX_DATA, play_sound_data, PLAY_SOUND_LEN);
    }
    save_game_data();
}

static void on_show_bg_change(uint8_t new_val) {
    if (!new_val) {
        // If BG is off, Parallax is also off
        setting_parallax_enabled = 0;
    }
    save_game_data();
}

static void on_parallax_change(uint8_t new_val) {
    // Parallax requires BG; force back off if BG is disabled
    if (new_val && !setting_show_bg_enabled) {
        setting_parallax_enabled = 0;
        return;
    }
    save_game_data();
}

static void on_setting_change(uint8_t new_val) {
    (void)new_val;
    save_game_data();
}

static void on_wipe_save_action(void) {
    for (uint8_t i = 0; i < NUM_SAVE_LEVELS; i++) {
        level_progress_normal[i] = 0;
        level_progress_practice[i] = 0;
        level_coins[i] = 0;
    }
    save_game_data();
    if (setting_sfx_enabled) {
        play_sample_with_music(BANK_SFX_DATA, quit_sound_data, QUIT_SOUND_LEN);
    }
    action_feedback_timer = 45;
}

static const SettingItem settings_list[] = {
    { "MUSIC",        SETTING_TYPE_TOGGLE, &setting_music_enabled,    0, 1, toggle_labels, on_music_toggle,   NULL },
    { "SFX",          SETTING_TYPE_TOGGLE, &setting_sfx_enabled,      0, 1, toggle_labels, on_sfx_toggle,     NULL },
    // the hUGETracker songs (hUGEDriver) instead of the Furnace ones
    { "LEGACY",       SETTING_TYPE_TOGGLE, &setting_legacy_music,     0, 1, toggle_labels, on_legacy_toggle,  NULL },
    { "GRADIENT",     SETTING_TYPE_TOGGLE, &setting_dmg_gradient,     0, 1, toggle_labels, on_setting_change, NULL },
    { "SHOW BG",      SETTING_TYPE_TOGGLE, &setting_show_bg_enabled,  0, 1, toggle_labels, on_show_bg_change, NULL },
    { "PARALLAX",     SETTING_TYPE_TOGGLE, &setting_parallax_enabled, 0, 1, toggle_labels, on_parallax_change, NULL },
    { "EFFECTS",      SETTING_TYPE_TOGGLE, &setting_effects_enabled,  0, 1, toggle_labels, on_setting_change, NULL },
    { "SHOW %",       SETTING_TYPE_TOGGLE, &setting_show_percent,     0, 1, toggle_labels, on_setting_change, NULL },
    { "SHIP CAM",     SETTING_TYPE_TOGGLE, &setting_old_ship_cam,     0, 1, ship_cam_labels, on_setting_change, NULL },
    { "AUTO CP",      SETTING_TYPE_TOGGLE, &setting_auto_checkpoints, 0, 1, toggle_labels,   on_setting_change, NULL },
    { "WIPE SAVE",    SETTING_TYPE_ACTION, NULL,                      0, 0, NULL,            NULL,              on_wipe_save_action }
};
#define NUM_SETTINGS (sizeof(settings_list) / sizeof(settings_list[0]))
#define VISIBLE_ROWS 5

static void reset_audio_full(void) {
    disable_interrupts();
    stop_sample();
    music_ready = 0;
    sample_playing = 0;
    sample_keeps_music = 0;

    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xFF;

    NR12_REG = 0; NR14_REG = 0x80;
    NR22_REG = 0; NR24_REG = 0x80;
    NR30_REG = 0;
    NR42_REG = 0; NR44_REG = 0x80;

    hUGE_mute_channel(HT_CH1, HT_CH_PLAY);
    hUGE_mute_channel(HT_CH2, HT_CH_PLAY);
    hUGE_mute_channel(HT_CH3, HT_CH_PLAY);
    hUGE_mute_channel(HT_CH4, HT_CH_PLAY);
    hUGE_reset_wave();

    enable_interrupts();
}

static void draw_fixed_text(uint8_t x, uint8_t y, const char *str, uint8_t width) {
    uint8_t len = 0;
    while (*str && len < width) {
        char c = *str++;
        uint8_t tile;
        if (c == ' ') tile = 0;
        else if (c == '%') tile = 1;
        else if (c == '/') tile = 2;
        else if (c >= '0' && c <= '9') tile = (c - '0') + 3;
        else if (c >= 'A' && c <= 'Z') tile = (c - 'A') + 13;
        else if (c >= 'a' && c <= 'z') tile = (c - 'a') + 13;
        else if (c == '<') tile = 39;
        else if (c == '>') tile = 40;
        else if (c == '-') tile = 41;
        else if (c == ':') tile = 42;
        else tile = 0;

        if (tile == 0) {
            set_bkg_tile_xy(x++, y, 0); // Tile 0 is brown interior
        } else {
            set_bkg_tile_xy(x++, y, (uint8_t)(FONT_PUSAB_START + tile));
        }
        len++;
    }
    while (len < width) {
        set_bkg_tile_xy(x++, y, 0);
        len++;
    }
}

static void render_setting_row(uint8_t visible_row, uint8_t item_idx, uint8_t is_selected) {
    uint8_t row_y = 4 + visible_row * 2;
    if (item_idx >= NUM_SETTINGS) {
        for (uint8_t x = 3; x <= 16; x++) set_bkg_tile_xy(x, row_y, 0);
        return;
    }

    const SettingItem *item = &settings_list[item_idx];
    char line_buf[15];
    for (uint8_t i = 0; i < 14; i++) line_buf[i] = ' '; // Clean spaces - NO DOTS!
    line_buf[14] = '\0';

    // Position 0: Cursor
    line_buf[0] = is_selected ? '>' : ' ';

    // Position 1..: Setting name
    uint8_t p = 1;
    const char *n = item->name;
    if (item->type == SETTING_TYPE_ACTION && action_feedback_timer > 0 && is_selected) {
        n = "SAVE WIPED";
    }
    while (*n && p < 13) {
        line_buf[p++] = *n++;
    }

    // Determine value string
    const char *val_str = "";
    if (item->type == SETTING_TYPE_TOGGLE && item->val_ptr) {
        val_str = item->val_labels[*item->val_ptr ? 1 : 0];
    } else if (item->type == SETTING_TYPE_CHOICE && item->val_ptr) {
        val_str = item->val_labels[*item->val_ptr];
    }

    // Right-align val_str at end of line (pos 14 - vlen)
    uint8_t vlen = 0;
    const char *v = val_str;
    while (*v++) vlen++;
    if (vlen > 0 && vlen <= 5) {
        uint8_t start_pos = (uint8_t)(14 - vlen);
        v = val_str;
        for (uint8_t i = 0; i < vlen; i++) {
            line_buf[start_pos + i] = *v++;
        }
    }

    draw_fixed_text(3, row_y, line_buf, 14);
}

static void render_all_settings(uint8_t current_sel, uint8_t scroll_offset) {
    for (uint8_t r = 0; r < VISIBLE_ROWS; r++) {
        uint8_t idx = scroll_offset + r;
        render_setting_row(r, idx, (idx == current_sel));
    }
}

GameState update_settings_state(void) BANKED {
    // load with the display on (off shows white); fade_from_black shows the palettes
    fade_set_black();
    fade_hold = 1;
    HIDE_SPRITES;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
    HIDE_WIN;

    SCX_REG = 0;
    SCY_REG = 0;

    reset_audio_full();

    fill_bkg_rect(0, 0, 32, 32, 0);

    // Load background tiles & tilemap
    set_bkg_data(0, SETTINGS_BG_TILE_COUNT, settings_bg_tiles);
    set_bkg_tiles(0, 0, SETTINGS_BG_WIDTH, SETTINGS_BG_HEIGHT, settings_bg_map);

    // white Pusab font
    setup_menu_font();
    set_bkg_data((uint8_t)(FONT_PUSAB_START + 39), 4, extra_font_tiles);

    if (_cpu == CGB_TYPE) {
        palette_color_t pals[32];   // fade_set_bkg_palette is banked: a RAM copy
        for (uint8_t i = 0; i < 32; i++) pals[i] = settings_bg_palettes[i];
        fade_set_bkg_palette(0, 8, pals);
        set_bkg_attributes(0, 0, SETTINGS_BG_WIDTH, SETTINGS_BG_HEIGHT, settings_bg_attributes);
    }

    fade_set_dmg_palettes(0xE4, 0xE4, 0xE4);

    uint8_t current_sel = 0;
    uint8_t scroll_offset = 0;
    action_feedback_timer = 0;

    render_all_settings(current_sel, scroll_offset);

    SHOW_BKG;
    DISPLAY_ON;
    fade_from_black(2);

    uint8_t prev_joy = joypad();

    while (1) {
        wait_vbl_done();

        if (action_feedback_timer > 0) {
            action_feedback_timer--;
            if (action_feedback_timer == 0) {
                render_all_settings(current_sel, scroll_offset);
            }
        }

        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;

        // Navigation Up / Down
        if (pressed & J_UP) {
            if (current_sel > 0) {
                current_sel--;
            } else {
                current_sel = NUM_SETTINGS - 1;
            }
            if (current_sel < scroll_offset) {
                scroll_offset = current_sel;
            } else if (current_sel >= scroll_offset + VISIBLE_ROWS) {
                scroll_offset = (uint8_t)(current_sel - VISIBLE_ROWS + 1);
            }
            render_all_settings(current_sel, scroll_offset);
        } else if (pressed & J_DOWN) {
            if (current_sel < NUM_SETTINGS - 1) {
                current_sel++;
            } else {
                current_sel = 0;
            }
            if (current_sel < scroll_offset) {
                scroll_offset = current_sel;
            } else if (current_sel >= scroll_offset + VISIBLE_ROWS) {
                scroll_offset = (uint8_t)(current_sel - VISIBLE_ROWS + 1);
            }
            render_all_settings(current_sel, scroll_offset);
        }

        // Toggle / Action on Left / Right / A
        if (pressed & (J_LEFT | J_RIGHT | J_A)) {
            const SettingItem *item = &settings_list[current_sel];
            if (item->type == SETTING_TYPE_TOGGLE && item->val_ptr) {
                // Parallax requires SHOW BG: ignore input when BG is off
                if (item->val_ptr == &setting_parallax_enabled && !setting_show_bg_enabled) {
                    // Keep forced OFF, just re-render to confirm
                    setting_parallax_enabled = 0;
                    render_setting_row(current_sel - scroll_offset, current_sel, 1);
                } else {
                    *item->val_ptr ^= 1;
                    if (item->on_change) {
                        item->on_change(*item->val_ptr);
                    }
                    // SHOW BG off forces parallax off: refresh list so both rows update
                    if (item->val_ptr == &setting_show_bg_enabled) {
                        render_all_settings(current_sel, scroll_offset);
                    } else {
                        render_setting_row(current_sel - scroll_offset, current_sel, 1);
                    }
                }
            } else if (item->type == SETTING_TYPE_CHOICE && item->val_ptr) {
                if (pressed & J_LEFT) {
                    if (*item->val_ptr > item->min_val) (*item->val_ptr)--;
                    else *item->val_ptr = item->max_val;
                } else {
                    if (*item->val_ptr < item->max_val) (*item->val_ptr)++;
                    else *item->val_ptr = item->min_val;
                }
                if (item->on_change) {
                    item->on_change(*item->val_ptr);
                }
                render_setting_row(current_sel - scroll_offset, current_sel, 1);
            } else if (item->type == SETTING_TYPE_ACTION && (pressed & J_A)) {
                if (item->on_action) {
                    item->on_action();
                    render_setting_row(current_sel - scroll_offset, current_sel, 1);
                }
            }
        }

        // Exit on B or START
        if (pressed & (J_B | J_START)) {
            fade_capture_current();
            fade_to_black(2);
            if (setting_music_enabled && !music_ready) {
                music_play_menu();
                music_ready = 1;
            } else if (!setting_music_enabled) {
                stop_music_safe();
            }

            HIDE_SPRITES;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            SCX_REG = 0;
            SCY_REG = 0;
            return STATE_MENU;
        }
    }
}
