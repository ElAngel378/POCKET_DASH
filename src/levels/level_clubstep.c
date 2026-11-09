#pragma bank 145
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_clubstep_1.c.. (next banks, same address)
INCBIN(clubstep_map, "levels/level_data/clubstep_0.bin")
INCBIN_EXTERN(clubstep_map)
