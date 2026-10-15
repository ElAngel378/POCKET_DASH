#pragma bank 89
#include <gbdk/incbin.h>

// Columns 512.. of cycles (read through cycles_map's address)
INCBIN(cycles_map_2, "levels/level_data/cycles_2.bin")
INCBIN_EXTERN(cycles_map_2)
