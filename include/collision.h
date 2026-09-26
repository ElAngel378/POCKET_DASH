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

extern const uint8_t famidash_metatile_collision[256];

#define col_of(tile_id) (famidash_metatile_collision[(tile_id)])

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
void col_at_begin(uint8_t map_bank);
void col_at_end(void);

// Safe music initialization from Bank 0
void init_music_banked(const struct hUGESong_t * song, uint8_t bank, uint8_t divider);

void get_map_column(uint16_t map_col, const uint8_t *map, uint8_t map_bank, uint8_t *dest);

void prepare_mt_column(uint16_t map_col, const uint8_t* map, uint8_t map_bank, uint8_t reversed) BANKED;
void flush_mt_column(uint8_t ring_col) BANKED;

void fill_scroll_bg(const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed) BANKED;

void load_bkg_tileset(const uint8_t* tiles, uint16_t tile_count, uint8_t bank);
void load_collision_columns(uint16_t map_col, const uint8_t* map,
                            uint16_t map_w, uint8_t map_bank,
                            uint8_t* columns);

extern uint8_t vram_row0_is_ground;
void load_menu_ground_tiles(void) BANKED;
void prepare_row0_level_tiles(uint16_t loaded_r, const uint8_t* map, uint16_t map_w, uint8_t map_bank, uint8_t reversed) BANKED;
void flush_vram_row0(uint8_t is_ground) BANKED;

#endif // COLLISION_H
