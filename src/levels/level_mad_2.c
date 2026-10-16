#pragma bank 172
#include <gbdk/incbin.h>

// Columns 512.. of madness (read through madness_map's address)
INCBIN(madness_map_2, "levels/level_data/madness_2.bin")
INCBIN_EXTERN(madness_map_2)
