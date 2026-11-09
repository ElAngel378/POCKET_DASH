#pragma bank 93
#include <gbdk/incbin.h>

// Columns 512.. of xstep (read through xstep_map's address)
INCBIN(xstep_map_2, "levels/level_data/xstep_2.bin")
INCBIN_EXTERN(xstep_map_2)
