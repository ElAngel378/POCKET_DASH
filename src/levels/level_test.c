#pragma bank 229
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_test_1.c.. (next banks, same address)
INCBIN(test_map, "levels/level_data/test_0.bin")
INCBIN_EXTERN(test_map)
