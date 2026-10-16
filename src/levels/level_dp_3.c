#pragma bank 196
#include <gbdk/incbin.h>

// Columns 768.. of demonpark (read through demonpark_map's address)
INCBIN(demonpark_map_3, "levels/level_data/demonpark_3.bin")
INCBIN_EXTERN(demonpark_map_3)
