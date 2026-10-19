#pragma bank 224
#include <gbdk/incbin.h>

// Columns 256.. of thelightningroad (read through thelightningroad_map's address)
INCBIN(thelightningroad_map_1, "levels/level_data/thelightningroad_1.bin")
INCBIN_EXTERN(thelightningroad_map_1)
