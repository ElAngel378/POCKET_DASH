#pragma bank 87
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_cy_1.c.. (next banks, same address)
INCBIN(cycles_map, "levels/level_data/cycles_0.bin")
INCBIN_EXTERN(cycles_map)
