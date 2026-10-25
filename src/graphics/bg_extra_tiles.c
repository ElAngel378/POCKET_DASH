#pragma bank 16

// The level backgrounds' extra tiles (tools/build_bg_tiles.py, per tile normal + mirrored: 32
// bytes): copied by bg_extra_copy (bg_parallax_phases.c) and the DMG saw animation
#include <gbdk/platform.h>
#include <gbdk/incbin.h>

INCBIN(bg_extra_tiles, "levels/chr_data/bg_extra_tiles.bin")
