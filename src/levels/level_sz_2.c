#pragma bank 190
#include <gbdk/incbin.h>

// Columns 512.. of subzero (read through subzero_map's address)
INCBIN(subzero_map_2, "levels/level_data/subzero_2.bin")
INCBIN_EXTERN(subzero_map_2)
