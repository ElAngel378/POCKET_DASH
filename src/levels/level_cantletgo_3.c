#pragma bank 78
#include <gbdk/incbin.h>

// Columns 768.. of cantletgo (read through cantletgo_map's address)
INCBIN(cantletgo_map_3, "levels/level_data/cantletgo_3.bin")
INCBIN_EXTERN(cantletgo_map_3)
