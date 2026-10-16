#pragma bank 216
#include <gbdk/incbin.h>

// Columns 512.. of thelightningroad (read through thelightningroad_map's address)
INCBIN(thelightningroad_map_2, "levels/level_data/thelightningroad_2.bin")
INCBIN_EXTERN(thelightningroad_map_2)
