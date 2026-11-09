#pragma bank 50
#include <gbdk/incbin.h>

// Columns 512.. of polargeist (read through polargeist_map's address)
INCBIN(polargeist_map_2, "levels/level_data/polargeist_2.bin")
INCBIN_EXTERN(polargeist_map_2)
