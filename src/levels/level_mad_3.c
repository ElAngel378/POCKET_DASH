#pragma bank 177
#include <gbdk/incbin.h>

// Columns 768.. of madness (read through madness_map's address)
INCBIN(madness_map_3, "levels/level_data/madness_3.bin")
INCBIN_EXTERN(madness_map_3)
