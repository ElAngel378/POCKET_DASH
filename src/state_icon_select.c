#pragma bank 24

#include <gb/gb.h>
#include <gb/cgb.h>
#include "states.h"
#include "icon_select_bg.h"
#include "icon_catalog.h"
#include "save_manager.h"
#include "settings.h"
#include "fade.h"

BANKREF(state_icon_select)

// Icon select (garage), laid out like the Geometry Dash one (tools/build_icon_select_gfx.py):
// cube preview, gamemode badges, the icon box and the colour swatch box (primary, secondary).
// Select switches the cursor between the two boxes. Up/Down moves between rows, Left/Right
// along a row (past the end: the next / previous page of 12). B / Start saves and leaves.
// Badges can't be selected yet (only the cube has icons). DMG: the colour rows are the 4
// shades (selected_dmg_*), and the preview shows the cube in them.

#define SECTION_GAMEMODE        0
#define SECTION_ICON            1
#define SECTION_COLOR_PRIMARY   2
#define SECTION_COLOR_SECONDARY 3

#define SPR_BRACKET 0
#define SPR_FRAME   1

#define ICONS_PER_ROW 6

// OAM: 0..3 icon cursor, 4..7 gamemode cursor, 8 primary swatch, 9 secondary swatch
#define OAM_ICON  0
#define OAM_TAB   4
#define OAM_COL1  8
#define OAM_COL2  9

#define BOX_GREY  RGB8( 40,  40,  40)
#define PAGE_GREY RGB8(140, 140, 140)

static uint8_t active_section = SECTION_ICON;
static uint8_t color_section = SECTION_COLOR_PRIMARY;   // the colour box row Select goes back to
static uint8_t selected_tab = 0;   // 0 = cube (the only gamemode with icons so far)
static uint8_t shown_page;

static uint8_t icon_page(uint8_t icon) {
    uint8_t p = 0;
    while (icon >= ICONS_PER_PAGE) { icon -= ICONS_PER_PAGE; p++; }
    return p;
}

// Preview of the selected icon, and its page in the icon box when that changed
static void refresh_preview_tiles(void) {
    uint8_t page = icon_page(selected_icon);
    if (page != shown_page) {
        shown_page = page;
        icon_page_load(page);
    }
    // preview colours 0 secondary, 1 page, 2 primary, 3 black; DMG: as the shades chosen
    if (_cpu == CGB_TYPE) icon_preview_load(selected_icon, 0, 1, 2, 3);
    else icon_preview_load(selected_icon, selected_dmg_secondary, 1, selected_dmg_primary, 3);
}

// Colour cursor column of a row: CGB the colour, DMG the shade
static uint8_t swatch_col(uint8_t primary) {
    if (_cpu == CGB_TYPE)
        return (uint8_t)(ICON_SELECT_SWATCH_COL + (primary ? selected_color_primary : selected_color_secondary));
    return (uint8_t)(ICON_SELECT_DMG_SWATCH_COL + (primary ? selected_dmg_primary : selected_dmg_secondary));
}

static void preview_palette(palette_color_t *p) {
    p[0] = icon_palette_colors[selected_color_secondary];
    p[1] = PAGE_GREY;
    p[2] = icon_palette_colors[selected_color_primary];
    p[3] = RGB8(0, 0, 0);
}

static void set_palettes(void) {
    if (_cpu == CGB_TYPE) {
        palette_color_t bg[32] = {
            RGB8(255, 255, 255), PAGE_GREY, BOX_GREY, RGB8(0, 0, 0),             // 0: greys
            0, 0, 0, 0,                                                          // 1: preview
            RGB8(189, 242, 71), PAGE_GREY, RGB8(67, 156, 24), RGB8(0, 0, 0),     // 2: green
            RGB8(60, 245, 230), PAGE_GREY, RGB8(15, 110, 115), RGB8(0, 0, 0),    // 3: cyan
        };
        preview_palette(&bg[4]);
        // 4..7: colour swatches, three per palette
        for (uint8_t i = 0; i < NUM_PALETTE_COLORS; i++) {
            uint8_t pal = 4 + i / 3;
            bg[pal * 4] = BOX_GREY;
            bg[pal * 4 + 1 + i % 3] = icon_palette_colors[i];
        }
        fade_set_bkg_palette(0, 8, bg);

        // Sprite 0: cursor of the active section (yellow), 1: the others (white)
        palette_color_t spr[8] = {
            0, RGB8(255, 230, 0), 0, RGB8(0, 0, 0),
            0, RGB8(255, 255, 255), 0, RGB8(0, 0, 0),
        };
        fade_set_sprite_palette(0, 2, spr);
    }
    // DMG: active cursor white, the others light grey (black edge on both)
    fade_set_dmg_palettes(0xE4, 0xC0, 0xC4);
}

static void refresh_preview_palette(void) {
    if (_cpu == CGB_TYPE) {
        palette_color_t p[4];
        preview_palette(p);
        fade_set_bkg_palette(1, 1, p);
    } else {
        refresh_preview_tiles();   // DMG: the shades are in the tiles
    }
}

static uint8_t cursor_prop(uint8_t section) {
    // CGB palette 0/1, DMG OBP0/OBP1
    if (section == active_section) return 0;
    return (_cpu == CGB_TYPE) ? 1 : S_PALETTE;
}

static void place_brackets(uint8_t oam, uint8_t x, uint8_t y, uint8_t size, uint8_t prop) {
    // Corners of a size x size frame around (x, y): bracket tile flipped per corner
    uint8_t x0 = (uint8_t)(x - 2 + 8), y0 = (uint8_t)(y - 2 + 16);
    uint8_t x1 = (uint8_t)(x0 + size + 4 - 8), y1 = (uint8_t)(y0 + size + 4 - 8);
    set_sprite_tile(oam,     SPR_BRACKET); set_sprite_prop(oam,     prop);                     move_sprite(oam,     x0, y0);
    set_sprite_tile(oam + 1, SPR_BRACKET); set_sprite_prop(oam + 1, prop | S_FLIPX);           move_sprite(oam + 1, x1, y0);
    set_sprite_tile(oam + 2, SPR_BRACKET); set_sprite_prop(oam + 2, prop | S_FLIPY);           move_sprite(oam + 2, x0, y1);
    set_sprite_tile(oam + 3, SPR_BRACKET); set_sprite_prop(oam + 3, prop | S_FLIPX | S_FLIPY); move_sprite(oam + 3, x1, y1);
}

static void update_cursor_sprites(void) {
    uint8_t slot = (uint8_t)(selected_icon - shown_page * ICONS_PER_PAGE);
    place_brackets(OAM_ICON,
                   (uint8_t)(ICON_SELECT_SLOT_X0 + (slot % ICONS_PER_ROW) * ICON_SELECT_SLOT_PITCH),
                   (uint8_t)(ICON_SELECT_SLOT_Y0 + (slot / ICONS_PER_ROW) * ICON_SELECT_SLOT_PITCH),
                   16, cursor_prop(SECTION_ICON));
    place_brackets(OAM_TAB,
                   (uint8_t)(ICON_SELECT_BADGE_X0 + selected_tab * ICON_SELECT_BADGE_PITCH),
                   ICON_SELECT_BADGE_Y, 16, cursor_prop(SECTION_GAMEMODE));

    set_sprite_tile(OAM_COL1, SPR_FRAME);
    set_sprite_prop(OAM_COL1, cursor_prop(SECTION_COLOR_PRIMARY));
    move_sprite(OAM_COL1, (uint8_t)(swatch_col(1) * 8 + 8),
                (uint8_t)(ICON_SELECT_SWATCH_ROW0 * 8 + 16));
    set_sprite_tile(OAM_COL2, SPR_FRAME);
    set_sprite_prop(OAM_COL2, cursor_prop(SECTION_COLOR_SECONDARY));
    move_sprite(OAM_COL2, (uint8_t)(swatch_col(0) * 8 + 8),
                (uint8_t)((ICON_SELECT_SWATCH_ROW0 + 1) * 8 + 16));
}

GameState update_icon_select_state(void) BANKED {
    // load with the display on (off shows white); fade_from_black shows the palettes
    fade_set_black();
    fade_hold = 1;
    SCX_REG = 0;
    SCY_REG = 0;
    HIDE_WIN;
    SPRITES_8x8;
    for (uint8_t s = 0; s < 40; s++) hide_sprite(s);

    if (selected_icon >= NUM_CUBE_ICONS) selected_icon = 0;
    if (active_section == SECTION_GAMEMODE) active_section = SECTION_ICON;
    if (selected_color_primary >= NUM_PALETTE_COLORS) selected_color_primary = 0;
    if (selected_color_secondary >= NUM_PALETTE_COLORS) selected_color_secondary = 1;

    set_bkg_data(0, ICON_SELECT_BG_TILE_COUNT, icon_select_bg_tiles);
    set_sprite_data(0, ICON_SELECT_SPR_TILE_COUNT, icon_select_spr_tiles);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_bkg_tiles(0, 0, 20, 18, icon_select_bg_attrmap);
        VBK_REG = 0;
    }
    set_bkg_tiles(0, 0, 20, 18, icon_select_bg_map);
    if (_cpu != CGB_TYPE) {
        // DMG: only the 4 shades it can show (white .. black), in the middle of the box
        for (uint8_t i = 0; i < NUM_PALETTE_COLORS; i++) {
            uint8_t col = (uint8_t)(ICON_SELECT_SWATCH_COL + i);
            uint8_t sh = (uint8_t)(col - ICON_SELECT_DMG_SWATCH_COL);
            uint8_t t = (sh < 4) ? (uint8_t)(ICON_SELECT_DMG_SWATCH_BASE + sh) : ICON_SELECT_DMG_BOX_TILE;
            set_bkg_tile_xy(col, ICON_SELECT_SWATCH_ROW0, t);
            set_bkg_tile_xy(col, ICON_SELECT_SWATCH_ROW0 + 1, t);
        }
    }

    shown_page = 0xFF;
    refresh_preview_tiles();
    update_cursor_sprites();
    set_palettes();

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;
    fade_from_black(2);

    uint8_t prev_joy = joypad();

    while (1) {
        wait_vbl_done();

        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;
        uint8_t moved = 0, recolour = 0;

        // the selected icon's row on its page: 0 top, 1 bottom
        uint8_t icon_row = (uint8_t)(selected_icon - shown_page * ICONS_PER_PAGE) >= ICONS_PER_ROW;
        if (pressed & J_SELECT) {
            if (active_section == SECTION_ICON) active_section = color_section;
            else { color_section = active_section; active_section = SECTION_ICON; }
            moved = 1;
        } else if (pressed & J_UP) {
            if (active_section == SECTION_ICON) {
                if (icon_row) {
                    selected_icon -= ICONS_PER_ROW;
                    refresh_preview_tiles();
                    moved = 1;
                }
            } else if (active_section == SECTION_COLOR_SECONDARY) {
                active_section = SECTION_COLOR_PRIMARY;
                moved = 1;
            }
        } else if (pressed & J_DOWN) {
            if (active_section == SECTION_ICON) {
                if (!icon_row && selected_icon + ICONS_PER_ROW < NUM_CUBE_ICONS) {
                    selected_icon += ICONS_PER_ROW;
                    refresh_preview_tiles();
                    moved = 1;
                }
            } else if (active_section == SECTION_COLOR_PRIMARY) {
                active_section = SECTION_COLOR_SECONDARY;
                moved = 1;
            }
        } else if (pressed & (J_LEFT | J_RIGHT)) {
            uint8_t right = (pressed & J_RIGHT) ? 1 : 0;
            if (active_section == SECTION_GAMEMODE) {
                if (right && selected_tab < NUM_GAMEMODE_TABS - 1) { selected_tab++; moved = 1; }
                else if (!right && selected_tab > 0) { selected_tab--; moved = 1; }
            } else if (active_section == SECTION_ICON) {
                if (right && selected_icon < NUM_CUBE_ICONS - 1) { selected_icon++; moved = 1; }
                else if (!right && selected_icon > 0) { selected_icon--; moved = 1; }
                if (moved) refresh_preview_tiles();
            } else {
                // CGB: the colour, DMG: the shade (plain values: SDCC got the pointer version wrong)
                uint8_t prim = (active_section == SECTION_COLOR_PRIMARY);
                uint8_t cgb = (_cpu == CGB_TYPE);
                uint8_t v = cgb ? (prim ? selected_color_primary : selected_color_secondary)
                                : (prim ? selected_dmg_primary : selected_dmg_secondary);
                uint8_t last = cgb ? (uint8_t)(NUM_PALETTE_COLORS - 1) : 3;
                if (pressed & J_RIGHT) {
                    if (v < last) { v++; moved = 1; }
                } else if (v) {
                    v--; moved = 1;
                }
                if (moved) {
                    recolour = 1;
                    if (cgb) { if (prim) selected_color_primary = v; else selected_color_secondary = v; }
                    else { if (prim) selected_dmg_primary = v; else selected_dmg_secondary = v; }
                }
            }
        }

        if (moved) {
            if (recolour) refresh_preview_palette();
            update_cursor_sprites();
        }

        if (pressed & J_A) save_game_data();

        if (pressed & (J_B | J_START)) {
            save_game_data();
            fade_to_black(2);
            HIDE_SPRITES;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            return STATE_MENU;
        }
    }
}
