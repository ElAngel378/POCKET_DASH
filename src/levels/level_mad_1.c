#pragma bank 175
#include <gbdk/incbin.h>

// Columns 256.. of madness (read through madness_map's address)
INCBIN(madness_map_1, "levels/level_data/madness_1.bin")
INCBIN_EXTERN(madness_map_1)
