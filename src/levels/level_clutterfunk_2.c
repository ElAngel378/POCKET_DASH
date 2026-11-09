#pragma bank 97
#include <gbdk/incbin.h>

// Columns 512.. of clutterfunk (read through clutterfunk_map's address)
INCBIN(clutterfunk_map_2, "levels/level_data/clutterfunk_2.bin")
INCBIN_EXTERN(clutterfunk_map_2)
