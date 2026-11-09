#pragma bank 80
#include <gbdk/incbin.h>

// Columns 256.. of jumper (read through jumper_map's address)
INCBIN(jumper_map_1, "levels/level_data/jumper_1.bin")
INCBIN_EXTERN(jumper_map_1)
