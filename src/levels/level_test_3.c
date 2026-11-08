#pragma bank 232
#include <gbdk/incbin.h>

// Columns 768.. of test (read through test_map's address)
INCBIN(test_map_3, "levels/level_data/test_3.bin")
INCBIN_EXTERN(test_map_3)
