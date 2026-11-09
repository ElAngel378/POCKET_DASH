#pragma bank 37
#include <gbdk/incbin.h>

// Columns 768.. of backontrack (read through backontrack_map's address)
INCBIN(backontrack_map_3, "levels/level_data/backontrack_3.bin")
INCBIN_EXTERN(backontrack_map_3)
