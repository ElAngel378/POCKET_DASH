#pragma bank 174
#include <gbdk/incbin.h>

// Columns 256.. of electromanadventures (read through electromanadventures_map's address)
INCBIN(electromanadventures_map_1, "levels/level_data/electromanadventures_1.bin")
INCBIN_EXTERN(electromanadventures_map_1)
