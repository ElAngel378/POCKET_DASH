#pragma bank 185
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_dp_1.c.. (next banks, same address)
INCBIN(demonpark_map, "levels/level_data/demonpark_0.bin")
INCBIN_EXTERN(demonpark_map)
