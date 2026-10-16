#pragma bank 128
#include <gbdk/incbin.h>

// Columns 512.. of electromanadventures (read through electromanadventures_map's address)
INCBIN(electromanadventures_map_2, "levels/level_data/electromanadventures_2.bin")
INCBIN_EXTERN(electromanadventures_map_2)
