#pragma bank 115
#include <gbdk/incbin.h>

// Columns 256.. of ultiatedestruction (read through ultiatedestruction_map's address)
INCBIN(ultiatedestruction_map_1, "levels/level_data/ultiatedestruction_1.bin")
INCBIN_EXTERN(ultiatedestruction_map_1)
