/* furplayer: the bank-0 C part (see furplayer.h) */
#include <gb/gb.h>
#include <string.h>
#include "furplayer.h"

/* State shared with furplayer_tick.s (keep names and sizes in sync) */
uint8_t fur_playing;
uint8_t fur_mute;
/* fur_ctx (per stream: idle ticks, bank, ptr lo, ptr hi, call depth, then frames of
   {bank, ptr lo, ptr hi, units lo, units hi}) and fur_chunks are in furplayer_tick.s */
uint8_t fur_wave_bank;
const uint8_t *fur_wave_ptr;
uint8_t fur_ac_on;
uint16_t fur_ac_step;
uint16_t fur_ac_cnt;
uint8_t fur_ac_pos;
uint16_t fur_ac_freq;          /* ch3 period as Furnace's chan[2].freq, 0 until first set */
uint8_t fur_nr33;
uint8_t fur_ac_q;              /* ac_step / freq (low byte), ac_step % freq */
uint16_t fur_ac_r;
uint32_t fur_period;           /* tick period, 1/16 cycles */

void fur_copy_banked(void *dst, const void *src, uint16_t len, uint8_t bank) NONBANKED {
    uint8_t save = CURRENT_BANK;
    SWITCH_ROM(bank);
    memcpy(dst, src, len);
    SWITCH_ROM(save);
}
