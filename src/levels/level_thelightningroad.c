#pragma bank 188
#include <gbdk/incbin.h>

// Columns 0..255; the rest are in level_thelightningroad_1.c.. (next banks, same address)
INCBIN(thelightningroad_map, "levels/level_data/thelightningroad_0.bin")
INCBIN_EXTERN(thelightningroad_map)
