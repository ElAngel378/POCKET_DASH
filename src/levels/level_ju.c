#pragma bank 79
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_ju_1.c.. (next banks, same address)
INCBIN(jumper_map, "levels/level_data/jumper_0.bin")
INCBIN_EXTERN(jumper_map)
