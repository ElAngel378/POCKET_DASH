#pragma bank 36
#include <gbdk/incbin.h>

// Columns 512.. of backontrack (read through backontrack_map's address)
INCBIN(backontrack_map_2, "levels/level_data/backontrack_2.bin")
INCBIN_EXTERN(backontrack_map_2)
