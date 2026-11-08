#pragma bank 231
#include <gbdk/incbin.h>

// Columns 512.. of test (read through test_map's address)
INCBIN(test_map_2, "levels/level_data/test_2.bin")
INCBIN_EXTERN(test_map_2)
