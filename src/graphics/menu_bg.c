#pragma bank 21
#include <gb/gb.h>
#include <gbdk/incbin.h>

BANKREF(menu_bg)

INCBIN(menu_bg_tiles, "levels/chr_data/bg_contrasted_tiles.bin")
INCBIN(menu_bg_map, "levels/chr_data/bg_contrasted_map.bin")
INCBIN(menu_ground_tiles, "levels/chr_data/menu_ground_tiles.bin")
INCBIN(menu_ground_map, "levels/chr_data/menu_ground_map.bin")
INCBIN_EXTERN(menu_ground_tiles)

void load_menu_ground_tiles(void) BANKED {
    if (_cpu != CGB_TYPE) return;
    VBK_REG = 1;
    set_bkg_data(48, 9, menu_ground_tiles);
    VBK_REG = 0;
}
