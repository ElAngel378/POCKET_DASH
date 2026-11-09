#pragma bank 196
#include <gbdk/incbin.h>

// Columns 256.. of everymadness (read through everymadness_map's address)
INCBIN(everymadness_map_1, "levels/level_data/everymadness_1.bin")
INCBIN_EXTERN(everymadness_map_1)
