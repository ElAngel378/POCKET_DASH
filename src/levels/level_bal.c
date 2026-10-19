#pragma bank 179
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_bal_1.c.. (next banks, same address)
INCBIN(bestautomaticlvl_map, "levels/level_data/bestautomaticlvl_0.bin")
INCBIN_EXTERN(bestautomaticlvl_map)
