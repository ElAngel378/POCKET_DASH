#pragma bank 195
#include <gbdk/incbin.h>

// Columns 512.. of ultiatedestruction (read through ultiatedestruction_map's address)
INCBIN(ultiatedestruction_map_2, "levels/level_data/ultiatedestruction_2.bin")
INCBIN_EXTERN(ultiatedestruction_map_2)
