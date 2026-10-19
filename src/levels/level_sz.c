#pragma bank 186
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_sz_1.c.. (next banks, same address)
INCBIN(subzero_map, "levels/level_data/subzero_0.bin")
INCBIN_EXTERN(subzero_map)
