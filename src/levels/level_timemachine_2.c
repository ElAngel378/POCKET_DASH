#pragma bank 85
#include <gbdk/incbin.h>

// Columns 512.. of timemachine (read through timemachine_map's address)
INCBIN(timemachine_map_2, "levels/level_data/timemachine_2.bin")
INCBIN_EXTERN(timemachine_map_2)
