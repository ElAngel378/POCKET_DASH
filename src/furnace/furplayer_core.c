/* furplayer: the bank-0 C part (see furplayer.h) */
#include <gb/gb.h>
#include <string.h>
#include "furplayer.h"

/* The player's state is in furplayer_tick.s (one area, so a game can place it in HRAM). */

void fur_copy_banked(void *dst, const void *src, uint16_t len, uint8_t bank) NONBANKED {
    uint8_t save = CURRENT_BANK;
    SWITCH_ROM(bank);
    memcpy(dst, src, len);
    SWITCH_ROM(save);
}
