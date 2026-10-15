#pragma bank 58
#include <gbdk/incbin.h>

// Columns 512.. of baseafterbase (read through baseafterbase_map's address)
INCBIN(baseafterbase_map_2, "levels/level_data/baseafterbase_2.bin")
INCBIN_EXTERN(baseafterbase_map_2)
