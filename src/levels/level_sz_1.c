#pragma bank 179
#include <gbdk/incbin.h>

// Columns 256.. of subzero (read through subzero_map's address)
INCBIN(subzero_map_1, "levels/level_data/subzero_1.bin")
INCBIN_EXTERN(subzero_map_1)
