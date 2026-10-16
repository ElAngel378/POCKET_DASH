#pragma bank 122
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_toe_1.c.. (next banks, same address)
INCBIN(theoryofeverything_map, "levels/level_data/theoryofeverything_0.bin")
INCBIN_EXTERN(theoryofeverything_map)
