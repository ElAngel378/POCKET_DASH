#pragma bank 215
#include <gbdk/incbin.h>

// Columns 256.. of demonpark (read through demonpark_map's address)
INCBIN(demonpark_map_1, "levels/level_data/demonpark_1.bin")
INCBIN_EXTERN(demonpark_map_1)
