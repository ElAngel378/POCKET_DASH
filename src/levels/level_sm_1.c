#pragma bank 31
#include <gbdk/incbin.h>

// Columns 256.. of stereomadness (read through stereomadness_map's address)
INCBIN(stereomadness_map_1, "levels/level_data/stereomadness_1.bin")
INCBIN_EXTERN(stereomadness_map_1)
