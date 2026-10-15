#pragma bank 75
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_clg_1.c.. (next banks, same address)
INCBIN(cantletgo_map, "levels/level_data/cantletgo_0.bin")
INCBIN_EXTERN(cantletgo_map)
