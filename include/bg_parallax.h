#ifndef BG_PARALLAX_H
#define BG_PARALLAX_H

#include <gb/gb.h>
#include <stdint.h>

#define BG_PARALLAX_TILE_BASE 0  // In VRAM Bank 1 (tiles 0..47)
#define BG_PARALLAX_NUM_TILES 48

BANKREF_EXTERN(bg_parallax_data_0)
BANKREF_EXTERN(bg_parallax_data_1)
BANKREF_EXTERN(bg_parallax_data_2)
BANKREF_EXTERN(bg_parallax_data_3)

extern const uint8_t bg_parallax_phases_0[16][768];
extern const uint8_t bg_parallax_phases_1[16][768];
extern const uint8_t bg_parallax_phases_2[16][768];
extern const uint8_t bg_parallax_phases_3[16][768];

void update_bg_parallax(uint8_t phase);
void init_bg_parallax(void);

#endif /* BG_PARALLAX_H */
