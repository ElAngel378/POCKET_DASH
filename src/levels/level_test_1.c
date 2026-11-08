#pragma bank 230
#include <gbdk/incbin.h>

// Columns 256.. of test (read through test_map's address)
INCBIN(test_map_1, "levels/level_data/test_1.bin")
INCBIN_EXTERN(test_map_1)
