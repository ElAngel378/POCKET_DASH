#pragma bank 82
#include <gbdk/incbin.h>

// Columns 768.. of jumper (read through jumper_map's address)
INCBIN(jumper_map_3, "levels/level_data/jumper_3.bin")
INCBIN_EXTERN(jumper_map_3)
