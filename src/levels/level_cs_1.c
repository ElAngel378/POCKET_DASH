#pragma bank 179
#include <gbdk/incbin.h>

// Columns 256.. of clubstep (read through clubstep_map's address)
INCBIN(clubstep_map_1, "levels/level_data/clubstep_1.bin")
INCBIN_EXTERN(clubstep_map_1)
