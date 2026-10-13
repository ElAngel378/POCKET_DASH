#pragma bank 26

#include <gb/gb.h>
#include "music.h"
#include <gb/cgb.h>
#include <stdint.h>
#include "states.h"
#include "gameplay.h"
#include "assets.h"
#include "sample_player.h"
#include "sfx_data.h"
#include "level_complete_sfx.h"
#include "fade.h"
#include "hUGEDriver.h"
#include "collision.h"
#include "gbc_palettes.h"
#include "settings.h"

extern volatile uint8_t current_song_bank;
extern volatile uint8_t current_music_divider;
extern uint8_t music_ready;
extern const hUGESong_t menuloop;
extern const hUGESong_t practice;

typedef struct {
    const char *name;
    const hUGESong_t *song;
    uint8_t bank;
    uint8_t divider;
    uint8_t fur;        // Furnace song id (FUR_NONE: the hUGETracker song above)
} MusicEntry;

typedef enum {
    SFX_SAMPLE,
    SFX_NOISE
} SfxKind;

typedef struct {
    const char *name;
    SfxKind kind;
    uint8_t bank;
    const uint8_t *sample_ptr;
    uint16_t sample_len;
} SfxItem;

static const SfxItem sfx_items[4] = {
    { "PLAY SOUND",     SFX_SAMPLE, BANK_SFX_DATA,            play_sound_data,        PLAY_SOUND_LEN },
    { "QUIT SOUND",     SFX_SAMPLE, BANK_SFX_DATA,            quit_sound_data,        QUIT_SOUND_LEN },
    { "LEVEL COMPLETE", SFX_SAMPLE, BANK_LEVEL_COMPLETE_SFX, level_complete_sfx_data, LEVEL_COMPLETE_SFX_LEN },
    { "DEATH CRASH",    SFX_NOISE,  0,                        NULL,                   0 }
};

#define NUM_SFX_ITEMS 4
// Every Furnace song first (music/Furnace, tools/convert_furnace.py); the hUGETracker ones after
// them (menu theme + the 12 level songs (level_test is silent, so not listed) + practice mode)
// only with the LEGACY setting, or once SELECT has been held for HUGE_UNLOCK_FRAMES
#define NUM_LEVEL_SONGS 12
#define NUM_HUGE_TRACKS (NUM_LEVEL_SONGS + 2)
#define HUGE_UNLOCK_FRAMES 150
#define PRACTICE_TRACK (NUM_LEVEL_SONGS + 1)
static const char * const fur_names[] = FUR_SONG_NAMES;

// Custom font tiles: < (39), > (40), - (41), : (42)
static const uint8_t extra_font_tiles[4 * 16] = {
    // 39: Left arrow <
    0x00,0x08, 0x08,0x1C, 0x18,0x3C, 0x38,0x7C, 0x18,0x3C, 0x08,0x1C, 0x00,0x08, 0x00,0x00,
    // 40: Right arrow >
    0x00,0x10, 0x10,0x38, 0x18,0x3C, 0x1C,0x3E, 0x18,0x3C, 0x10,0x38, 0x00,0x10, 0x00,0x00,
    // 41: Dash -
    0x00,0x00, 0x00,0x00, 0x00,0x7C, 0x38,0x7C, 0x00,0x7C, 0x00,0x00, 0x00,0x00, 0x00,0x00,
    // 42: Colon :
    0x00,0x00, 0x00,0x30, 0x10,0x38, 0x00,0x30, 0x00,0x30, 0x10,0x38, 0x00,0x30, 0x00,0x00
};

// 8x16 cursor sprite (right pointing arrow ►: white body, black border)
static const uint8_t cursor_sprite_tiles[32] = {
    // Tile 0 (top 8x8: right-pointing arrow)
    0x20, 0x20,
    0x30, 0x30,
    0x38, 0x28,
    0x3C, 0x24,
    0x3C, 0x24,
    0x38, 0x28,
    0x30, 0x30,
    0x20, 0x20,
    // Tile 1 (bottom 8x8: blank)
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void stop_music_safe(void) {
    disable_interrupts();
    music_ready = 0;
    NR12_REG = 0; NR14_REG = 0x80;
    NR22_REG = 0; NR24_REG = 0x80;
    NR30_REG = 0;
    NR42_REG = 0; NR44_REG = 0x80;
    enable_interrupts();
}

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
        set_bkg_tile_xy(x++, y, (uint8_t)(FONT_PUSAB_START + tile));
        len++;
    }
    while (len < width) {
        set_bkg_tile_xy(x++, y, (uint8_t)FONT_PUSAB_START);
        len++;
    }
}

static void get_music_entry(uint8_t idx, MusicEntry *entry) {
    entry->fur = FUR_NONE;
    if (idx < FUR_NUM_SONGS) {
        entry->fur = idx;
        entry->name = fur_names[idx];
        entry->song = 0;
        entry->bank = 0;
        return;
    }
    idx -= FUR_NUM_SONGS;   // the hUGETracker songs
    if (idx == 0) {
        entry->name = "MENU THEME";
        entry->song = &menuloop;
        entry->bank = 1;
        entry->divider = 176;
    } else if (idx == PRACTICE_TRACK) {
        entry->name = "PRACTICE MODE";
        entry->song = &practice;
        entry->bank = 212;
        entry->divider = 187;   // PRACTICE_MUSIC_DIVIDER in gameplay.c
    } else {
        uint8_t lvl_idx = idx - 1;
        entry->name = game_levels[lvl_idx]->name;
        entry->song = level_songs[lvl_idx];
        entry->bank = song_bank[lvl_idx];
        entry->divider = game_levels[lvl_idx]->timer_divider;
    }
}

static void render_music_section(uint8_t track_idx, uint8_t playing, uint8_t total) {
    MusicEntry me;
    get_music_entry(track_idx, &me);

    // Number & Status: "< 01/12 > STOPPED" or "< 01/12 > PLAYING"
    char num_buf[19];
    uint8_t num = track_idx + 1;
    num_buf[0] = '<';
    num_buf[1] = ' ';
    num_buf[2] = (char)('0' + (num / 10));
    num_buf[3] = (char)('0' + (num % 10));
    num_buf[4] = '/';
    num_buf[5] = (char)('0' + (total / 10));
    num_buf[6] = (char)('0' + (total % 10));
    num_buf[7] = ' ';
    num_buf[8] = '>';
    num_buf[9] = ' ';
    if (playing) {
        num_buf[10] = 'P'; num_buf[11] = 'L'; num_buf[12] = 'A';
        num_buf[13] = 'Y'; num_buf[14] = 'I'; num_buf[15] = 'N';
        num_buf[16] = 'G'; num_buf[17] = '\0';
    } else {
        num_buf[10] = 'S'; num_buf[11] = 'T'; num_buf[12] = 'O';
        num_buf[13] = 'P'; num_buf[14] = 'P'; num_buf[15] = 'E';
        num_buf[16] = 'D'; num_buf[17] = '\0';
    }
    draw_fixed_text(2, 5, num_buf, 17);

    // Track name (row 6, cols 2..19) and which tracker it is from (row 7)
    draw_fixed_text(2, 6, me.name, 18);
    draw_fixed_text(2, 7, me.fur == FUR_NONE ? "HUGETRACKER" : "FURNACE", 11);
}

static void render_sfx_section(uint8_t sfx_idx) {
    const SfxItem *item = &sfx_items[sfx_idx % NUM_SFX_ITEMS];

    // Number indicator: "< 01/04 >"
    char num_buf[10];
    uint8_t num = sfx_idx + 1;
    num_buf[0] = '<';
    num_buf[1] = ' ';
    num_buf[2] = '0';
    num_buf[3] = (char)('0' + num);
    num_buf[4] = '/';
    num_buf[5] = '0';
    num_buf[6] = '4';
    num_buf[7] = ' ';
    num_buf[8] = '>';
    num_buf[9] = '\0';
    draw_fixed_text(2, 10, num_buf, 10);

    // SFX name (row 11, width 16 columns: cols 2..17)
    draw_fixed_text(2, 11, item->name, 16);
}

static void update_cursor_sprite(uint8_t active_section) {
    // Arrow at screen X = 4 (OAM X = 12)
    // Section 0 (Music): row 4 (screen Y = 32 -> OAM Y = 48)
    // Section 1 (SFX): row 9 (screen Y = 72 -> OAM Y = 88)
    uint8_t cy = (active_section == 0) ? 48 : 88;
    move_sprite(0, 12, cy);
    set_sprite_tile(0, 0);
    set_sprite_prop(0, 0);
    for (uint8_t s = 1; s < 40; s++) hide_sprite(s);
}

static void update_section_highlights(uint8_t active_section) {
    if (_cpu != CGB_TYPE) return;
    VBK_REG = 1;
    uint8_t pal_music = (active_section == 0) ? 1 : 2;
    uint8_t pal_sfx   = (active_section == 1) ? 1 : 2;

    fill_bkg_rect(0, 4, 20, 4, pal_music); // rows 4-7: Level Music
    fill_bkg_rect(0, 9, 20, 3, pal_sfx);   // rows 9, 10, 11: Sound Effects
    VBK_REG = 0;
}

static void play_current_music(uint8_t track_idx) {
    MusicEntry me;
    get_music_entry(track_idx, &me);
    if (me.fur != FUR_NONE) {
        music_play_fur(me.fur);
        music_ready = 1;
    } else if (me.song && me.bank) {
        init_music_banked(me.song, me.bank, me.divider);
        current_song_bank = me.bank;
        music_ready = 1;
    }
}

static void play_current_sfx(uint8_t sfx_idx, uint8_t music_on) {
    const SfxItem *item = &sfx_items[sfx_idx % NUM_SFX_ITEMS];
    if (item->kind == SFX_SAMPLE) {
        stop_sample();
        if (music_on) {
            play_sample_with_music(item->bank, item->sample_ptr, item->sample_len);
        } else {
            play_sample(item->bank, item->sample_ptr, item->sample_len);
        }
    } else {
        NR41_REG = 0x00;
        NR42_REG = 0xF2;
        NR43_REG = 0x43;
        NR44_REG = 0x80;
    }
}

GameState update_music_test_state(void) BANKED {
    // Load behind the black screen the last state faded to, display on (a display switched off
    // shows white): the palettes are only stored until fade_from_black
    fade_set_black();
    fade_hold = 1;
    HIDE_SPRITES;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
    HIDE_WIN;

    SCX_REG = 0;
    SCY_REG = 0;

    // Clear background
    fill_bkg_rect(0, 0, 32, 32, 0);

    // Stop and fully reset sound system on entry
    reset_audio_full();

    // Load font and extra tiles
    setup_menu_font();
    set_bkg_data((uint8_t)(FONT_PUSAB_START + 39), 4, extra_font_tiles);

    // Setup cursor sprite (right-pointing arrow ►)
    SPRITES_8x16;
    set_sprite_data(0, 2, cursor_sprite_tiles);

    // Vibrant themed CGB palettes with pure white font fill for maximum readability
    if (_cpu == CGB_TYPE) {
        static const palette_color_t music_test_pals[16] = {
            // Pal 0: General UI / Separators / Help (Dark BG, Deep Cyan, Bright Cyan, Pure White)
            RGB8(15, 20, 35), RGB8(0, 130, 180), RGB8(60, 210, 255), RGB8(255, 255, 255),
            // Pal 1: Active Section Highlight (Dark BG, Warm Amber, Bright Gold/Yellow, Pure White)
            RGB8(15, 20, 35), RGB8(210, 140, 20), RGB8(255, 225, 40), RGB8(255, 255, 255),
            // Pal 2: Inactive Section (Dark BG, Dim Slate, Light Slate, Soft White)
            RGB8(15, 20, 35), RGB8(60, 75, 105), RGB8(110, 135, 175), RGB8(200, 215, 235),
            // Pal 3: Title Header (Dark BG, Forest Green, Vibrant Mint Green, Pure White)
            RGB8(15, 20, 35), RGB8(20, 150, 70), RGB8(60, 245, 120), RGB8(255, 255, 255)
        };
        palette_color_t pals[16];   // fade_set_* are banked: RAM copies
        for (uint8_t i = 0; i < 16; i++) pals[i] = music_test_pals[i];
        fade_set_bkg_palette(0, 4, pals);

        static const palette_color_t cursor_pal[4] = {
            RGB8(255, 255, 255), // 0: trans
            RGB8(255, 255, 255), // 1: white
            RGB8(255, 255, 255), // 2: white
            RGB8(0, 0, 0)        // 3: black
        };
        for (uint8_t i = 0; i < 4; i++) pals[i] = cursor_pal[i];
        fade_set_sprite_palette(0, 1, pals);

        VBK_REG = 1;
        fill_bkg_rect(0, 0, 32, 32, 0); // Default to Pal 0 (Cyan / White)
        fill_bkg_rect(0, 1, 20, 1, 3);   // Title row 1: Pal 3 (Mint Green)
        VBK_REG = 0;
    }

    fade_set_dmg_palettes(0xE4, 0xD2, 0xE4);

    // Static UI
    draw_fixed_text(3, 1, "MUSIC AND SFX", 14);
    draw_fixed_text(0, 2, "--------------------", 20);

    draw_fixed_text(2, 4, "LEVEL MUSIC", 11);

    draw_fixed_text(2, 9, "SOUND EFFECTS", 13);

    draw_fixed_text(0, 13, "--------------------", 20);
    draw_fixed_text(1, 14, "LEFT/RIGHT: SELECT", 18);
    draw_fixed_text(1, 15, "UP/DOWN: SECTION", 16);
    draw_fixed_text(1, 16, "A:PLAY/STOP  B:BACK", 19);

    uint8_t current_section = 0; // 0 = Music, 1 = SFX
    uint8_t current_music = 0;
    uint8_t current_sfx = 0;
    uint8_t music_playing = 0;
    uint8_t show_huge = setting_legacy_music;    // the hUGETracker songs are listed
    uint8_t num_tracks = FUR_NUM_SONGS + (show_huge ? NUM_HUGE_TRACKS : 0);
    uint8_t select_held = 0;

    render_music_section(current_music, music_playing, num_tracks);
    render_sfx_section(current_sfx);
    update_cursor_sprite(current_section);
    update_section_highlights(current_section);

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;
    fade_from_black(2);

    uint8_t prev_joy = joypad();
    uint8_t hold_timer = 0;

    while (1) {
        wait_vbl_done();

        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;

        // Auto-repeat when holding left or right
        uint8_t repeat = 0;
        if (joy & (J_LEFT | J_RIGHT)) {
            hold_timer++;
            if (hold_timer >= 18 && (hold_timer % 7) == 0) {
                repeat = 1;
            }
        } else {
            hold_timer = 0;
        }

        // Holding SELECT for a while lists the hUGETracker songs too
        if (!show_huge) {
            if (joy & J_SELECT) {
                if (++select_held >= HUGE_UNLOCK_FRAMES) {
                    show_huge = 1;
                    num_tracks = FUR_NUM_SONGS + NUM_HUGE_TRACKS;
                    render_music_section(current_music, music_playing, num_tracks);
                }
            } else {
                select_held = 0;
            }
        }

        // Section switching
        if (pressed & (J_UP | J_DOWN)) {
            current_section ^= 1;
            update_cursor_sprite(current_section);
            update_section_highlights(current_section);
        }

        // Left / Right switching within current section
        if ((pressed & J_LEFT) || (repeat && (joy & J_LEFT))) {
            if (current_section == 0) {
                if (current_music > 0) current_music--;
                else current_music = num_tracks - 1;
                if (music_playing) {
                    stop_music_safe();
                    music_playing = 0;
                }
                render_music_section(current_music, music_playing, num_tracks);
            } else {
                if (current_sfx > 0) current_sfx--;
                else current_sfx = NUM_SFX_ITEMS - 1;
                render_sfx_section(current_sfx);
            }
        } else if ((pressed & J_RIGHT) || (repeat && (joy & J_RIGHT))) {
            if (current_section == 0) {
                if (current_music < num_tracks - 1) current_music++;
                else current_music = 0;
                if (music_playing) {
                    stop_music_safe();
                    music_playing = 0;
                }
                render_music_section(current_music, music_playing, num_tracks);
            } else {
                if (current_sfx < NUM_SFX_ITEMS - 1) current_sfx++;
                else current_sfx = 0;
                render_sfx_section(current_sfx);
            }
        }

        // A button:
        // On LEVEL MUSIC: toggles START / STOP
        // On SFX: plays the selected sound effect
        if (pressed & J_A) {
            if (current_section == 0) {
                if (music_playing) {
                    stop_music_safe();
                    music_playing = 0;
                } else {
                    stop_sample();
                    play_current_music(current_music);
                    music_playing = 1;
                }
                render_music_section(current_music, music_playing, num_tracks);
            } else {
                play_current_sfx(current_sfx, music_playing);
            }
        }

        // B button: Exit to main menu
        if (pressed & (J_B | J_START)) {
            fade_capture_current();
            fade_to_black(2);
            reset_audio_full();

            // Restore menu loop song if enabled
            if (setting_music_enabled) {
                music_play_menu();
            }

            HIDE_SPRITES;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            SCX_REG = 0;
            SCY_REG = 0;
            return STATE_MENU;
        }
    }
}
