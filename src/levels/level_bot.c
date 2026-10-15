#pragma bank 34
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_bot_1.c.. (next banks, same address)
INCBIN(backontrack_map, "levels/level_data/backontrack_0.bin")
INCBIN_EXTERN(backontrack_map)
