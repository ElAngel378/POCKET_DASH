#pragma bank 83
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_tm_1.c.. (next banks, same address)
INCBIN(timemachine_map, "levels/level_data/timemachine_0.bin")
INCBIN_EXTERN(timemachine_map)
