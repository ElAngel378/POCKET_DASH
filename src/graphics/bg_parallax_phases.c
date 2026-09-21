#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "bg_parallax.h"

void init_bg_parallax(void) {
    if (_cpu == CGB_TYPE) {
        update_bg_parallax(0);
    }
}

void update_bg_parallax(uint8_t phase) {
    if (_cpu != CGB_TYPE) return;

    uint8_t bank = 41 + (phase >> 4);
    uint8_t p_in_bank = phase & 15u;
    const uint8_t *src;
    if (bank == 41) src = bg_parallax_phases_0[p_in_bank];
    else if (bank == 42) src = bg_parallax_phases_1[p_in_bank];
    else if (bank == 43) src = bg_parallax_phases_2[p_in_bank];
    else src = bg_parallax_phases_3[p_in_bank];

    uint8_t prev_b = _current_bank;
    SWITCH_ROM(bank);

    VBK_REG = 1;
    // Fast CGB GDMA transfer: 48 blocks of 16 bytes = 768 bytes
    // Destination: VRAM Bank 1, 0x9000 (tiles 0..47 in LCDC signed mode)
    HDMA1_REG = (uint8_t)((uint16_t)src >> 8);
    HDMA2_REG = (uint8_t)((uint16_t)src & 0xF0);
    HDMA3_REG = 0x90;
    HDMA4_REG = 0x00;
    HDMA5_REG = 47; // 48 blocks (768 bytes)
    VBK_REG = 0;

    SWITCH_ROM(prev_b);
}
