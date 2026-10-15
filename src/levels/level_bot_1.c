#pragma bank 35
#include <gbdk/incbin.h>

// Columns 256.. of backontrack (read through backontrack_map's address)
INCBIN(backontrack_map_1, "levels/level_data/backontrack_1.bin")
INCBIN_EXTERN(backontrack_map_1)
