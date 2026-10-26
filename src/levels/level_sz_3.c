#pragma bank 191
#include <gbdk/incbin.h>

// Columns 768.. of subzero (read through subzero_map's address)
INCBIN(subzero_map_3, "levels/level_data/subzero_3.bin")
INCBIN_EXTERN(subzero_map_3)
