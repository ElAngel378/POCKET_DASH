#pragma bank 185
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_em_1.c.. (next banks, same address)
INCBIN(everymadness_map, "levels/level_data/everymadness_0.bin")
INCBIN_EXTERN(everymadness_map)
