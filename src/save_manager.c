#pragma bank 23

#include "save_manager.h"
#include <gb/gb.h>

BANKREF(save_manager)

#define SRAM_BASE_ADDR ((volatile uint8_t *)0xA000)
#define SAVE_MAGIC_0 'P'
#define SAVE_MAGIC_1 'D'
#define SAVE_MAGIC_2 'S'
#define SAVE_MAGIC_3 'H'
#define SAVE_VERSION 10
// Level count of the version 3 layout (progress arrays were 11 entries long)
#define V3_NUM_SAVE_LEVELS 11
#define NUM_SETTINGS_BYTES 6

#include "settings.h"
#include "icon_catalog.h"

uint8_t level_progress_normal[NUM_SAVE_LEVELS] = {0};
uint8_t level_progress_practice[NUM_SAVE_LEVELS] = {0};
uint8_t level_coins[NUM_SAVE_LEVELS] = {0};

uint8_t selected_icon = 0;
uint8_t selected_color_primary = 0;
uint8_t selected_color_secondary = 1;
uint8_t selected_dmg_primary = 2;     // DMG shades (0 white .. 3 black): the original cube's
uint8_t selected_dmg_secondary = 1;

// Version 4 layout: progress normal[N], practice[N], settings[6], coins[N], checksum
#define V4_COINS_OFS (5 + NUM_SAVE_LEVELS * 2 + NUM_SETTINGS_BYTES)
#define V4_DATA_LEN  (NUM_SAVE_LEVELS * 3 + NUM_SETTINGS_BYTES)
// Version 5: version 4 + one more settings byte (SHOW %) before the checksum
#define V5_SHOW_PCT_OFS (5 + V4_DATA_LEN)
#define V5_DATA_LEN  (V4_DATA_LEN + 1)
// Version 6: version 5 + SHIP CAM
#define V6_SHIP_CAM_OFS (5 + V5_DATA_LEN)
#define V6_DATA_LEN  (V5_DATA_LEN + 1)
// Version 7: version 6 + AUTO CP
#define V7_AUTO_CP_OFS (5 + V6_DATA_LEN)
#define V7_DATA_LEN  (V6_DATA_LEN + 1)
// Version 8: version 7 + 3 customization bytes (selected_icon, selected_color_primary, selected_color_secondary)
#define V8_ICON_OFS  (5 + V7_DATA_LEN)
#define V8_COL1_OFS  (V8_ICON_OFS + 1)
#define V8_COL2_OFS  (V8_ICON_OFS + 2)
#define V8_DATA_LEN  (V7_DATA_LEN + 3)
// Version 9: version 8 + 2 bytes: DMG primary / secondary shade. Icon numbers changed (Famidash
// icons): a version 8 icon is reset to the original cube.
#define V9_DMG1_OFS  (5 + V8_DATA_LEN)
#define V9_DMG2_OFS  (V9_DMG1_OFS + 1)
#define V9_DATA_LEN  (V8_DATA_LEN + 2)
// Version 10: version 9 + FURNACE (play every song that has a Furnace version with it)
#define V10_FURNACE_OFS (5 + V9_DATA_LEN)
#define V10_DATA_LEN (V9_DATA_LEN + 1)

uint8_t setting_music_enabled   = 1;
uint8_t setting_sfx_enabled     = 1;
uint8_t setting_dmg_gradient    = 1;
uint8_t setting_show_bg_enabled = 1;
uint8_t setting_parallax_enabled = 1;
uint8_t setting_effects_enabled = 1;
uint8_t setting_show_percent    = 1;
uint8_t setting_old_ship_cam    = 0;
uint8_t setting_auto_checkpoints = 1;
// setting_furnace_music is in HRAM (src/furnace/music_hram.s)

static uint8_t calc_checksum(const uint8_t *data, uint8_t len) {
    uint8_t sum = 0x5A;
    for (uint8_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + data[i] + (uint8_t)(i * 7));
    }
    return sum;
}

void init_save_system(void) BANKED {
    setting_furnace_music = 0;   // HRAM: not initialized at boot; older saves don't have it
    ENABLE_RAM;
    volatile uint8_t *sram = SRAM_BASE_ADDR;

    // Check magic signature and version
    if (sram[0] == SAVE_MAGIC_0 && sram[1] == SAVE_MAGIC_1 &&
        sram[2] == SAVE_MAGIC_2 && sram[3] == SAVE_MAGIC_3) {

        if (sram[4] == 10 || sram[4] == 9 || sram[4] == 8 || sram[4] == 7 || sram[4] == 6 || sram[4] == 5 || sram[4] == 4) {
            uint8_t v5 = (sram[4] >= 5);
            uint8_t v6 = (sram[4] >= 6);
            uint8_t v7 = (sram[4] >= 7);
            uint8_t v8 = (sram[4] >= 8);
            uint8_t v9 = (sram[4] >= 9);
            uint8_t v10 = (sram[4] == 10);
            uint8_t len = v10 ? V10_DATA_LEN : v9 ? V9_DATA_LEN : v8 ? V8_DATA_LEN : (v7 ? V7_DATA_LEN : (v6 ? V6_DATA_LEN : (v5 ? V5_DATA_LEN : V4_DATA_LEN)));
            uint8_t chk = calc_checksum((const uint8_t *)&sram[5], len);
            if (sram[5 + len] == chk) {
                for (uint8_t i = 0; i < NUM_SAVE_LEVELS; i++) {
                    level_coins[i] = sram[V4_COINS_OFS + i] & 7;
                    level_progress_normal[i] = sram[5 + i];
                    level_progress_practice[i] = sram[5 + NUM_SAVE_LEVELS + i];
                    if (level_progress_normal[i] > 100) level_progress_normal[i] = 100;
                    if (level_progress_practice[i] > 100) level_progress_practice[i] = 100;
                }
                setting_music_enabled    = sram[5 + NUM_SAVE_LEVELS * 2 + 0] ? 1 : 0;
                setting_sfx_enabled      = sram[5 + NUM_SAVE_LEVELS * 2 + 1] ? 1 : 0;
                setting_dmg_gradient     = sram[5 + NUM_SAVE_LEVELS * 2 + 2] ? 1 : 0;
                setting_show_bg_enabled  = sram[5 + NUM_SAVE_LEVELS * 2 + 3] ? 1 : 0;
                setting_parallax_enabled = sram[5 + NUM_SAVE_LEVELS * 2 + 4] ? 1 : 0;
                setting_effects_enabled  = sram[5 + NUM_SAVE_LEVELS * 2 + 5] ? 1 : 0;
                setting_show_percent     = v5 ? (sram[V5_SHOW_PCT_OFS] ? 1 : 0) : 1;
                setting_old_ship_cam     = v6 ? (sram[V6_SHIP_CAM_OFS] ? 1 : 0) : 0;
                setting_auto_checkpoints = v7 ? (sram[V7_AUTO_CP_OFS] ? 1 : 0) : 1;
                selected_icon            = v9 ? sram[V8_ICON_OFS] : 0;
                selected_color_primary   = v8 ? sram[V8_COL1_OFS] : 0;
                selected_color_secondary = v8 ? sram[V8_COL2_OFS] : 1;
                selected_dmg_primary     = v9 ? sram[V9_DMG1_OFS] : 2;
                selected_dmg_secondary   = v9 ? sram[V9_DMG2_OFS] : 1;
                setting_furnace_music    = v10 ? (sram[V10_FURNACE_OFS] ? 1 : 0) : 0;
                if (selected_icon >= NUM_CUBE_ICONS) selected_icon = 0;
                if (selected_dmg_primary > 3) selected_dmg_primary = 2;
                if (selected_dmg_secondary > 3) selected_dmg_secondary = 1;
                if (selected_color_primary >= 12) selected_color_primary = 0;
                if (selected_color_secondary >= 12) selected_color_secondary = 1;
                if (!setting_show_bg_enabled) setting_parallax_enabled = 0;
                DISABLE_RAM;
                if (!v10) save_game_data();   // upgrade to version 10
                return;
            }
        } else if (sram[4] == 3) {
            // Upgrade from Version 3 (11 levels, 6 settings bytes)
            uint8_t chk = calc_checksum((const uint8_t *)&sram[5], V3_NUM_SAVE_LEVELS * 2 + NUM_SETTINGS_BYTES);
            if (sram[5 + V3_NUM_SAVE_LEVELS * 2 + NUM_SETTINGS_BYTES] == chk) {
                for (uint8_t i = 0; i < V3_NUM_SAVE_LEVELS; i++) {
                    level_progress_normal[i] = sram[5 + i];
                    level_progress_practice[i] = sram[5 + V3_NUM_SAVE_LEVELS + i];
                    if (level_progress_normal[i] > 100) level_progress_normal[i] = 100;
                    if (level_progress_practice[i] > 100) level_progress_practice[i] = 100;
                }
                setting_music_enabled    = sram[5 + V3_NUM_SAVE_LEVELS * 2 + 0] ? 1 : 0;
                setting_sfx_enabled      = sram[5 + V3_NUM_SAVE_LEVELS * 2 + 1] ? 1 : 0;
                setting_dmg_gradient     = sram[5 + V3_NUM_SAVE_LEVELS * 2 + 2] ? 1 : 0;
                setting_show_bg_enabled  = sram[5 + V3_NUM_SAVE_LEVELS * 2 + 3] ? 1 : 0;
                setting_parallax_enabled = sram[5 + V3_NUM_SAVE_LEVELS * 2 + 4] ? 1 : 0;
                setting_effects_enabled  = sram[5 + V3_NUM_SAVE_LEVELS * 2 + 5] ? 1 : 0;
                if (!setting_show_bg_enabled) setting_parallax_enabled = 0;
                DISABLE_RAM;
                save_game_data();
                return;
            }
        } else if (sram[4] == 2) {
            // Upgrade from Version 2 (5 settings bytes, no SHOW BG)
            uint8_t chk = calc_checksum((const uint8_t *)&sram[5], NUM_SAVE_LEVELS * 2 + 5);
            if (sram[5 + NUM_SAVE_LEVELS * 2 + 5] == chk) {
                for (uint8_t i = 0; i < NUM_SAVE_LEVELS; i++) {
                    level_progress_normal[i] = sram[5 + i];
                    level_progress_practice[i] = sram[5 + NUM_SAVE_LEVELS + i];
                    if (level_progress_normal[i] > 100) level_progress_normal[i] = 100;
                    if (level_progress_practice[i] > 100) level_progress_practice[i] = 100;
                }
                setting_music_enabled    = sram[5 + NUM_SAVE_LEVELS * 2 + 0] ? 1 : 0;
                setting_sfx_enabled      = sram[5 + NUM_SAVE_LEVELS * 2 + 1] ? 1 : 0;
                setting_dmg_gradient     = sram[5 + NUM_SAVE_LEVELS * 2 + 2] ? 1 : 0;
                setting_parallax_enabled = sram[5 + NUM_SAVE_LEVELS * 2 + 3] ? 1 : 0;
                setting_effects_enabled  = sram[5 + NUM_SAVE_LEVELS * 2 + 4] ? 1 : 0;
                setting_show_bg_enabled  = 1;
                DISABLE_RAM;
                save_game_data();
                return;
            }
        } else if (sram[4] == 1) {
            // Upgrade from Version 1
            uint8_t chk = calc_checksum((const uint8_t *)&sram[5], NUM_SAVE_LEVELS * 2);
            if (sram[5 + NUM_SAVE_LEVELS * 2] == chk) {
                for (uint8_t i = 0; i < NUM_SAVE_LEVELS; i++) {
                    level_progress_normal[i] = sram[5 + i];
                    level_progress_practice[i] = sram[5 + NUM_SAVE_LEVELS + i];
                    if (level_progress_normal[i] > 100) level_progress_normal[i] = 100;
                    if (level_progress_practice[i] > 100) level_progress_practice[i] = 100;
                }
                DISABLE_RAM;
                save_game_data();
                return;
            }
        }
    }

    // Default to 0 on first boot or corrupt save
    for (uint8_t i = 0; i < NUM_SAVE_LEVELS; i++) {
        level_progress_normal[i] = 0;
        level_progress_practice[i] = 0;
        level_coins[i] = 0;
    }
    setting_music_enabled   = 1;
    setting_sfx_enabled     = 1;
    setting_dmg_gradient    = 1;
    setting_show_bg_enabled = 1;
    setting_parallax_enabled = 1;
    setting_effects_enabled = 1;
    setting_show_percent    = 1;
    setting_old_ship_cam    = 0;
    setting_auto_checkpoints = 1;
    setting_furnace_music   = 0;
    selected_icon           = 0;
    selected_color_primary  = 0;
    selected_color_secondary = 1;
    selected_dmg_primary    = 2;
    selected_dmg_secondary  = 1;
    DISABLE_RAM;
    save_game_data();
}

void save_game_data(void) BANKED {
    ENABLE_RAM;
    volatile uint8_t *sram = SRAM_BASE_ADDR;

    sram[0] = SAVE_MAGIC_0;
    sram[1] = SAVE_MAGIC_1;
    sram[2] = SAVE_MAGIC_2;
    sram[3] = SAVE_MAGIC_3;
    sram[4] = SAVE_VERSION;

    for (uint8_t i = 0; i < NUM_SAVE_LEVELS; i++) {
        sram[5 + i] = level_progress_normal[i];
        sram[5 + NUM_SAVE_LEVELS + i] = level_progress_practice[i];
    }

    sram[5 + NUM_SAVE_LEVELS * 2 + 0] = setting_music_enabled;
    sram[5 + NUM_SAVE_LEVELS * 2 + 1] = setting_sfx_enabled;
    sram[5 + NUM_SAVE_LEVELS * 2 + 2] = setting_dmg_gradient;
    sram[5 + NUM_SAVE_LEVELS * 2 + 3] = setting_show_bg_enabled;
    sram[5 + NUM_SAVE_LEVELS * 2 + 4] = setting_parallax_enabled;
    sram[5 + NUM_SAVE_LEVELS * 2 + 5] = setting_effects_enabled;

    for (uint8_t i = 0; i < NUM_SAVE_LEVELS; i++) {
        sram[V4_COINS_OFS + i] = level_coins[i];
    }

    sram[V5_SHOW_PCT_OFS] = setting_show_percent;
    sram[V6_SHIP_CAM_OFS] = setting_old_ship_cam;
    sram[V7_AUTO_CP_OFS] = setting_auto_checkpoints;
    sram[V8_ICON_OFS] = selected_icon;
    sram[V8_COL1_OFS] = selected_color_primary;
    sram[V8_COL2_OFS] = selected_color_secondary;
    sram[V9_DMG1_OFS] = selected_dmg_primary;
    sram[V9_DMG2_OFS] = selected_dmg_secondary;
    sram[V10_FURNACE_OFS] = setting_furnace_music;
    sram[5 + V10_DATA_LEN] = calc_checksum((const uint8_t *)&sram[5], V10_DATA_LEN);
    DISABLE_RAM;
}

void record_level_progress(uint8_t level_idx, uint8_t pct, uint8_t is_practice) BANKED {
    if (level_idx >= NUM_SAVE_LEVELS) return;
    if (pct > 100) pct = 100;

    if (is_practice) {
        if (pct > level_progress_practice[level_idx]) {
            level_progress_practice[level_idx] = pct;
            save_game_data();
        }
    } else {
        if (pct > level_progress_normal[level_idx]) {
            level_progress_normal[level_idx] = pct;
            save_game_data();
        }
    }
}

void record_level_coins(uint8_t level_idx, uint8_t coins) BANKED {
    if (level_idx >= NUM_SAVE_LEVELS) return;
    if ((level_coins[level_idx] | coins) != level_coins[level_idx]) {
        level_coins[level_idx] |= coins;
        save_game_data();
    }
}

void record_level_progress_from_cam(uint8_t level_idx, uint16_t cam_x, uint16_t max_x, uint8_t is_practice) BANKED {
    if (level_idx >= NUM_SAVE_LEVELS) return;
    uint8_t pct;
    if (max_x == 0) pct = 0;
    else if (cam_x >= max_x) pct = 100;
    else {
        uint32_t p = ((uint32_t)cam_x * 100u) / max_x;
        pct = (p > 100u) ? 100u : (uint8_t)p;
    }
    record_level_progress(level_idx, pct, is_practice);
}
