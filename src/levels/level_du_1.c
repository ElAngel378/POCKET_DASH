#pragma bank 53
#include <gbdk/incbin.h>

// Columns 256.. of dryout (read through dryout_map's address)
INCBIN(dryout_map_1, "levels/level_data/dryout_1.bin")
INCBIN_EXTERN(dryout_map_1)
