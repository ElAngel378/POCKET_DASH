#pragma bank 33
#include <gbdk/incbin.h>

// Columns 768.. of stereomadness (read through stereomadness_map's address)
INCBIN(stereomadness_map_3, "levels/level_data/stereomadness_3.bin")
INCBIN_EXTERN(stereomadness_map_3)
