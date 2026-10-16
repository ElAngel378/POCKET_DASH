#pragma bank 216
#include <gbdk/incbin.h>

// Columns 512.. of clubstep (read through clubstep_map's address)
INCBIN(clubstep_map_2, "levels/level_data/clubstep_2.bin")
INCBIN_EXTERN(clubstep_map_2)
