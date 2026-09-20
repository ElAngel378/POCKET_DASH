#pragma bank 11

#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/incbin.h>
#include "famidash_sprites.h"

INCBIN(famidash_sprites_tiles, "levels/chr_data/famidash/famidash_sprites_dmg_tiles.bin")
INCBIN_EXTERN(famidash_sprites_tiles)

INCBIN(famidash_deco_tiles, "levels/chr_data/famidash/famidash_deco_cgb_tiles.bin")
INCBIN_EXTERN(famidash_deco_tiles)

void load_famidash_sprite_tiles(void) BANKED {
    set_sprite_data(FAMIDASH_SPRITE_TILE_BASE, FAMIDASH_SPRITE_TILE_COUNT, famidash_sprites_tiles);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_sprite_data(FAMIDASH_SPRITE_TILE_BASE, FAMIDASH_DECO_TILE_COUNT, famidash_deco_tiles);
        VBK_REG = 0;
    }
}
