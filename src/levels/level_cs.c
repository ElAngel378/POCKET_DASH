#pragma bank 214
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_cs_1.c.. (next banks, same address)
INCBIN(clubstep_map, "levels/level_data/clubstep_0.bin")
INCBIN_EXTERN(clubstep_map)
