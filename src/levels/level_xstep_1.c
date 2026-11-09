#pragma bank 92
#include <gbdk/incbin.h>

// Columns 256.. of xstep (read through xstep_map's address)
INCBIN(xstep_map_1, "levels/level_data/xstep_1.bin")
INCBIN_EXTERN(xstep_map_1)
