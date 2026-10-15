#pragma bank 55
#include <gbdk/incbin.h>

// Columns 768.. of dryout (read through dryout_map's address)
INCBIN(dryout_map_3, "levels/level_data/dryout_3.bin")
INCBIN_EXTERN(dryout_map_3)
