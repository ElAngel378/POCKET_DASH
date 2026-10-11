#pragma bank 29

#include "states.h"
#include "scratch.h"
#include "fade.h"
#include "gameplay.h"
#include "assets.h"
#include "rainbow.h"
#include "logo.h"
#include "bg_parallax.h"
#include "settings.h"
#include "title_buttons.h"
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>

// CGB: the menu background is never scrolled. Like in gameplay, the sky is the parallax block
// pattern, animated by the VBlank handler, and the ground strip scrolls by rewriting its map
// rows. DMG: the sky scrolls with SCX between two scanline interrupts.
//
// Big buttons (Famidash's icon / play / wrench row), see tools/gen_title_buttons.py: three of
// them side by side need more than 10 sprites on a line.
// CGB: each is a BG "core" (cells fully inside the button, static tiles in VRAM bank 1) plus 8x8
//      sprites for the rest, where the moving sky shows through. The top of the screen uses 8x8
//      sprites, the ground buttons 8x16 (switched by a scanline interrupt at MENU_OBJ16_LINE).
// DMG: PLAY is the old 8x16 sprites; the side buttons are BG tiles blended with the sky for each
//      of the 64 SCX values (tables in ROM), copied in as the sky scrolls (double buffered).

// Set to 1 to easily re-enable the version label in the bottom-right corner
#define SHOW_MENU_VERSION_LABEL 0

// Menu selections
#define SEL_PLAY     0
#define SEL_MUSIC    1
#define SEL_SETTINGS 2
#define SEL_ICON     3
#define SEL_WRENCH   4

#define MENU_OBJ16_LINE 100   // CGB: 8x8 sprites above (big buttons), 8x16 below (ground buttons)

static uint8_t tb_cgb_place(uint8_t slot) __nonbanked;

static void update_menu_sprites(uint8_t sel) {
    // Music button (16x16) on left of ground bar (Screen X = 56..72, OAM X = 64)
    uint8_t mx = 64;
    uint8_t my = (sel == SEL_MUSIC) ? 138 : 140; // Screen Y = 122 (selected) / 124 (unselected)
    uint8_t prop_m = (_cpu == CGB_TYPE) ? 3 : 0;
    set_sprite_tile(0, 22); move_sprite(0, mx, my);      set_sprite_prop(0, prop_m);
    set_sprite_tile(1, 24); move_sprite(1, mx + 8, my);  set_sprite_prop(1, prop_m);

    // Settings cog button (16x16) on right of ground bar (Screen X = 88..104, OAM X = 96)
    uint8_t sx = 96;
    uint8_t sy = (sel == SEL_SETTINGS) ? 138 : 140;
    set_sprite_tile(2, 28); move_sprite(2, sx, sy);      set_sprite_prop(2, prop_m);
    set_sprite_tile(3, 30); move_sprite(3, sx + 8, sy);  set_sprite_prop(3, prop_m);

    uint8_t slot;
    if (_cpu == CGB_TYPE) {
        slot = tb_cgb_place(4);
    } else {
        // Play button sprites (OAM 4..11) - centered at Screen X = 64..96 (OAM X = 72)
        uint8_t bx = 72;
        uint8_t by = 68;   // Screen Y = 52 (the big buttons do not move when selected)
        for (uint8_t c = 0; c < 4; c++) {
            set_sprite_tile(4 + 2 * c, 4 * c);     move_sprite(4 + 2 * c, bx + 8 * c, by);      set_sprite_prop(4 + 2 * c, 0);
            set_sprite_tile(5 + 2 * c, 4 * c + 2); move_sprite(5 + 2 * c, bx + 8 * c, by + 16); set_sprite_prop(5 + 2 * c, 0);
        }
        slot = 12;
    }

    // Select arrow cursor (tile 26) on top of the selected button, pointing down. Last in OAM:
    // on a line with too many sprites, the one dropped is the cursor, where it has no pixels.
    uint8_t prop_c = (_cpu == CGB_TYPE) ? 4 : 0;
    uint8_t cx, cy = TB_CURSOR_Y + 16;
    switch (sel) {
        case SEL_PLAY:   cx = TB_PLAY_CURSOR_X + 8; break;
        case SEL_ICON:   cx = TB_ICON_CURSOR_X + 8; break;
        case SEL_WRENCH: cx = (uint8_t)(TB_WRENCH_CURSOR_X + 8); break;
        case SEL_MUSIC:  cx = mx + 4; cy = my - 9; break;
        default:         cx = sx + 4; cy = sy - 9; break;
    }
    set_sprite_tile(slot, 26);
    set_sprite_prop(slot, prop_c);
    move_sprite(slot, cx, cy);

    for (uint8_t s = slot + 1; s < 40; s++) hide_sprite(s);
}

// This file lives in a switchable ROM bank, so everything that switches ROM
// banks (to read tile data from other banks) must run from bank 0: __nonbanked.
extern const uint8_t menu_ground_tiles[];
BANKREF_EXTERN(menu_bg)
extern const unsigned char playbutton[];
BANKREF_EXTERN(playbutton)

// Ground strip: menu_ground.png, 3 tile rows (screen y 120..143). Per row: a 64px period
// of start tile, 6 uniform tiles, end tile. See build_ground_variants().
#define GROUND_ROW            15
#define GROUND_ROWS           3
#define GROUND_PERIOD         64
#define GROUND_SPEED          3
#define GROUND_TILE_BASE      1    // VRAM tiles 1..48, 16 per row
#define GROUND_TILES_PER_ROW  16
#define GROUND_TILE_COUNT     9

// menu_ground_tiles, copied out of its bank: only while the ground tiles are built (scratch.h)
#define ground_src scratch_buf

static void menu_load_bg_gfx(void) __nonbanked {
    uint8_t prev_bank = _current_bank;
    SWITCH_ROM(BANK(menu_bg));
    for (uint8_t i = 0; i < GROUND_TILE_COUNT * 16; i++) ground_src[i] = menu_ground_tiles[i];

    // Load logo tiles from BANK(logo)
    SWITCH_ROM(BANK(logo));
    set_bkg_data(LOGO_TILE_START, LOGO_TILE_COUNT, logo_tiles);
    SWITCH_ROM(prev_bank);
}

// The ground is a flat strip with one 8px wide "pillar" every 64px. A pillar at any pixel
// position is a pair of tiles per row (the left one holds the pillar's first 8-o pixels,
// the right one the rest), so all 8 offsets o are loaded once and the strip scrolls by
// choosing tiles in the map. Per row: uniform, L(o) for o = 0..7, R(o) for o = 1..7.
#define GROUND_UNIFORM(k)  ((uint8_t)(GROUND_TILE_BASE + (k) * GROUND_TILES_PER_ROW))
#define GROUND_LEFT(k, o)  ((uint8_t)(GROUND_UNIFORM(k) + 1 + (o)))
#define GROUND_RIGHT(k, o) ((uint8_t)(GROUND_UNIFORM(k) + 8 + (o)))

static void build_ground_variants(void) {
    for (uint8_t k = 0; k < GROUND_ROWS; k++) {
        const uint8_t *st = &ground_src[k * 3 * 16];   // tile at the start of the period
        const uint8_t *un = st + 16;                   // uniform middle tile
        const uint8_t *en = st + 32;                   // tile at the end of the period
        set_bkg_data(GROUND_UNIFORM(k), 1, un);
        for (uint8_t o = 0; o < 8; o++) {
            uint8_t left[16], right[16];
            for (uint8_t i = 0; i < 16; i++) {         // bytes alternate between the 2 bit planes
                // the pillar: last 5 pixels of the end tile + first 3 of the start tile
                uint8_t pillar = (uint8_t)(((en[i] & 0x1F) << 3) | (st[i] >> 5));
                uint16_t w = ((uint16_t)un[i] << 8) | un[i];
                w = (w & ~(0xFF00u >> o)) | (((uint16_t)pillar << 8) >> o);
                left[i] = (uint8_t)(w >> 8);
                right[i] = (uint8_t)w;
            }
            set_bkg_data(GROUND_LEFT(k, o), 1, left);
            if (o) set_bkg_data(GROUND_RIGHT(k, o), 1, right);
        }
    }
}

// phase = screen x of the first pillar's left edge (pillars are GROUND_PERIOD apart)
static void ground_rows_hblank(const uint8_t *rows) __nonbanked;

// The 3 rows are written whole (32 columns, contiguous in the map): on DMG with the fast HBlank
// copy (set_bkg_tiles waits on STAT per byte: ~66 scanlines for these 60 bytes). Columns 20..31
// are never seen on the ground lines (SCX 0 there).
static void draw_ground(uint8_t phase) {
    uint8_t rows[GROUND_ROWS][32];
    for (uint8_t k = 0; k < GROUND_ROWS; k++) memset(rows[k], GROUND_UNIFORM(k), 32);
    for (uint8_t j = 0; j < 3; j++) {
        int16_t xl = (int16_t)phase + (int16_t)(j * GROUND_PERIOD) - 8;
        int8_t tx = (int8_t)(xl >> 3);
        uint8_t o = (uint8_t)xl & 7;
        for (uint8_t k = 0; k < GROUND_ROWS; k++) {
            if (tx >= 0 && tx < 20) rows[k][tx] = GROUND_LEFT(k, o);
            if (o && tx + 1 >= 0 && tx + 1 < 20) rows[k][tx + 1] = GROUND_RIGHT(k, o);
        }
    }
    if (_cpu != CGB_TYPE && (LCDC_REG & LCDCF_ON)) {
        ground_rows_hblank(&rows[0][0]);
    } else {
        for (uint8_t k = 0; k < GROUND_ROWS; k++) {
            set_bkg_tiles(0, (uint8_t)(GROUND_ROW + k), 20, 1, rows[k]);
        }
    }
}

// DMG sky: the gameplay parallax block pattern (phase 0, 48 tiles, 8 tiles = 64px period),
// loaded into bank 0 VRAM and scrolled with SCX by a scanline (LYC) interrupt, the same
// look as the CGB menu. Rows 0..1 (logo) and the ground strip are drawn at SCX 0.
#define DMG_SKY_TILE   53
#define DMG_SKY_PERIOD 64
static volatile uint8_t menu_sky_scx;

// The interrupt comes one line early and waits for the line itself: an interrupt in the way
// (the music tick) can no longer make the split land a line late.
static void menu_stat_isr(void) __nonbanked {
    if (LYC_REG == 15) {
        while (LY_REG < 16u);
        SCX_REG = menu_sky_scx;
        LYC_REG = 119;
    } else {
        while (LY_REG < 120u);
        SCX_REG = 0;
        LYC_REG = 255;
    }
}

// Start of every frame: logo at SCX 0, the STAT handler scrolls the sky from line 16. Done in
// the VBlank interrupt: the main loop's ground update can run well into the next frame.
static void menu_vbl_isr(void) __nonbanked {
    SCX_REG = 0;
    LYC_REG = 15;
}

static void menu_load_dmg_sky_tiles(void) __nonbanked {
    uint8_t prev_bank = _current_bank;
    SWITCH_ROM(BANK(bg_parallax_data_0));
    set_bkg_data(DMG_SKY_TILE, BG_PARALLAX_NUM_TILES, bg_parallax_phases_0[0]);
    SWITCH_ROM(prev_bank);
}

// Sky rows 2..14: the gameplay parallax pattern (48 tiles, tile row offsets 0/16/32,
// 8 tiles wide): VRAM bank 1 on CGB, bank 0 at DMG_SKY_TILE on DMG.
static void draw_sky(void) {
    uint8_t tiles[32];
    if (_cpu == CGB_TYPE && setting_show_bg_enabled) {
        static const uint8_t row_to_ty0[3] = { 0, 16, 32 };
        for (uint8_t ty = 2; ty < GROUND_ROW; ty++) {
            uint8_t ty0 = (uint8_t)(row_to_ty0[(ty >> 1) % 3] + ((ty & 1) << 3));
            for (uint8_t x = 0; x < 20; x++) tiles[x] = (uint8_t)(ty0 + (x & 7));
            VBK_REG = 0;
            set_bkg_tiles(0, ty, 20, 1, tiles);
        }
        VBK_REG = 1;
        fill_bkg_rect(0, 2, 20, GROUND_ROW - 2, 0x0B);   // bank 1, palette 3
        VBK_REG = 0;
    } else if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 2, 20, GROUND_ROW - 2, 3);      // plain sky colour (palette 3)
        VBK_REG = 0;
    } else if (setting_show_bg_enabled) {
        static const uint8_t row_to_ty0[3] = { 0, 16, 32 };
        menu_load_dmg_sky_tiles();
        for (uint8_t ty = 2; ty < GROUND_ROW; ty++) {
            uint8_t ty0 = (uint8_t)(DMG_SKY_TILE + row_to_ty0[(ty >> 1) % 3] + ((ty & 1) << 3));
            for (uint8_t x = 0; x < 32; x++) tiles[x] = (uint8_t)(ty0 + (x & 7));   // all 32 columns: SCX scrolls
            set_bkg_tiles(0, ty, 32, 1, tiles);
        }
    }
}

static void menu_load_playbutton_gfx(void) __nonbanked {
    uint8_t prev_bank = _current_bank;
    SWITCH_ROM(BANK(playbutton));
    set_sprite_data(0, 16, &playbutton[16]);
    SWITCH_ROM(prev_bank);
}

// ---------------------------------------------------------------- big buttons (generated data)
// Sky tile row offset of map row ty (draw_sky's pattern: 3 tile pairs, 16 tiles apart). A table:
// the __nonbanked functions below switch ROM banks and cannot call into this one.
static uint8_t sky_ty0_ram[18];
#define sky_ty0(ty) (sky_ty0_ram[(ty)])
static void sky_ty0_init(void) {
    for (uint8_t ty = 0; ty < 18; ty++)
        sky_ty0_ram[ty] = (uint8_t)((((ty >> 1) % 3) << 4) + ((ty & 1) << 3));
}

// CGB: place the rim sprites of the three big buttons from OAM slot `slot`, returns the next free slot
static uint8_t tb_cgb_place(uint8_t slot) __nonbanked {
    uint8_t prev_bank = _current_bank;
    SWITCH_ROM(BANK(title_buttons));
    for (uint8_t b = 0; b < 3; b++) {
        const uint8_t *e = tb_groups[b].spr;
        for (uint8_t i = tb_groups[b].nspr; i; i--, e += 4, slot++) {
            shadow_OAM[slot].y = e[0];
            shadow_OAM[slot].x = e[1];
            shadow_OAM[slot].tile = e[2];
            shadow_OAM[slot].prop = e[3];
        }
    }
    SWITCH_ROM(prev_bank);
    return slot;
}

// CGB: the BG core cells of the big buttons (tile in VRAM bank 0's map, attribute in bank 1's)
static void tb_cgb_cores(void) __nonbanked {
    uint8_t prev_bank = _current_bank;
    SWITCH_ROM(BANK(title_buttons));
    for (uint8_t b = 0; b < 3; b++) {
        const uint8_t *e = tb_groups[b].core;
        for (uint8_t i = tb_groups[b].ncore; i; i--, e += 4) {
            VBK_REG = 1; set_bkg_tile_xy(e[0], e[1], e[3]);
            VBK_REG = 0; set_bkg_tile_xy(e[0], e[1], e[2]);
        }
    }
    SWITCH_ROM(prev_bank);
}

static palette_color_t tb_obj_pals_ram[TB_OBJ_PAL_COUNT * 4];
static palette_color_t tb_core_pals_ram[TB_CORE_PAL_COUNT * 4];

static void tb_cgb_load_gfx(void) __nonbanked {
    uint8_t prev_bank = _current_bank;
    SWITCH_ROM(BANK(title_buttons));
    set_sprite_data(TB_OBJ_TILE_BASE, TB_OBJ_TILE_COUNT, tb_obj_tiles);
    VBK_REG = 1;
    set_bkg_data(TB_CORE_TILE_BASE, TB_CORE_TILE_COUNT, tb_core_tiles);
    VBK_REG = 0;
    for (uint8_t i = 0; i < TB_OBJ_PAL_COUNT * 4; i++) tb_obj_pals_ram[i] = tb_obj_palettes[i];
    for (uint8_t i = 0; i < TB_CORE_PAL_COUNT * 4; i++) tb_core_pals_ram[i] = tb_core_palettes[i];
    SWITCH_ROM(prev_bank);
}

// CGB: 8x8 sprites for the big buttons, 8x16 from MENU_OBJ16_LINE (ground buttons)
static void menu_cgb_stat_isr(void) __nonbanked { LCDC_REG |= LCDCF_OBJ16; }
static void menu_cgb_vbl_isr(void) __nonbanked { LCDC_REG &= ~LCDCF_OBJ16; }

// DMG side buttons: TB_DMG_TILES tiles per side, 2 buffers (the one not shown is filled with the
// next scroll position while the sky waits its 2 frames)
static const uint8_t *tb_dmg_src[4];   // [side * 2 + half]: SCX 0..31, 32..63
static uint8_t tb_dmg_bank[4];
static uint8_t tb_dmg_left_ram[2], tb_dmg_row0_ram[2];
static uint8_t dmg_buf;            // buffer shown
static uint8_t dmg_scx;            // SCX the shown tiles are for
static uint8_t dmg_prep;           // sides of scroll position dmg_scx + 1 copied into the other buffer
static uint8_t dmg_c0[2];          // first map column shown per side (0xFF: none)

static void tb_dmg_init(void) __nonbanked {
    uint8_t prev_bank = _current_bank;
    tb_dmg_src[0] = tb_dmg_icon_0;   tb_dmg_bank[0] = BANK(tb_dmg_icon_0);
    tb_dmg_src[1] = tb_dmg_icon_1;   tb_dmg_bank[1] = BANK(tb_dmg_icon_1);
    tb_dmg_src[2] = tb_dmg_wrench_0; tb_dmg_bank[2] = BANK(tb_dmg_wrench_0);
    tb_dmg_src[3] = tb_dmg_wrench_1; tb_dmg_bank[3] = BANK(tb_dmg_wrench_1);
    SWITCH_ROM(BANK(title_buttons));
    for (uint8_t i = 0; i < 2; i++) { tb_dmg_left_ram[i] = tb_dmg_left[i]; tb_dmg_row0_ram[i] = tb_dmg_row0[i]; }
    SWITCH_ROM(prev_bank);
    dmg_buf = 1;
    dmg_c0[0] = dmg_c0[1] = 0xFF;
}

// VRAM copy with the display on, 8 bytes per HBlank: set_bkg_data waits on STAT before every byte
// and copying a side per frame with it made the DMG menu miss every other VBlank (30 fps).
// Waits for mode 3 with interrupts on, then (interrupts off) for its end and copies 8 bytes:
// 41 M-cycles + ~9 for noticing the mode change, inside HBlank + the next line's OAM scan (VRAM
// is free in both). That is >= 55 M-cycles on the menu's lines, which have at most ~5 sprites
// (a line with 10 sprites can cut HBlank to 21). An interrupt between the two waits makes it
// miss the mode 3 start: then it waits for the next line.
static const uint8_t *hb_src;
static uint8_t *hb_dst;
static uint8_t hb_bursts;    // 8-byte bursts, 1..255
static void tb_hblank_copy(void) __naked __nonbanked {   // (called with a data bank switched in)
    __asm
        ld      hl, #_hb_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a                    ; hl = src
        ld      a, (_hb_dst)
        ld      e, a
        ld      a, (_hb_dst + 1)
        ld      d, a                    ; de = dst
        ld      a, (_hb_bursts)
        ld      b, a
    1$:
        ldh     a, (_STAT_REG + 0)      ; mode 3 (interrupts on)
        and     a, #3
        cp      a, #3
        jr      NZ, 1$
        di
        ldh     a, (_STAT_REG + 0)
        and     a, #3
        cp      a, #3
        jr      Z, 2$
        ei                              ; mode 3 is over already (an interrupt): next line
        jr      1$
    2$:
        ldh     a, (_STAT_REG + 0)      ; until HBlank
        and     a, #3
        cp      a, #3
        jr      Z, 2$
        .rept 7                         ; (dst is 8-byte aligned: e only wraps on the 8th)
        ld      a, (hl+)
        ld      (de), a
        inc     e
        .endm
        ld      a, (hl+)
        ld      (de), a
        inc     de
        ei
        dec     b
        jr      NZ, 1$
        ret
    __endasm;
}

static void ground_rows_hblank(const uint8_t *rows) __nonbanked {
    hb_src = rows;
    hb_dst = (uint8_t *)(0x9800u + GROUND_ROW * 32u);
    hb_bursts = GROUND_ROWS * 32 / 8;
    tb_hblank_copy();
}

// The tiles of one side (0 icon, 1 wrench) for scroll position scx into buffer buf
static void tb_dmg_copy(uint8_t side, uint8_t scx, uint8_t buf) __nonbanked {
    uint8_t prev_bank = _current_bank;
    const uint8_t *src;
    if (setting_show_bg_enabled) {
        uint8_t k = (uint8_t)(side * 2 + ((scx & 63) >> 5));   // (the sky repeats every 64 px)
        SWITCH_ROM(tb_dmg_bank[k]);
        src = tb_dmg_src[k] + (uint16_t)(scx & 31) * (TB_DMG_TILES * 16);
    } else {
        SWITCH_ROM(BANK(title_buttons));
        src = tb_dmg_plain + (uint16_t)side * (TB_DMG_TILES * 16);
    }
    uint8_t tile = (uint8_t)(TB_DMG_BUF_TILE + buf * (2 * TB_DMG_TILES) + side * TB_DMG_TILES);
    if (LCDC_REG & LCDCF_ON) {
        hb_src = src;
        hb_dst = (uint8_t *)(0x8800u + (uint16_t)(tile - 128u) * 16u);   // (buffer tiles are >= 128)
        hb_bursts = TB_DMG_TILES * 2;
        tb_hblank_copy();
    } else {
        set_bkg_data(tile, TB_DMG_TILES, src);   // display off: no waiting
    }
    SWITCH_ROM(prev_bank);
}

// Map cells of the sides. SCX runs 0..255 (the sky repeats every 64 px and the 32 column map is
// 256 px wide), so a scroll step moves a side's cells by at most one column, wrapping around the
// map (no jump back at the 64 px wrap, which made that frame rewrite 13 columns and miss a VBlank).
// All the tile numbers are worked out once (tb_dmg_ids_init): a step writes the button's 5 x 4
// cells and the column it left (its sky tiles back), 2 small writes per side; written late, they
// would land after the LCD drew the button's first rows (line 48).
static uint8_t dmg_ids[2][2][TB_DMG_TILES];               // [side][buffer]: row major, 5 x 4
static uint8_t dmg_col_ids[2][2][TB_DMG_COLS][TB_DMG_ROWS]; // the same, a column at a time
static uint8_t dmg_sky_col[2][8][TB_DMG_ROWS];             // [side][map column & 7]: sky tiles

static void tb_dmg_ids_init(void) {
    for (uint8_t side = 0; side < 2; side++) {
        uint8_t row0 = tb_dmg_row0_ram[side];
        for (uint8_t b = 0; b < 2; b++) {
            uint8_t first = (uint8_t)(TB_DMG_BUF_TILE + b * (2 * TB_DMG_TILES) + side * TB_DMG_TILES);
            for (uint8_t r = 0; r < TB_DMG_ROWS; r++)
                for (uint8_t i = 0; i < TB_DMG_COLS; i++) {
                    uint8_t t = (uint8_t)(first + r * TB_DMG_COLS + i);
                    dmg_ids[side][b][r * TB_DMG_COLS + i] = t;
                    dmg_col_ids[side][b][i][r] = t;
                }
        }
        for (uint8_t c = 0; c < 8; c++)
            for (uint8_t r = 0; r < TB_DMG_ROWS; r++)
                dmg_sky_col[side][c][r] = setting_show_bg_enabled ? (uint8_t)(TB_DMG_SKY_TILE + sky_ty0(row0 + r) + c) : 0;
    }
}

// Map cells of both sides for scroll position scx from buffer buf
static void tb_dmg_place(uint8_t scx, uint8_t buf) {
    for (uint8_t side = 0; side < 2; side++) {
        uint8_t row0 = tb_dmg_row0_ram[side];
        uint8_t c0 = (uint8_t)((((uint16_t)tb_dmg_left_ram[side] + scx) >> 3) & 31);
        if (c0 + TB_DMG_COLS <= 32) {
            set_bkg_tiles(c0, row0, TB_DMG_COLS, TB_DMG_ROWS, dmg_ids[side][buf]);
        } else {                                  // across the map's right edge
            for (uint8_t i = 0; i < TB_DMG_COLS; i++)
                set_bkg_tiles((uint8_t)((c0 + i) & 31), row0, 1, TB_DMG_ROWS, dmg_col_ids[side][buf][i]);
        }
        uint8_t old = dmg_c0[side];
        if (old != 0xFF && old != c0) {           // the columns left: sky again (normally 1)
            for (uint8_t i = 0; i < TB_DMG_COLS; i++) {
                uint8_t c = (uint8_t)((old + i) & 31);
                if ((uint8_t)((c - c0) & 31) >= TB_DMG_COLS)
                    set_bkg_tiles(c, row0, 1, TB_DMG_ROWS, dmg_sky_col[side][c & 7]);
            }
        }
        dmg_c0[side] = c0;
    }
}

// Show both sides for scroll position scx now (through the other buffer)
static void tb_dmg_show(uint8_t scx) {
    uint8_t nb = dmg_buf ^ 1;
    tb_dmg_copy(0, scx, nb);
    tb_dmg_copy(1, scx, nb);
    tb_dmg_place(scx, nb);
    dmg_buf = nb; dmg_scx = scx; dmg_prep = 0;
}

static void menu_stop_irqs(uint8_t dmg_sky_irq) {
    if (dmg_sky_irq) {
        disable_interrupts();
        remove_LCD(menu_stat_isr);
        remove_VBL(menu_vbl_isr);
        STAT_REG &= ~STATF_LYC;
        SCX_REG = 0;
        set_interrupts(VBL_IFLAG | TIM_IFLAG);
        enable_interrupts();
    } else if (_cpu == CGB_TYPE) {
        disable_interrupts();
        remove_LCD(menu_cgb_stat_isr);
        remove_VBL(menu_cgb_vbl_isr);
        STAT_REG &= ~STATF_LYC;
        set_interrupts(VBL_IFLAG | TIM_IFLAG);
        enable_interrupts();
        LCDC_REG |= LCDCF_OBJ16;
    }
}

GameState update_menu_state(void) BANKED {
    // Load behind the black screen the last state faded to, display on (a display switched off
    // shows white): the palettes are only stored until fade_from_black
    fade_set_black();
    fade_hold = 1;

    // Restore standard palettes
    fade_set_dmg_palettes(0xE4, 0xE4, 0xD2);

    static uint16_t frame_counter = 0;

    sky_ty0_init();
    menu_load_bg_gfx();
    static const uint8_t blank_tile[16] = { 0 };
    set_bkg_data(0, 1, blank_tile);
    fill_bkg_rect(0, 0, 32, 32, 0);
    build_ground_variants();

#if SHOW_MENU_VERSION_LABEL
    // Load Pusab font tiles for version label
    setup_menu_font();

    // Version label "Demo v03" on window layer
    static const uint8_t ver_tiles[] = { 16, 17, 25, 27, 0, 34, 3, 6 };
    for (uint8_t i = 0; i < 8; i++) {
        set_win_tile_xy(i, 0, (uint8_t)(0xD0u + ver_tiles[i]));
    }
#endif

    // Title logo
    for (uint8_t x = 0; x < 20; x++) {
        set_bkg_tile_xy(x, 0, (uint8_t)(LOGO_TILE_START + x));
        set_bkg_tile_xy(x, 1, (uint8_t)(LOGO_TILE_START + 20 + x));
    }

    draw_sky();
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 2, 1);                    // logo: palette 1
        fill_bkg_rect(0, GROUND_ROW, 20, GROUND_ROWS, 4); // ground: palette 4
        VBK_REG = 0;
        // Continue the rainbow where it was (entering with colour 0 flashed red)
        apply_rainbow_palette((uint8_t)(frame_counter >> 4));
    }
    uint8_t ground_x = 0;
    draw_ground(ground_x);

    // Play button
    menu_load_playbutton_gfx();

    if (_cpu == CGB_TYPE) {
        // Big buttons: rim sprite tiles (OBJ), core tiles (VRAM bank 1), their palettes
        tb_cgb_load_gfx();
        for (uint8_t i = 0; i < TB_CORE_PAL_COUNT; i++)   // BG palettes 2, 5, 6, 7
            fade_set_bkg_palette(i ? (uint8_t)(4 + i) : 2, 1, &tb_core_pals_ram[i * 4]);

        static const uint16_t music_btn_palette[] = {
            RGB8(255, 255, 255),
            RGB8(255, 235, 20),
            RGB8(80, 210, 20),
            RGB8(0, 0, 0)
        };
        static const uint16_t cursor_palette[] = {
            RGB8(255, 255, 255),
            RGB8(255, 255, 255),
            RGB8(255, 255, 255),
            RGB8(0, 0, 0)
        };
        // fade_set_sprite_palette is banked: RAM copies of this bank's tables. Palettes 0..2 and
        // 5..7: big button rims, 3: ground buttons, 4: cursor
        palette_color_t spr[32];
        for (uint8_t i = 0; i < 32; i++) spr[i] = RGB8(255, 255, 255);
        for (uint8_t i = 0; i < TB_OBJ_PAL_COUNT * 4; i++) spr[(i < 12) ? i : i + 8] = tb_obj_pals_ram[i];
        for (uint8_t i = 0; i < 4; i++) {
            spr[12 + i] = music_btn_palette[i];
            spr[16 + i] = cursor_palette[i];
        }
        fade_set_sprite_palette(0, 8, spr);
    } else {
        tb_dmg_init();
        tb_dmg_ids_init();
    }

    // Music button tiles (16x16 icon -> 4 8x8 tiles = 2 8x16 sprites)
    // Colors: 0 transparent, 1 yellow (note), 2 lime (bg), 3 black (outline)
    // Eighth-note drawn from scratch: oval head, stem, slim flag.
    static const uint8_t music_button_tiles[64] = {
        // Tile 0 (left top)
        0x00, 0x00, 0x0F, 0x0F, 0x18, 0x1F, 0x21, 0x3F, 0x63, 0x7E, 0x43, 0x7E, 0x43, 0x7E, 0x43, 0x7E,
        // Tile 1 (left bottom)
        0x43, 0x7E, 0x5F, 0x7E, 0x7F, 0x60, 0x7F, 0x60, 0x3F, 0x31, 0x1E, 0x1F, 0x0F, 0x0F, 0x00, 0x00,
        // Tile 2 (right top)
        0x00, 0x00, 0xF0, 0xF0, 0x18, 0xF8, 0x84, 0xFC, 0xC6, 0x7E, 0xE2, 0x3E, 0xF2, 0x9E, 0xF2, 0xDE,
        // Tile 3 (right bottom)
        0xA2, 0xFE, 0x82, 0xFE, 0x82, 0xFE, 0x86, 0xFE, 0x04, 0xFC, 0x18, 0xF8, 0xF0, 0xF0, 0x00, 0x00
    };
    set_sprite_data(22, 4, music_button_tiles);

    // Settings button tiles (16x16 icon -> 4 8x8 tiles = 2 8x16 sprites)
    // Colors: 0 transparent, 1 yellow (gear), 2 lime (bg), 3 black (outline)
    // 8-tooth gear drawn from scratch: yellow ring + teeth, lime hub hole.
    static const uint8_t settings_button_tiles[64] = {
        0x00, 0x00, 0x0F, 0x0F, 0x1B, 0x1E, 0x2F, 0x3E, 0x7F, 0x70, 0x5F, 0x71, 0x7E, 0x73, 0x7C, 0x47,
        0x7C, 0x47, 0x7E, 0x73, 0x5F, 0x71, 0x7F, 0x70, 0x2F, 0x3E, 0x1B, 0x1E, 0x0F, 0x0F, 0x00, 0x00,
        0x00, 0x00, 0xF0, 0xF0, 0xD8, 0x78, 0xF4, 0x7C, 0xFE, 0x0E, 0xFA, 0x8E, 0x7E, 0xCE, 0x3E, 0xE2,
        0x3E, 0xE2, 0x7E, 0xCE, 0xFA, 0x8E, 0xFE, 0x0E, 0xF4, 0x7C, 0xD8, 0x78, 0xF0, 0xF0, 0x00, 0x00,
    };
    set_sprite_data(28, 4, settings_button_tiles);

    // Cursor indicator tiles (downward-pointing chevron, 8x16 mode: white body, black border)
    static const uint8_t pause_cursor_tiles[32] = {
        0x7E, 0x7E, 0x7E, 0x42, 0x7E, 0x42, 0x3C, 0x24,
        0x3C, 0x24, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    set_sprite_data(26, 2, pause_cursor_tiles);

    SPRITES_8x16;
    // Top row: icon, play, wrench. Ground row: music, settings.
    uint8_t menu_sel = SEL_PLAY;
    uint8_t last_ground_sel = SEL_MUSIC;

    update_menu_sprites(menu_sel);

    SCX_REG = 0;
    SCY_REG = 0;

    if (_cpu == CGB_TYPE && setting_show_bg_enabled) {
        wait_vbl_done();   // the display is on: the parallax GDMA has to run in VBlank
        init_bg_parallax();
    }

    uint8_t dmg_sky_irq = (_cpu != CGB_TYPE && setting_show_bg_enabled);
    if (_cpu == CGB_TYPE) {
        tb_cgb_cores();
        disable_interrupts();
        add_LCD(menu_cgb_stat_isr);
        add_VBL(menu_cgb_vbl_isr);
        STAT_REG |= STATF_LYC;
        LYC_REG = MENU_OBJ16_LINE;
        set_interrupts(VBL_IFLAG | LCD_IFLAG | TIM_IFLAG);
        enable_interrupts();
    } else {
        tb_dmg_show(0);
    }
    if (dmg_sky_irq) {
        menu_sky_scx = 0;
        disable_interrupts();
        add_LCD(menu_stat_isr);
        add_VBL(menu_vbl_isr);
        STAT_REG |= STATF_LYC;
        LYC_REG = 15;
        set_interrupts(VBL_IFLAG | LCD_IFLAG | TIM_IFLAG);
        enable_interrupts();
    }
    // DMG: the side buttons follow the sky scroll
    uint8_t dmg_scroll = dmg_sky_irq && setting_parallax_enabled;

    SHOW_BKG;
    SHOW_SPRITES;
#if SHOW_MENU_VERSION_LABEL
    WY_REG = 136;
    WX_REG = 103;
    SHOW_WIN;
#else
    HIDE_WIN;
#endif
    DISPLAY_ON;   // (only off at boot)

    // VBlank handler: runs the parallax GDMA at the start of VBlank (CGB)
    bg_parallax_isr_start();
    fade_from_black(2);
    uint8_t sky_phase = 0;
    uint8_t prev_joy = joypad();

    while (1) {
        if (_cpu == CGB_TYPE && setting_show_bg_enabled && setting_parallax_enabled) {
            // the sky drifts left half a pixel per frame
            uint8_t phase = (uint8_t)(-(int8_t)(frame_counter >> 1)) & 63u;
            if (phase != sky_phase) {
                sky_phase = phase;
                request_bg_parallax(phase);
            }
        }
        bg_wait_vbl();
        if (dmg_scroll) {
            // In VBlank: the STAT handler takes menu_sky_scx at line 16, and the side buttons' map
            // cells (from line 48) have to move with it in the same frame
            uint8_t scx = (uint8_t)(frame_counter >> 1);   // 0..255, see tb_dmg_place
            if (scx != dmg_scx) {
                uint8_t nb = dmg_buf ^ 1;
                if (dmg_prep < 2 || scx != (uint8_t)(dmg_scx + 1)) {
                    menu_sky_scx = scx;                // not ready (a slow frame): copy now
                    tb_dmg_show(scx);
                } else {
                    menu_sky_scx = scx;
                    tb_dmg_place(scx, nb);
                    dmg_buf = nb; dmg_scx = scx; dmg_prep = 0;
                }
            }
        }
        draw_ground(ground_x);
        if (dmg_scroll && dmg_prep < 2) {
            // the next scroll position into the buffer not shown, a side per frame
            uint8_t next = (uint8_t)(dmg_scx + 1);
            tb_dmg_copy(dmg_prep, next, dmg_buf ^ 1);
            dmg_prep++;
        }
        if (_cpu == CGB_TYPE && (frame_counter & 15) == 0) {
            apply_rainbow_palette((uint8_t)(frame_counter >> 4));
        }

        uint8_t joy = joypad();
        uint8_t pressed = joy & ~prev_joy;
        prev_joy = joy;
        uint8_t old_sel = menu_sel;

        if (pressed & (J_LEFT | J_RIGHT)) {
            uint8_t left = (pressed & J_LEFT) != 0;
            switch (menu_sel) {
                case SEL_PLAY:     menu_sel = left ? SEL_ICON : SEL_WRENCH; break;
                case SEL_ICON:     if (!left) menu_sel = SEL_PLAY; break;
                case SEL_WRENCH:   if (left) menu_sel = SEL_PLAY; break;
                case SEL_MUSIC:    if (!left) menu_sel = SEL_SETTINGS; break;
                case SEL_SETTINGS: if (left) menu_sel = SEL_MUSIC; break;
            }
        } else if (pressed & J_DOWN) {
            if (menu_sel == SEL_PLAY) menu_sel = last_ground_sel;
            else if (menu_sel == SEL_ICON) menu_sel = SEL_MUSIC;
            else if (menu_sel == SEL_WRENCH) menu_sel = SEL_SETTINGS;
        } else if (pressed & J_UP) {
            if (menu_sel == SEL_MUSIC || menu_sel == SEL_SETTINGS) menu_sel = SEL_PLAY;
        }
        if (menu_sel != old_sel) {
            if (menu_sel == SEL_MUSIC || menu_sel == SEL_SETTINGS) last_ground_sel = menu_sel;
            update_menu_sprites(menu_sel);
        }

        // A on the wrench (custom levels, not made yet) does nothing
        uint8_t go = (pressed & (J_A | J_START)) && menu_sel != SEL_WRENCH;
        if (go) {
            fade_capture_current();   // the rainbow palette changes: fade out from the current one
            fade_to_black(2);
            menu_stop_irqs(dmg_sky_irq);
            bg_parallax_isr_stop();
            HIDE_SPRITES;
            HIDE_WIN;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            switch (menu_sel) {
                case SEL_PLAY:  return STATE_NEW_MENU_SELECT;
                case SEL_MUSIC: return STATE_MUSIC_TEST;
                case SEL_ICON:  return STATE_ICON_SELECT;
                default:        return STATE_SETTINGS;
            }
        }

        frame_counter++;
        ground_x = (uint8_t)(ground_x - GROUND_SPEED) & (GROUND_PERIOD - 1);
    }
}
