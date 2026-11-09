#pragma bank 86
#include <gbdk/incbin.h>

// Columns 768.. of timemachine (read through timemachine_map's address)
INCBIN(timemachine_map_3, "levels/level_data/timemachine_3.bin")
INCBIN_EXTERN(timemachine_map_3)
