#pragma bank 96
#include <gbdk/incbin.h>

// Columns 256.. of clutterfunk (read through clutterfunk_map's address)
INCBIN(clutterfunk_map_1, "levels/level_data/clutterfunk_1.bin")
INCBIN_EXTERN(clutterfunk_map_1)
