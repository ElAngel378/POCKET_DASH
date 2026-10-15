#pragma bank 95
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_cf_1.c.. (next banks, same address)
INCBIN(clutterfunk_map, "levels/level_data/clutterfunk_0.bin")
INCBIN_EXTERN(clutterfunk_map)
