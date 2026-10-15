#pragma bank 54
#include <gbdk/incbin.h>

// Columns 512.. of dryout (read through dryout_map's address)
INCBIN(dryout_map_2, "levels/level_data/dryout_2.bin")
INCBIN_EXTERN(dryout_map_2)
