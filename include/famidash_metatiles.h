#ifndef FAMIDASH_METATILES_H
#define FAMIDASH_METATILES_H

#include <stdint.h>

#define FAMIDASH_NUM_METATILES 256
#define NUM_METATILES FAMIDASH_NUM_METATILES

extern const uint8_t metatiles[FAMIDASH_NUM_METATILES][4];
extern const uint8_t famidash_metatile_palettes[FAMIDASH_NUM_METATILES];
extern const uint8_t famidash_metatile_collision[FAMIDASH_NUM_METATILES];

#endif /* FAMIDASH_METATILES_H */
