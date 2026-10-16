#pragma bank 167
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_mad_1.c.. (next banks, same address)
INCBIN(madness_map, "levels/level_data/madness_0.bin")
INCBIN_EXTERN(madness_map)
