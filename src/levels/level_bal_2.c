#pragma bank 184
#include <gbdk/incbin.h>

// Columns 512.. of bestautomaticlvl (read through bestautomaticlvl_map's address)
INCBIN(bestautomaticlvl_map_2, "levels/level_data/bestautomaticlvl_2.bin")
INCBIN_EXTERN(bestautomaticlvl_map_2)
