#ifndef COLLISION_H
#define COLLISION_H

#include <gb/gb.h>
#include <stdint.h>

// Collision types mapping to famidash metatiles
#define COL_NONE         0x00  // Air/No collision
#define COL_DEATH_RIGHT  0x01  // Right-facing spike
#define COL_DEATH_LEFT   0x02  // Left-facing spike
#define COL_DEATH_TOP    0x03  // Up-facing spike
#define COL_DEATH_BOTTOM 0x04  // Down-facing spike
#define COL_TOP          0x05  // Solid from top only (platform)
#define COL_BOTTOM       0x06  // Solid from bottom only
#define COL_ALL          0x07  // Full solid block
#define COL_DEATH        0x08  // Hazard (orb/saw)
#define COL_FLOOR_CEIL   0x09  // Solid top and bottom
#define COL_ORB          0x0A  // Yellow Orb
#define COL_PAD          0x0B  // Yellow Pad
#define COL_ORB_BLUE     0x0C  // Blue Orb (Gravity Flip)
#define COL_ORB_MAGENTA  0x0D  // Magenta Orb (Small Jump)
#define COL_PAD_BLUE     0x0E  // Blue Pad (Gravity Flip)
#define COL_PAD_MAGENTA  0x0F  // Magenta Pad (Small Jump)
#define COL_DEATH_TOP_HALF    0x10  // Deadly in bottom half (spike points up)
#define COL_DEATH_BOTTOM_HALF 0x11  // Deadly in top half (spike points down)
#define COL_BREAKABLE    0x16  // Solid block that breaks when the player runs into it (break_block)
// Quarter-tile spikes (one half of a spike split over two tiles, 8px tall).
// bit0 = right half, bit2 = top half. The metatile table stores these; the
// collision lookups turn them into COL_DEATH_LEFT/RIGHT (x half is checked by
// hazard_kills) or COL_NONE (wrong vertical half), so they never reach IS_HAZARD.
#define COL_DEATH_LEFT_BOTTOMQ  0x12
#define COL_DEATH_RIGHT_BOTTOMQ 0x13
#define COL_DEATH_LEFT_TOPQ     0x14
#define COL_DEATH_RIGHT_TOPQ    0x15
// Quadrant types (half/quarter solid blocks, spike blocks, half spikes): each 8x8
// quadrant of the tile is solid, deadly or empty. See col_quads[] in player.c.
#define COL_QUAD_BASE  0x20
#define COL_QUAD_COUNT 26
// Box types (the 1.6 slabs, hitboxes as in Geometry Dash Advance): solid inside one or two
// rectangles of the 16x16 metatile. See col_boxes[] in player.c.
#define COL_BOX_BASE   0x40
#define COL_BOX_COUNT  20
#define COL_SLAB_MED_TOP       (COL_BOX_BASE + 0)    // x 2..13, y 0..7
#define COL_SLAB_MED_BOTTOM    (COL_BOX_BASE + 1)    // x 2..13, y 8..15
#define COL_SLAB_MED_LEFT      (COL_BOX_BASE + 2)    // x 0..7, y 2..13
#define COL_SLAB_MED_RIGHT     (COL_BOX_BASE + 3)    // x 8..15, y 2..13
#define COL_SLAB_TINY_TOP      (COL_BOX_BASE + 4)    // x 5..10, y 0..7
#define COL_SLAB_TINY_BOTTOM   (COL_BOX_BASE + 5)
#define COL_SLAB_TINY_LEFT     (COL_BOX_BASE + 6)    // x 0..7, y 5..10
#define COL_SLAB_TINY_RIGHT    (COL_BOX_BASE + 7)
#define COL_EA_SLAB_TOP        (COL_BOX_BASE + 8)    // Electroman ball rails: 10 px of 16
#define COL_EA_SLAB_BOTTOM     (COL_BOX_BASE + 9)
#define COL_EA_SLAB_LEFT       (COL_BOX_BASE + 10)
#define COL_EA_SLAB_RIGHT      (COL_BOX_BASE + 11)
#define COL_EA_CORNER_TL       (COL_BOX_BASE + 12)   // 10x10 in that corner
#define COL_EA_CORNER_TR       (COL_BOX_BASE + 13)
#define COL_EA_CORNER_BL       (COL_BOX_BASE + 14)
#define COL_EA_CORNER_BR       (COL_BOX_BASE + 15)
#define COL_EA_INSIDE_TL       (COL_BOX_BASE + 16)   // the two rails of an inside corner
#define COL_EA_INSIDE_TR       (COL_BOX_BASE + 17)
#define COL_EA_INSIDE_BL       (COL_BOX_BASE + 18)
#define COL_EA_INSIDE_BR       (COL_BOX_BASE + 19)
// GDA's full slabs are COL_TOP / COL_BOTTOM (top / bottom half) and the quadrant types
// COL_QUAD_BASE + 0 / + 1 (left / right half)
// Saws: Famidash's spike types (COL_DEATH, COL_DEATH_TOP / BOTTOM / LEFT / RIGHT, the diagonal
// spike quadrants), so they kill where Famidash's do, plus a core circle in the middle of each
// saw that closes the holes in those zones (saw_core_* in player.c)

#define IS_SOLID(col)  ((col) == COL_ALL || (col) == COL_FLOOR_CEIL || \
                        (col) == COL_TOP || (col) == COL_BOTTOM)

#define IS_HAZARD(col) ((col) == COL_DEATH      || \
                        (col) == COL_DEATH_TOP   || \
                        (col) == COL_DEATH_BOTTOM|| \
                        (col) == COL_DEATH_LEFT  || \
                        (col) == COL_DEATH_RIGHT || \
                        (col) == COL_DEATH_TOP_HALF || \
                        (col) == COL_DEATH_BOTTOM_HALF)

#define IS_ORB(col)    ((col) == COL_ORB || (col) == COL_ORB_BLUE || (col) == COL_ORB_MAGENTA)
#define IS_PAD(col)    ((col) == COL_PAD || (col) == COL_PAD_BLUE || (col) == COL_PAD_MAGENTA)

#define IS_PASSTHROUGH(col) ((col) == COL_NONE)

// The level's collision type per metatile (its own table: tools/build_bg_tiles.py bg_level_col,
// copied here by apply_level_bg_tiles)
extern uint8_t level_mt_col[256];

#define col_of(tile_id) (level_mt_col[(tile_id)])

struct hUGESong_t;

// Checks collision at a world coordinate (Bank 0, handles switching)
uint8_t col_at(
    uint16_t world_px,
    int16_t  world_py,
    const uint8_t *map,
    uint16_t map_w,
    uint8_t  map_bank
);

// Raw collision check (No bank switching, must be inside begin/end)
uint8_t col_at_raw(
    uint16_t world_px,
    int16_t  world_py,
    const uint8_t *map,
    uint16_t map_w
);

// Fast version using pre-calculated column pointer
uint8_t col_at_raw_cached(const uint8_t *col_ptr, uint16_t world_py);

// Batch collision context: switches to the map bank once.
// Level maps: MAP_ROWS metatile rows per column, MAP_BANK_COLS columns (16 KB) per ROM bank, in
// consecutive banks (see collision.c and tools/build_levels.py, which must use the same values)
#define MAP_ROWS_SHIFT 6
#define MAP_ROWS (1u << MAP_ROWS_SHIFT)
#define MAP_BANK_COLS_SHIFT 8
#define MAP_BANK_COLS (1u << MAP_BANK_COLS_SHIFT)
// CGB: the row below the map shows the ground strip
#define GROUND_ROW MAP_ROWS

void col_at_begin(uint8_t map_bank);
void col_at_end(void);

// Safe music initialization from Bank 0
void init_music_banked(const struct hUGESong_t * song, uint8_t bank, uint8_t divider);

void get_map_column(uint16_t map_col, const uint8_t *map, uint8_t map_bank, uint8_t *dest, uint8_t band);
uint8_t get_map_tile(uint16_t col, uint8_t row, const uint8_t *map, uint8_t map_bank);
void get_map_row_slots(uint8_t first, uint8_t n, uint8_t row, uint16_t loaded_r, uint8_t reversed,
                       const uint8_t *map, uint16_t map_w, uint8_t map_bank, uint8_t *out);

void prepare_mt_column(uint16_t map_col, const uint8_t* map, uint8_t map_bank, uint8_t reversed) BANKED;
void flush_mt_column(uint8_t ring_col) BANKED;
void prepare_mt_column_slice(uint16_t map_col, const uint8_t* map, uint8_t map_bank, uint8_t reversed, uint8_t step) BANKED;
void refetch_mt_column(const uint8_t* map, uint8_t map_bank) BANKED;
extern uint8_t mt_spike_b;
// Level background tiles (tools/build_bg_tiles.py): the sheet (`tiles`, the base sheet in this
// orientation, or the level's ready-made DMG one), then apply_level_bg_tiles on top of it
void load_level_bg_sheet(uint8_t level, uint8_t reversed, const uint8_t *tiles, uint16_t count, uint8_t bank) BANKED;
void apply_level_bg_tiles(uint8_t level, uint8_t reversed) BANKED;
void reapply_bg_shared_slots(uint8_t level, uint8_t reversed) BANKED;
// CGB: mirror mode by the X flip attribute (normal tiles), see mt_renderer.c. Set per level
// before its tiles are loaded.
extern uint8_t mt_cgb_flip;
void bg_level_set_flip_mode(uint8_t level) BANKED;
// CGB seamless mirror portal: build a map column / row into the BG map at map_hi << 8 with the
// display on (HBlank-synced writes)
// DMG mirror portal, display off: mirror the 0x9800 map in place
void bg_map_reflect(void) BANKED;
void mj_build_column(uint16_t map_col, const uint8_t* map, uint8_t map_bank, uint8_t reversed, uint8_t map_hi) BANKED;
void mj_build_row(uint8_t row, uint16_t loaded_r, const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed, uint8_t map_hi) BANKED;
// Saw animation (see mt_renderer.c): call before waiting for VBlank, the VBlank handler uploads
void saw_anim_request(void) BANKED;
extern uint8_t saw_on;   // the level has saws (mt_renderer.c)
void flush_mt_column_slice(uint8_t ring_col, uint8_t step) BANKED;
void request_mt_column_slice(uint8_t ring_col, uint8_t step) BANKED;
// Vertical streaming: the VRAM ring (16 metatile rows) holds map rows mt_band .. mt_band+15,
// map row m in VRAM metatile row m & 15. A row entering the band is written with the row job:
void request_row_slots(uint8_t first, uint8_t row, uint16_t loaded_r, const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed) BANKED;
void flush_row_slots(uint8_t first, uint8_t count, uint8_t row, uint16_t loaded_r, const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed) BANKED;
// CGB: rewrites the ground row if the band shows it (after the pause box changed its attributes)
void flush_ground_row(void) BANKED;
void ground_below_fill(uint8_t scy) BANKED;   // the row under the ground (level end shake), CGB
extern uint8_t mt_band;

void fill_scroll_bg(const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed) BANKED;

void vram_copy(uint8_t *dst, const uint8_t *src, uint16_t n);   // display off, n % 16 == 0, 16-aligned dst
void load_bkg_tileset(const uint8_t* tiles, uint16_t tile_count, uint8_t bank);
void load_collision_columns(uint16_t map_col, const uint8_t* map,
                            uint16_t map_w, uint8_t map_bank,
                            uint8_t* columns, uint8_t row0);

// Breakable blocks the player broke: these map cells read as empty (metatile 0) in the map
// fetches above, until broken_reset (level start, respawn). break_block also asks gameplay.c to
// redraw the column (broken_redraw, 0xFFFF: none).
void break_block(uint16_t col, uint8_t row);
void broken_reset(void);
extern uint16_t broken_redraw;

void load_menu_ground_tiles(void) BANKED;


#endif // COLLISION_H
