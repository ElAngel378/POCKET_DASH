#ifndef ASSETS_H
#define ASSETS_H

#include <gb/gb.h>
#include <stdint.h>
#include "hUGEDriver.h"

#ifndef SPDEF_TYPE
#define SPDEF_TYPE
typedef struct {
    uint16_t x;   // X coordinate in pixels
    uint16_t y;   // Y coordinate in pixels
    uint8_t  obj; // Object ID (0=Cube, 1=Ship, 10=Yellow Pad, etc.)
} SpDef;

#define MAX_ACTIVE_SP_OBJECTS 16

/* SpCache: Struct of Arrays (SoA) for optimized Game Boy access */
typedef struct {
    uint8_t  obj[MAX_ACTIVE_SP_OBJECTS];
    uint16_t px[MAX_ACTIVE_SP_OBJECTS];
    uint16_t py[MAX_ACTIVE_SP_OBJECTS];
    uint8_t  active[MAX_ACTIVE_SP_OBJECTS];
    uint8_t  activated[MAX_ACTIVE_SP_OBJECTS];
} SpCache;
#endif

// Structure defining a game level's data and metadata
typedef struct {
  const char *name;
  const uint8_t *tiles;     // VRAM tile data
  const uint8_t *tiles_rev; // Flipped tileset for mirror mode
  const uint8_t *map;       // Metatile map data
  uint16_t tile_count;      // Total tiles in tileset
  uint16_t map_width;   // Width in metatiles
  uint16_t map_height;  // Height in metatiles
  uint8_t tiles_are_compressed;
  uint8_t map_is_compressed;
  uint8_t map_bank;     // ROM bank of the map's first 1024 columns (the rest follow in the next banks)
  uint8_t timer_divider; // The TMA_REG value for hUGEDriver
  const SpDef *sp_list;
  uint8_t sp_bank;
  const SpDef *sp_list_dmg;
  uint8_t map_top;      // first map row of the level (rows above it are padding, see build_levels.py)
  uint8_t bg_color;     // Starting background NES color ID from metadata
  uint8_t g_color;      // Starting ground NES color ID from metadata
} Level;

// Per-level song pointers (same order as game_levels[]; NULL = silent)
extern const hUGESong_t * const level_songs[];

// Per-level song bank (matches level_songs[]; 0 = silent)
extern const uint8_t song_bank[];

extern const Level * const game_levels[];
BANKREF_EXTERN(game_levels)

extern const uint8_t chr_gb_tiles[];
BANKREF_EXTERN(chr_gb)

extern const uint8_t MAX_LEVELS;
// Save slot of each level (game_levels order): a level keeps its saved progress and coins
// when the level order changes
extern const uint8_t level_save_slot[];

#endif
