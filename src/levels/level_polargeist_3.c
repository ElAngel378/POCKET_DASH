#pragma bank 51
#include <gbdk/incbin.h>

// Columns 768.. of polargeist (read through polargeist_map's address)
INCBIN(polargeist_map_3, "levels/level_data/polargeist_3.bin")
INCBIN_EXTERN(polargeist_map_3)
