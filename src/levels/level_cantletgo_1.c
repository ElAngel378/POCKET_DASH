#pragma bank 76
#include <gbdk/incbin.h>

// Columns 256.. of cantletgo (read through cantletgo_map's address)
INCBIN(cantletgo_map_1, "levels/level_data/cantletgo_1.bin")
INCBIN_EXTERN(cantletgo_map_1)
