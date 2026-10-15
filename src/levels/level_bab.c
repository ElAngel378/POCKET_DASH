#pragma bank 56
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_bab_1.c.. (next banks, same address)
INCBIN(baseafterbase_map, "levels/level_data/baseafterbase_0.bin")
INCBIN_EXTERN(baseafterbase_map)
