#pragma bank 175
#include <gbdk/incbin.h>

// Columns 256.. of leveleasy (read through leveleasy_map's address)
INCBIN(leveleasy_map_1, "levels/level_data/leveleasy_1.bin")
INCBIN_EXTERN(leveleasy_map_1)
