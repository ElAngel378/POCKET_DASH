#pragma bank 81
#include <gbdk/incbin.h>

// Columns 512.. of jumper (read through jumper_map's address)
INCBIN(jumper_map_2, "levels/level_data/jumper_2.bin")
INCBIN_EXTERN(jumper_map_2)
