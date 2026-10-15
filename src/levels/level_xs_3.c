#pragma bank 94
#include <gbdk/incbin.h>

// Columns 768.. of xstep (read through xstep_map's address)
INCBIN(xstep_map_3, "levels/level_data/xstep_3.bin")
INCBIN_EXTERN(xstep_map_3)
