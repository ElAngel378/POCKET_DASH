#pragma bank 59
#include <gbdk/incbin.h>

// Columns 768.. of baseafterbase (read through baseafterbase_map's address)
INCBIN(baseafterbase_map_3, "levels/level_data/baseafterbase_3.bin")
INCBIN_EXTERN(baseafterbase_map_3)
