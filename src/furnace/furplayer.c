#pragma bank 25
// Copied from fur2gb by tools/convert_furnace.py
/* furplayer: song setup, muting and frame pacing (may be banked: see furplayer.h).
   The per-tick interpreter is in furplayer_tick.s, the shared state in furplayer_core.c. */
#include <gb/gb.h>
#include "furplayer.h"

extern uint8_t fur_ctx[];
extern fur_chunk_t fur_chunks[];
extern uint8_t fur_wave_bank;
extern const uint8_t *fur_wave_ptr;
extern uint8_t fur_ac_on;
extern uint16_t fur_ac_step;
extern uint16_t fur_ac_cnt;
extern uint8_t fur_ac_pos;
extern uint16_t fur_ac_freq;
extern uint8_t fur_nr33;
extern uint8_t fur_ac_q;
extern uint16_t fur_ac_r;

static uint32_t fur_acc;       /* fur_frame() time accumulator, 1/16 cycles */

#define FRAME_TIME (70224UL * 16)

void fur_play(const fur_song_t *song, uint8_t bank) FUR_API {
    fur_song_t s;
    uint8_t i;
    fur_playing = 0;
    fur_copy_banked(&s, song, sizeof(s), bank);
    fur_copy_banked(fur_chunks, s.chunks, s.chunk_count * sizeof(fur_chunk_t), bank);
    for (i = 0; i < FUR_STREAMS; i++) {
        uint8_t *c = fur_ctx + i * FUR_CTX_SIZE;
        const fur_chunk_t *ch = &fur_chunks[s.entries[i].chunk];
        const uint8_t *p = ch->ptr + s.entries[i].offset;
        c[0] = 0;
        c[1] = ch->bank;
        c[2] = (uint8_t)(uint16_t)p;
        c[3] = (uint8_t)((uint16_t)p >> 8);
        c[4] = 0;
    }
    fur_ac_on = s.anticlick;
    fur_ac_step = s.ac_step;
    fur_period = s.period;
    if (s.wave_chunk != 0xFF) {
        fur_wave_bank = fur_chunks[s.wave_chunk].bank;
        fur_wave_ptr = fur_chunks[s.wave_chunk].ptr;
    }
    fur_ac_cnt = 0;
    fur_ac_pos = 0;
    fur_ac_freq = 0;
    fur_nr33 = 0;
    fur_ac_q = 0;
    fur_ac_r = 0;
    fur_forget_wave();
    /* the first fur_frame() plays exactly tick 0 */
    fur_acc = (fur_period > FRAME_TIME) ? fur_period - FRAME_TIME : 0;

    /* Furnace's chip starts zeroed: power cycle (clears NR10-NR51) and clear wave RAM */
    NR52_REG = 0x00;
    for (i = 0; i < 16; i++) AUD3WAVE[i] = 0;
    fur_playing = 1;
}

void fur_stop(void) FUR_API {
    fur_playing = 0;
    NR52_REG = 0x00;
}

void fur_set_mute(uint8_t mask) FUR_API {
    uint8_t unmute3 = (fur_mute & 4) && !(mask & 4);
    fur_mute = mask;
    if (unmute3) {
        __critical { fur_reload_wave(); }
    }
}

void fur_frame(void) FUR_API {
    if (!fur_playing) return;
    fur_acc += FRAME_TIME;
    while (fur_acc >= fur_period) {
        fur_acc -= fur_period;
        fur_tick();
    }
}
