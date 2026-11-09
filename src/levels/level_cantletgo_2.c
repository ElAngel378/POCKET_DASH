#pragma bank 77
#include <gbdk/incbin.h>

// Columns 512.. of cantletgo (read through cantletgo_map's address)
INCBIN(cantletgo_map_2, "levels/level_data/cantletgo_2.bin")
INCBIN_EXTERN(cantletgo_map_2)
