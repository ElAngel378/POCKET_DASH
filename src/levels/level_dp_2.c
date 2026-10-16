#pragma bank 184
#include <gbdk/incbin.h>

// Columns 512.. of demonpark (read through demonpark_map's address)
INCBIN(demonpark_map_2, "levels/level_data/demonpark_2.bin")
INCBIN_EXTERN(demonpark_map_2)
