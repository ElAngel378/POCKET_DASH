#pragma bank 185
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_le_1.c.. (next banks, same address)
INCBIN(leveleasy_map, "levels/level_data/leveleasy_0.bin")
INCBIN_EXTERN(leveleasy_map)
