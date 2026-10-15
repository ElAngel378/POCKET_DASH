#pragma bank 91
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_xs_1.c.. (next banks, same address)
INCBIN(xstep_map, "levels/level_data/xstep_0.bin")
INCBIN_EXTERN(xstep_map)
