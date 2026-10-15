#pragma bank 30
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_sm_1.c.. (next banks, same address)
INCBIN(stereomadness_map, "levels/level_data/stereomadness_0.bin")
INCBIN_EXTERN(stereomadness_map)
