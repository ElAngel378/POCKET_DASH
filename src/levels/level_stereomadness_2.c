#pragma bank 32
#include <gbdk/incbin.h>

// Columns 512.. of stereomadness (read through stereomadness_map's address)
INCBIN(stereomadness_map_2, "levels/level_data/stereomadness_2.bin")
INCBIN_EXTERN(stereomadness_map_2)
