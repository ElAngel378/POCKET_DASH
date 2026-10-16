#pragma bank 125
#include <gbdk/incbin.h>

// Columns 768.. of theoryofeverything (read through theoryofeverything_map's address)
INCBIN(theoryofeverything_map_3, "levels/level_data/theoryofeverything_3.bin")
INCBIN_EXTERN(theoryofeverything_map_3)
