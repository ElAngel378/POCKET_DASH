#pragma bank 122
#include <gbdk/incbin.h>

// Columns 256.. of theoryofeverything (read through theoryofeverything_map's address)
INCBIN(theoryofeverything_map_1, "levels/level_data/theoryofeverything_1.bin")
INCBIN_EXTERN(theoryofeverything_map_1)
