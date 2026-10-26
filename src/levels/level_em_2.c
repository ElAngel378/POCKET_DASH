#pragma bank 197
#include <gbdk/incbin.h>

// Columns 512.. of everymadness (read through everymadness_map's address)
INCBIN(everymadness_map_2, "levels/level_data/everymadness_2.bin")
INCBIN_EXTERN(everymadness_map_2)
