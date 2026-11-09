#pragma bank 187
#include <gbdk/incbin.h>

// Columns 768.. of leveleasy (read through leveleasy_map's address)
INCBIN(leveleasy_map_3, "levels/level_data/leveleasy_3.bin")
INCBIN_EXTERN(leveleasy_map_3)
