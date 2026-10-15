#pragma bank 90
#include <gbdk/incbin.h>

// Columns 768.. of cycles (read through cycles_map's address)
INCBIN(cycles_map_3, "levels/level_data/cycles_3.bin")
INCBIN_EXTERN(cycles_map_3)
