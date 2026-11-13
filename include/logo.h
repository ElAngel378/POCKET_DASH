#ifndef LOGO_H
#define LOGO_H

#include <gb/gb.h>
#include <stdint.h>

#define LOGO_TILE_START 128
#define LOGO_TILE_COUNT 40

extern const uint8_t logo_tiles[];
BANKREF_EXTERN(logo)
void load_logo_tiles(void) BANKED;

#endif // LOGO_H
