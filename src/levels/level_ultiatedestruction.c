#pragma bank 113
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_ultiatedestruction_1.c.. (next banks, same address)
INCBIN(ultiatedestruction_map, "levels/level_data/ultiatedestruction_0.bin")
INCBIN_EXTERN(ultiatedestruction_map)
