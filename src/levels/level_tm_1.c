#pragma bank 84
#include <gbdk/incbin.h>

// Columns 256.. of timemachine (read through timemachine_map's address)
INCBIN(timemachine_map_1, "levels/level_data/timemachine_1.bin")
INCBIN_EXTERN(timemachine_map_1)
