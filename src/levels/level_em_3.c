#pragma bank 188
#include <gbdk/incbin.h>

// Columns 768.. of everymadness (read through everymadness_map's address)
INCBIN(everymadness_map_3, "levels/level_data/everymadness_3.bin")
INCBIN_EXTERN(everymadness_map_3)
