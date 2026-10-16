#pragma bank 196
#include <gbdk/incbin.h>

// Columns 768.. of clubstep (read through clubstep_map's address)
INCBIN(clubstep_map_3, "levels/level_data/clubstep_3.bin")
INCBIN_EXTERN(clubstep_map_3)
