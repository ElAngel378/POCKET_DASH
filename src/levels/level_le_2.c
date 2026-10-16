#pragma bank 176
#include <gbdk/incbin.h>

// Columns 512.. of leveleasy (read through leveleasy_map's address)
INCBIN(leveleasy_map_2, "levels/level_data/leveleasy_2.bin")
INCBIN_EXTERN(leveleasy_map_2)
