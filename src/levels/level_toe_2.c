#pragma bank 123
#include <gbdk/incbin.h>

// Columns 512.. of theoryofeverything (read through theoryofeverything_map's address)
INCBIN(theoryofeverything_map_2, "levels/level_data/theoryofeverything_2.bin")
INCBIN_EXTERN(theoryofeverything_map_2)
