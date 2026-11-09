#pragma bank 88
#include <gbdk/incbin.h>

// Columns 256.. of cycles (read through cycles_map's address)
INCBIN(cycles_map_1, "levels/level_data/cycles_1.bin")
INCBIN_EXTERN(cycles_map_1)
