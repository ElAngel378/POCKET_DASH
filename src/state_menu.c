#pragma bank 29

#include "states.h"
#include "scratch.h"
#include "fade.h"
#include "gameplay.h"
#include "assets.h"

extern uint8_t selected;   // main.c: the level of the level selects
#include "rainbow.h"
#include "logo.h"
#include "bg_parallax.h"
#include "settings.h"
#include "title_buttons.h"
#include "cube_icon_frames.h"
#include "icon_catalog.h"
BANKREF_EXTERN(mini_cube_icons)
extern const uint8_t mini_cube_icons[];   // 32 icons, 3 images each
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>

// CGB: the menu BG never scrolls. The sky is the parallax pattern (VBlank handler) and the
// ground strip scrolls by rewriting its map rows. DMG: the sky scrolls with SCX between two
// scanline interrupts.
//
// Big buttons (icon / play / wrench), from tools/gen_title_buttons.py. Three side by side
// would be more than 10 sprites on a line, so on CGB each is a BG core plus 8x8 sprites for
// the rest (the sky shows through). Every CGB sprite here is 8x8, so the menu cube can jump
// past the big buttons (at most 9 sprites on a line).
// DMG: PLAY is 8x16 sprites; the side buttons are BG tiles blended with the sky for each of
// the 64 SCX values (tables in ROM), double buffered.

// Set to 1 to easily re-enable the version label in the bottom-right corner
#define SHOW_MENU_VERSION_LABEL 0

// Menu selections
#define SEL_PLAY     0
#define SEL_MUSIC    1
#define SEL_SETTINGS 2
#define SEL_ICON     3
#define SEL_WRENCH   4

#define MENU_BTN_PAL 5   // CGB: music and settings share a big button rim palette (lime, black, yellow)

// CGB: a random icon (cube, ship, ball, UFO) crosses the screen behind the buttons (GDA's title screen)
#define MC_OAM     36    // OAM 36..39: behind the other sprites (a lower slot is drawn on top on CGB)
#define MC_TILE    52    // sprite tiles 52..55 / 56..59, double buffered
#define MC_PAL     3
#define MC_REST_Y  104   // screen y of the icon standing on the ground
#define MC_REST_Y_MINI 109  // (of the player box: the mini image is drawn 3 px lower, 8 tall)
#define MC_TOP_Y   16    // the ship and the UFO fly no higher (below the logo)
#define MC_START_X (-17) // off the left edge
#define MC_CUT_ABOVE 41  // icon y from which it can touch the buttons' BG centres
#define MC_CUT_BELOW 80  // (map rows 7..9, lines 56..79; the icon is 16 lines tall)

// The icon's state: in scratch_buf, after the 64 bytes the ground buttons' tiles are built in (WRAM
// is full: a few dozen more bytes of globals run the stack into them)
typedef struct {
    uint8_t solid[18][3];        // map cells that are BG centres of the big buttons (a bit per column)
    uint8_t art_ram[64 + 15];    // art with the cut, for the GDMA: 16 byte aligned
    uint8_t *art;
    const uint8_t *src;          // the first image of this look, in ROM bank bank
    uint8_t bank, seed, mode;    // mode: MODE_CUBE .. MODE_UFO
    int16_t x;                   // screen x of the left edge
    uint16_t acc, speed, y;      // y: 8.8
    int16_t vy;
    uint8_t on_ground, frame, timer;   // frame / timer: as the player's anim_*
    uint8_t hold;                // ship: the button held
    uint8_t tilt;                // ship / UFO: the image shown, eased towards the flight direction
    uint8_t front;               // sprite tile buffer shown
    uint8_t art_id;              // image in it, 0xFF: none
    uint8_t art_cut;             // it has pixels cut out, for the position it was drawn at
    uint8_t wait;                // frames left before the next icon comes in
    uint8_t mini;                // the mini size: one 8x8 sprite
    uint16_t rest;               // y on the ground (8.8)
    // What the next update draws (the copy into the buffer not shown takes a frame: the sprites
    // keep to the same frame as their art, or the cut would be a frame off the buttons)
    uint8_t q_valid, q_copied, q_id, q_flip, q_cut, q_y;
    int16_t q_x;
} mc_state_t;
typedef char mc_state_fits[(sizeof(mc_state_t) <= SCRATCH_SIZE - 64u) ? 1 : -1];
#define mc_s (*(mc_state_t *)(scratch_buf + 64u))
#define mc_solid mc_s.solid
#define mc_art mc_s.art
#define mc_src mc_s.src
#define mc_bank mc_s.bank
#define mc_seed mc_s.seed
#define mc_mode mc_s.mode
#define mc_x mc_s.x
#define mc_acc mc_s.acc
#define mc_speed mc_s.speed
#define mc_y mc_s.y
#define mc_vy mc_s.vy
#define mc_on_ground mc_s.on_ground
#define mc_frame mc_s.frame
#define mc_timer mc_s.timer
#define mc_hold mc_s.hold
#define mc_tilt mc_s.tilt
#define mc_front mc_s.front
#define mc_art_id mc_s.art_id
#define mc_art_cut mc_s.art_cut
#define mc_wait mc_s.wait
#define mc_mini mc_s.mini
#define mc_rest mc_s.rest
#define mc_q_valid mc_s.q_valid
#define mc_q_copied mc_s.q_copied
#define mc_q_id mc_s.q_id
#define mc_q_flip mc_s.q_flip
#define mc_q_cut mc_s.q_cut
#define mc_q_y mc_s.q_y
#define mc_q_x mc_s.q_x

static uint8_t tb_cgb_place(uint8_t slot) __nonbanked;

static void update_menu_sprites(uint8_t sel) {
    // Music button (16x16) on left of ground bar (Screen X = 56..72, OAM X = 64)
    uint8_t mx = 64;
    uint8_t my = (sel == SEL_MUSIC) ? 138 : 140; // Screen Y = 122 (selected) / 124 (unselected)
    // Settings cog button (16x16) on right of ground bar (Screen X = 88..104, OAM X = 96)
    uint8_t sx = 96;
    uint8_t sy = (sel == SEL_SETTINGS) ? 138 : 140;

    uint8_t slot;
    if (_cpu == CGB_TYPE) {
        slot = 0;
        for (uint8_t b = 0; b < 2; b++) {
            uint8_t x = b ? sx : mx, y = b ? sy : my, t = b ? 28 : 22;
            for (uint8_t i = 0; i < 4; i++, slot++) {   // top and bottom of the left, then right column
                set_sprite_tile(slot, (uint8_t)(t + i));
                move_sprite(slot, (uint8_t)(x + ((i & 2u) << 2)), (uint8_t)(y + ((i & 1u) << 3)));
                set_sprite_prop(slot, MENU_BTN_PAL);
            }
        }
        slot = tb_cgb_place(slot);
    } else {
        set_sprite_tile(0, 22); move_sprite(0, mx, my);      set_sprite_prop(0, 0);
        set_sprite_tile(1, 24); move_sprite(1, mx + 8, my);  set_sprite_prop(1, 0);
        set_sprite_tile(2, 28); move_sprite(2, sx, sy);      set_sprite_prop(2, 0);
        set_sprite_tile(3, 30); move_sprite(3, sx + 8, sy);  set_sprite_prop(3, 0);

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

    for (uint8_t s = slot + 1; s < ((_cpu == CGB_TYPE) ? MC_OAM : 40); s++) hide_sprite(s);
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

// big buttons (generated data)
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
            mc_solid[e[1]][e[0] >> 3] |= (uint8_t)(1u << (e[0] & 7u));
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
        SPRITES_8x16;   // what the other screens expect
    }
}

// Music / settings button art (colours 1 yellow, 2 lime, 3 black) into 4 sprite tiles. CGB: for
// the rim palette MENU_BTN_PAL (1 lime, 2 black, 3 yellow).
static void load_ground_button_tiles(uint8_t first, const uint8_t *src) {
    uint8_t *out = scratch_buf;   // (the ground tiles are built by now)
    for (uint8_t i = 0; i < 64u; i += 2) {
        uint8_t lo = src[i], hi = src[i + 1];
        out[i] = (_cpu == CGB_TYPE) ? (uint8_t)(lo ^ hi) : lo;
        out[i + 1] = (_cpu == CGB_TYPE) ? lo : hi;
    }
    set_sprite_data(first, 4, out);
}

// Half the game's speeds (8.8 px a frame): the screen is much narrower than the GBA's, where GDA
// uses them as they are
static const uint16_t mc_speeds[5] = { 288, 357, 444, 536, 660 };
static const uint16_t mc_ship_tilt_vy[] = SHIP_TILT_VY;
static const uint16_t mc_ufo_tilt_vy[] = UFO_TILT_VY;
static uint8_t mc_rnd(void) {
    mc_seed = (uint8_t)(((mc_seed & 0x80u) ? (uint8_t)((mc_seed << 1) ^ 0x1Du) : (uint8_t)(mc_seed << 1)) + DIV_REG);
    return mc_seed;
}

static uint8_t mc_colour(void) {
    uint8_t c = mc_rnd() & 15u;
    return (c >= NUM_PALETTE_COLORS) ? (uint8_t)(c - NUM_PALETTE_COLORS) : c;
}

// A new look, off the left edge. In VBlank: it writes a sprite palette.
static void mc_reset(void) {
    mc_mode = mc_rnd() & 3u;
    mc_mini = !(mc_rnd() & 3u);         // 1 in 4, like GDA
    mc_bank = BANK(cube_icon_frames);
    if (mc_mini) {
        if (mc_mode == MODE_CUBE) {
            mc_src = mini_cube_icons + (uint16_t)(mc_rnd() & 31u) * 48u;
            mc_bank = BANK(mini_cube_icons);
        } else {
            mc_src = MINI_FRAME(0);
        }
    } else if (mc_mode == MODE_SHIP) {
        mc_src = SHIP_FRAME(0);
    } else if (mc_mode == MODE_BALL) {
        mc_src = BALL_FRAME(0);
    } else if (mc_mode == MODE_UFO) {
        mc_src = UFO_FRAME(0);
    } else {
        uint8_t icon = mc_rnd() & 63u;
        if (icon >= NUM_CUBE_ICONS) icon -= NUM_CUBE_ICONS;
        if (icon == 0) {
            mc_src = cube_icon_frames;
        } else {
            uint8_t j = (uint8_t)(icon - 1u);
            if (j < CUBE_FD_PER_BANK) { mc_src = cube_fd_frames_0; mc_bank = BANK(cube_fd_frames_0); }
            else if ((j -= CUBE_FD_PER_BANK) < CUBE_FD_PER_BANK) { mc_src = cube_fd_frames_1; mc_bank = BANK(cube_fd_frames_1); }
            else if ((j -= CUBE_FD_PER_BANK) < CUBE_FD_PER_BANK) { mc_src = cube_fd_frames_2; mc_bank = BANK(cube_fd_frames_2); }
            else if ((j -= CUBE_FD_PER_BANK) < CUBE_FD_PER_BANK) { mc_src = cube_fd_frames_3; mc_bank = BANK(cube_fd_frames_3); }
            else { j -= CUBE_FD_PER_BANK; mc_src = cube_fd_frames_4; mc_bank = BANK(cube_fd_frames_4); }
            mc_src += (uint16_t)j * CUBE_FD_FRAMES * 64u;
        }
    }
    palette_color_t pal[4];
    pal[0] = RGB8(255, 255, 255);
    pal[1] = icon_color(mc_colour());   // secondary
    pal[2] = icon_color(mc_colour());   // primary
    pal[3] = RGB8(0, 0, 0);
    fade_set_sprite_palette(MC_PAL, 1, pal);

    uint8_t s = mc_rnd() & 7u;
    mc_speed = mc_speeds[(s >= 5u) ? (uint8_t)(s - 5u) : s];
    mc_x = MC_START_X;
    mc_acc = 0;
    mc_rest = (uint16_t)(mc_mini ? MC_REST_Y_MINI : MC_REST_Y) << 8;
    mc_y = mc_rest;
    mc_vy = 0;
    mc_on_ground = 1;
    mc_frame = mc_timer = 0;
    mc_hold = 0;
    mc_tilt = (mc_mode == MODE_SHIP) ? SHIP_FRAME_LEVEL : UFO_FRAME_LEVEL;
    mc_art_id = 0xFF;
    mc_art_cut = 0;
    mc_q_valid = 0;
}

static void mc_spin(void) {
    mc_timer += 10;
    if (mc_timer >= 21) {
        mc_timer -= 21;
        if (++mc_frame >= 24) mc_frame = 0;
    }
}

// up (vel_y < 0) stops at the top, down lands on the ground
static void mc_move_y(void) {
    mc_y += (uint16_t)mc_vy;
    if (mc_y >= mc_rest) {
        mc_y = mc_rest;
        mc_vy = 0;
        mc_on_ground = 1;
    } else if (mc_y < ((uint16_t)MC_TOP_Y << 8)) {
        mc_y = (uint16_t)MC_TOP_Y << 8;
        mc_vy = 0;
    }
}

static void mc_step_cube(void) {
    if (mc_on_ground && !(mc_rnd() & 15u)) {
        mc_vy = mc_mini ? MINI_JUMP_FORCE : JUMP_FORCE;
        mc_on_ground = 0;
        if ((uint8_t)(mc_frame % 6u) == 0) { mc_frame = (uint8_t)((mc_frame + 1u) % 24u); mc_timer = 0; }
    }
    if (!mc_on_ground) {
        mc_vy += mc_mini ? MINI_GRAVITY : GRAVITY;
        if (mc_vy > MAX_FALL_SPEED) mc_vy = MAX_FALL_SPEED;
        mc_move_y();
    }
    if (mc_on_ground) {
        uint8_t st = (uint8_t)((mc_frame << 1) + (mc_timer >= 11u ? 1u : 0u));
        uint8_t q = (uint8_t)(((uint8_t)(mc_frame % 6u) << 1) + (st & 1u));
        if (q != 0) {                   // settle onto the nearest side
            if (q >= 6) st += (q == 11) ? 1 : 2;
            else st -= (q == 1) ? 1 : 2;
            if (st >= 48) st -= 48;
            mc_frame = (uint8_t)(st >> 1);
            mc_timer = (st & 1u) ? 11 : 0;
        } else {
            mc_timer = 0;
        }
    } else {
        mc_spin();
    }
}

static void mc_step_ship(void) {
    if (!(mc_rnd() & 15u)) mc_hold ^= 1;
    uint8_t falling = (mc_vy > 0);
    if (mc_mini) {
        if (mc_hold) mc_vy += falling ? -MINI_SHIP_GRAVITY_HOLD_FALL : MINI_SHIP_THRUST;
        else mc_vy += falling ? MINI_SHIP_GRAVITY : MINI_SHIP_GRAVITY_AFTER_HOLD;
    } else if (mc_hold) {
        mc_vy += falling ? -SHIP_GRAVITY_HOLD_FALL : SHIP_THRUST;
    } else {
        mc_vy += falling ? SHIP_GRAVITY : SHIP_GRAVITY_AFTER_HOLD;
    }
    mc_move_y();
    if (mc_vy > SHIP_MAX_VEL_UP) mc_vy = SHIP_MAX_VEL_UP;
    if (mc_vy < -SHIP_MAX_VEL_DOWN) mc_vy = -SHIP_MAX_VEL_DOWN;

    uint16_t a = (mc_vy < 0) ? (uint16_t)-mc_vy : (uint16_t)mc_vy;
    uint8_t k = 0;
    while (k < (uint8_t)(sizeof(mc_ship_tilt_vy) / 2u) && a >= mc_ship_tilt_vy[k]) k++;
    uint8_t target = (mc_vy > 0) ? (uint8_t)(SHIP_FRAME_LEVEL - k) : (uint8_t)(SHIP_FRAME_LEVEL + k);
    if (mc_tilt + 2u <= target) mc_tilt += 2;
    else if (mc_tilt < target) mc_tilt++;
    else if (mc_tilt >= target + 2u) mc_tilt -= 2;
    else if (mc_tilt > target) mc_tilt--;
}

static void mc_step_ufo(void) {
    if (!(mc_rnd() & 31u)) mc_vy = mc_mini ? MINI_UFO_JUMP_VEL : UFO_JUMP_VEL;
    if (mc_vy > (mc_mini ? MINI_UFO_MAX_FALL : UFO_MAX_FALL)) mc_vy -= UFO_GRAVITY;
    else mc_vy += UFO_GRAVITY;
    mc_move_y();

    uint16_t a = (mc_vy < 0) ? (uint16_t)-mc_vy : (uint16_t)mc_vy;
    uint8_t k = 0;
    while (k < (uint8_t)(sizeof(mc_ufo_tilt_vy) / 2u) && a >= mc_ufo_tilt_vy[k]) k++;
    uint8_t target = (mc_vy > 0) ? (uint8_t)(UFO_FRAME_LEVEL + k) : (uint8_t)(UFO_FRAME_LEVEL - k);
    if (mc_tilt < target) mc_tilt++;
    else if (mc_tilt > target) mc_tilt--;
}

// An image into RAM (it is in another ROM bank than this code: run from bank 0)
static void mc_art_load(const uint8_t *src) __nonbanked {
    uint8_t prev_bank = _current_bank;
    SWITCH_ROM(mc_bank);
    for (uint8_t i = 0; i < 64u; i++) mc_art[i] = src[i];
    SWITCH_ROM(prev_bank);
}

// Clears the pixels of mc_art that the BG centres of the big buttons cover, with its top left at
// (sx, sy), drawn with the sprite flips in flags: n = 4 tiles (2 x 2) or 1. Returns 1 if there were any.
static uint8_t mc_art_cut_out(int16_t sx, uint8_t sy, uint8_t flags, uint8_t n) {
    uint8_t any = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t *tile = mc_art + ((uint16_t)((n == 4u && flags) ? 3u - i : i) << 4);
        int16_t x = sx + (int16_t)((i & 2u) << 2);
        int8_t ca = (int8_t)(x >> 3);
        int8_t cb = (int8_t)(ca + 1);
        uint8_t off = (uint8_t)x & 7u;
        uint8_t y0 = (uint8_t)(sy + ((i & 1u) << 3));
        uint8_t ma = (flags & S_FLIPX) ? (uint8_t)(0xFFu >> off) : (uint8_t)(0xFFu << off);   // the pixels in cell ca
        for (uint8_t r = 0; r < 8u; r++) {
            uint8_t row = (uint8_t)((uint8_t)(y0 + r) >> 3);
            if (row >= 18u) continue;
            uint8_t m = 0;
            if (ca >= 0 && ca < 20 && (mc_solid[row][(uint8_t)ca >> 3] & (uint8_t)(1u << ((uint8_t)ca & 7u)))) m = ma;
            if (cb >= 0 && cb < 20 && (mc_solid[row][(uint8_t)cb >> 3] & (uint8_t)(1u << ((uint8_t)cb & 7u)))) m |= (uint8_t)~ma;
            if (m) {
                uint8_t tr = (flags & S_FLIPY) ? (uint8_t)(7u - r) : r;
                tile[(uint8_t)(tr << 1)] &= (uint8_t)~m;
                tile[(uint8_t)((tr << 1) + 1u)] &= (uint8_t)~m;
                any = 1;
            }
        }
    }
    return any;
}

static void mc_draw(void) {
    uint8_t t = (uint8_t)(MC_TILE + (uint8_t)(mc_front << 2));
    int16_t ox = mc_q_x + 8;
    uint8_t oy = (uint8_t)(mc_q_y + 16u);
    uint8_t n = mc_mini ? 1u : 4u;
    for (uint8_t i = 0; i < 4u; i++) {
        uint8_t s = (uint8_t)(MC_OAM + i);
        int16_t x = ox + (int16_t)((i & 2u) << 2);
        if (i >= n || x <= 0 || x >= 168) {
            hide_sprite(s);
        } else {
            set_sprite_tile(s, (uint8_t)(t + ((n == 4u && mc_q_flip) ? 3u - i : i)));
            set_sprite_prop(s, (uint8_t)(MC_PAL | mc_q_flip));
            move_sprite(s, (uint8_t)x, (uint8_t)(oy + ((i & 1u) << 3)));
        }
    }
}

static void mc_update(void) {
    if (mc_q_valid) {
        if (bg_cube_pending) return;    // (the copy always runs in the next VBlank)
        if (mc_q_copied) {
            mc_front ^= 1;
            mc_art_id = mc_q_id;
            mc_art_cut = mc_q_cut;
        }
        mc_draw();
        mc_q_valid = 0;
    }
    if (mc_wait) {
        if (--mc_wait == 0) mc_reset();
        return;
    }
    if (mc_x >= 161) {                  // off the right edge: a short pause
        mc_wait = (uint8_t)(15u + (mc_rnd() & 15u));
        return;
    }

    mc_acc += mc_speed;
    mc_x += (int16_t)(mc_acc >> 8);
    mc_acc &= 0xFFu;
    if (mc_mode == MODE_CUBE) mc_step_cube();
    else if (mc_mode == MODE_SHIP) mc_step_ship();
    else if (mc_mode == MODE_UFO) mc_step_ufo();
    else mc_spin();                     // the ball rolls along the ground

    uint8_t id, flip = 0;               // flip: the sprite flips
    uint8_t half_steps = (uint8_t)((mc_frame << 1) + (mc_timer >= 11u ? 1u : 0u));
    if (mc_mini) {
        static const uint8_t mini_cube_img[6] = { 0, 1, 1, 2, 1, 0 };
        // ship image (0 steep down .. 30 steep up) -> mini image (30, 15, 0, -15, -30 degrees)
        static const uint8_t mini_ship_img[SHIP_FRAME_COUNT] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };
        if (mc_mode == MODE_CUBE) {
            uint8_t q = (uint8_t)(mc_frame % 6u);
            id = mini_cube_img[q];
            if (q == 4) flip = S_FLIPX;
        } else if (mc_mode == MODE_BALL) {
            uint8_t k = (uint8_t)(half_steps / 3u);
            if (k >= MINI_BALL_FRAMES) { k -= MINI_BALL_FRAMES; flip = S_FLIPX | S_FLIPY; }
            id = (uint8_t)(MINI_BALL_FIRST + k);
        } else if (mc_mode == MODE_SHIP) {
            id = (uint8_t)(MINI_SHIP_FIRST + mini_ship_img[mc_tilt]);
        } else {
            id = (uint8_t)(MINI_UFO_FIRST + ((mc_tilt > UFO_FRAME_LEVEL) ? (uint8_t)(mc_tilt - UFO_FRAME_LEVEL)
                                                                         : (uint8_t)(UFO_FRAME_LEVEL - mc_tilt)));
            if (mc_tilt > UFO_FRAME_LEVEL) flip = S_FLIPX;
        }
        mc_q_x = mc_x;
        mc_q_y = (uint8_t)((mc_y >> 8) + 3u);
    } else {
        if (mc_mode == MODE_CUBE || mc_mode == MODE_BALL) {
            id = half_steps;
            if (id >= CUBE_ICON_FRAMES) { id -= CUBE_ICON_FRAMES; flip = S_FLIPX | S_FLIPY; }
        } else {
            id = mc_tilt;
        }
        mc_q_x = (mc_mode == MODE_BALL) ? mc_x : (int16_t)(mc_x - 1);
        mc_q_y = (uint8_t)((mc_y >> 8) - ((mc_mode == MODE_SHIP || mc_mode == MODE_UFO) ? 1u : 0u));
    }
    mc_q_id = id;
    mc_q_flip = flip;
    mc_q_cut = 0;
    uint8_t mini_cube = (mc_mini && mc_mode == MODE_CUBE);   // its images are 16 bytes each, not aligned for the GDMA
    const uint8_t *src = mc_src + (uint16_t)id * (mini_cube ? 16u : 64u);
    uint8_t in_band = (mc_q_y >= MC_CUT_ABOVE && mc_q_y < MC_CUT_BELOW);
    if (in_band || mini_cube) mc_art_load(src);
    if (in_band) mc_q_cut = mc_art_cut_out(mc_q_x, mc_q_y, flip, mc_mini ? 1u : 4u);
    mc_q_copied = (mc_q_cut || mc_art_cut || id != mc_art_id);
    if (mc_q_copied) {
        bg_cube_src = (mc_q_cut || mini_cube) ? (const uint8_t *)mc_art : src;
        bg_cube_bank = mc_bank;
        bg_cube_dst = (uint8_t *)(0x8000u + ((uint16_t)(MC_TILE + (uint8_t)((mc_front ^ 1u) << 2)) << 4));
        bg_cube_pending = 4;
    }
    mc_q_valid = 1;
}

GameState update_menu_state(void) BANKED {
    // load with the display on (off shows white); fade_from_black shows the palettes
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

        static const uint16_t cursor_palette[] = {
            RGB8(255, 255, 255),
            RGB8(255, 255, 255),
            RGB8(255, 255, 255),
            RGB8(0, 0, 0)
        };
        // fade_set_sprite_palette is banked: RAM copies of this bank's tables. Palettes 0..2 and
        // 5..7: big button rims (5: the ground buttons too), 3: menu cube, 4: cursor
        palette_color_t spr[32];
        for (uint8_t i = 0; i < 32; i++) spr[i] = RGB8(255, 255, 255);
        for (uint8_t i = 0; i < TB_OBJ_PAL_COUNT * 4; i++) spr[(i < 12) ? i : i + 8] = tb_obj_pals_ram[i];
        for (uint8_t i = 0; i < 4; i++) spr[16 + i] = cursor_palette[i];
        fade_set_sprite_palette(0, 8, spr);

        memset(&mc_s, 0, sizeof(mc_state_t));
        mc_seed = DIV_REG | 1u;
        mc_art = (uint8_t *)(((uint16_t)mc_s.art_ram + 15u) & 0xFFF0u);
        mc_front = 0;
        bg_cube_pending = 0;
        for (uint8_t i = 0; i < 4u; i++) hide_sprite((uint8_t)(MC_OAM + i));
        mc_reset();
    } else {
        tb_dmg_init();
        tb_dmg_ids_init();
    }

    // Music button tiles (16x16 icon -> 4 8x8 tiles = 2 8x16 sprites)
    // Colors: 0 transparent, 1 yellow (note), 2 lime (bg), 3 black (outline)
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
    load_ground_button_tiles(22, music_button_tiles);

    // Settings button tiles (16x16 icon -> 4 8x8 tiles = 2 8x16 sprites)
    // Colors: 0 transparent, 1 yellow (gear), 2 lime (bg), 3 black (outline)
    static const uint8_t settings_button_tiles[64] = {
        0x00, 0x00, 0x0F, 0x0F, 0x1B, 0x1E, 0x2F, 0x3E, 0x7F, 0x70, 0x5F, 0x71, 0x7E, 0x73, 0x7C, 0x47,
        0x7C, 0x47, 0x7E, 0x73, 0x5F, 0x71, 0x7F, 0x70, 0x2F, 0x3E, 0x1B, 0x1E, 0x0F, 0x0F, 0x00, 0x00,
        0x00, 0x00, 0xF0, 0xF0, 0xD8, 0x78, 0xF4, 0x7C, 0xFE, 0x0E, 0xFA, 0x8E, 0x7E, 0xCE, 0x3E, 0xE2,
        0x3E, 0xE2, 0x7E, 0xCE, 0xFA, 0x8E, 0xFE, 0x0E, 0xF4, 0x7C, 0xD8, 0x78, 0xF0, 0xF0, 0x00, 0x00,
    };
    load_ground_button_tiles(28, settings_button_tiles);

    // Cursor indicator tiles (downward-pointing chevron, 8x16 mode: white body, black border)
    static const uint8_t pause_cursor_tiles[32] = {
        0x7E, 0x7E, 0x7E, 0x42, 0x7E, 0x42, 0x3C, 0x24,
        0x3C, 0x24, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    set_sprite_data(26, 2, pause_cursor_tiles);

    if (_cpu == CGB_TYPE) SPRITES_8x8; else SPRITES_8x16;
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
        if (_cpu == CGB_TYPE) mc_update();
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

        uint8_t credits = (pressed & J_SELECT) != 0;   // SELECT: the credits again
        uint8_t go = credits || (pressed & (J_A | J_START));
        if (go) {
            fade_capture_current();   // the rainbow palette changes: fade out from the current one
            fade_to_black(2);
            menu_stop_irqs(dmg_sky_irq);
            bg_parallax_isr_stop();
            HIDE_SPRITES;
            HIDE_WIN;
            for (uint8_t s = 0; s < 40; s++) hide_sprite(s);
            if (credits) return STATE_CREDITS;
            switch (menu_sel) {
                case SEL_PLAY:
                    if (selected >= NUM_MAIN_LEVELS) selected = 0;
                    return STATE_NEW_MENU_SELECT;
                case SEL_WRENCH:   // the extra levels
                    if (selected < NUM_MAIN_LEVELS) selected = NUM_MAIN_LEVELS;
                    return STATE_LEVEL_SELECT;
                case SEL_MUSIC: return STATE_MUSIC_TEST;
                case SEL_ICON:  return STATE_ICON_SELECT;
                default:        return STATE_SETTINGS;
            }
        }

        frame_counter++;
        ground_x = (uint8_t)(ground_x - GROUND_SPEED) & (GROUND_PERIOD - 1);
    }
}
