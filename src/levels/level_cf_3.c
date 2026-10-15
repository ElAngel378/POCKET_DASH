#pragma bank 98
#include <gbdk/incbin.h>

// Columns 768.. of clutterfunk (read through clutterfunk_map's address)
INCBIN(clutterfunk_map_3, "levels/level_data/clutterfunk_3.bin")
INCBIN_EXTERN(clutterfunk_map_3)
