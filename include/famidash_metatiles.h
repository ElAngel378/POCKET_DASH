#ifndef FAMIDASH_METATILES_H
#define FAMIDASH_METATILES_H

#include <stdint.h>

#define FAMIDASH_NUM_METATILES 256
#define NUM_METATILES FAMIDASH_NUM_METATILES

// No global metatile tables: every level has its own metatile, collision and palette tables
// (tools/build_bg_tiles.py: bg_level_mt_*.c), loaded by apply_level_bg_tiles. The tool reads the
// Famidash tables in famidash_metatiles_dmg.c (not compiled).

#endif /* FAMIDASH_METATILES_H */
