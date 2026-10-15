#pragma bank 48
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_pg_1.c.. (next banks, same address)
INCBIN(polargeist_map, "levels/level_data/polargeist_0.bin")
INCBIN_EXTERN(polargeist_map)
