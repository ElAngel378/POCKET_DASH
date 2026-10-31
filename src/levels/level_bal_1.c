#pragma bank 183
#include <gbdk/incbin.h>

// Columns 256.. of bestautomaticlvl (read through bestautomaticlvl_map's address)
INCBIN(bestautomaticlvl_map_1, "levels/level_data/bestautomaticlvl_1.bin")
INCBIN_EXTERN(bestautomaticlvl_map_1)
