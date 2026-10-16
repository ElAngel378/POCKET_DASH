#pragma bank 126
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_ea_1.c.. (next banks, same address)
INCBIN(electromanadventures_map, "levels/level_data/electromanadventures_0.bin")
INCBIN_EXTERN(electromanadventures_map)
