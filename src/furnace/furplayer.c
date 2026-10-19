#pragma bank 25
// Copied from fur2gb by tools/convert_furnace.py
#define FUR_NO_FRAME  // the timer interrupt ticks the player (main.c)
/* furplayer: song setup, muting and frame pacing (may be banked: see furplayer.h).
   The per-tick interpreter is in furplayer_tick.s, the shared state in furplayer_core.c. */
#include <gb/gb.h>
#include "furplayer.h"

/* state in furplayer_tick.s */
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
extern uint16_t fur_ac_x;

#ifndef FUR_NO_FRAME   /* a game that ticks the player itself can leave fur_frame() out */
static uint32_t fur_acc;       /* fur_frame() time accumulator, 1/16 cycles */
#define FRAME_TIME (70224UL * 16)
#endif

void fur_play(const fur_song_t *song, uint8_t bank) FUR_API {
    /* field by field into place: no copy of the header on the stack (a game's stack may be tight) */
    const fur_chunk_t *chunks;
    uint8_t n, wave_chunk, i;
    fur_entry_t e;
    fur_playing = 0;
    fur_copy_banked(&n, &song->chunk_count, 1, bank);
    fur_copy_banked(&wave_chunk, &song->wave_chunk, 1, bank);
    fur_copy_banked(&fur_ac_on, &song->anticlick, 1, bank);
    fur_copy_banked(&fur_ac_step, &song->ac_step, 2, bank);
    fur_copy_banked(&fur_period, &song->period, 4, bank);
    fur_copy_banked(&chunks, &song->chunks, 2, bank);
    fur_copy_banked(fur_chunks, chunks, n * sizeof(fur_chunk_t), bank);
    for (i = 0; i < FUR_STREAMS; i++) {
        uint8_t *c = fur_ctx + i * FUR_CTX_SIZE;
        const fur_chunk_t *ch;
        const uint8_t *p;
        fur_copy_banked(&e, &song->entries[i], sizeof(e), bank);
        ch = &fur_chunks[e.chunk];
        p = ch->ptr + e.offset;
        c[0] = 0;
        c[1] = ch->bank;
        c[2] = (uint8_t)(uint16_t)p;
        c[3] = (uint8_t)((uint16_t)p >> 8);
        c[4] = 0;
    }
    if (wave_chunk != 0xFF) {
        fur_wave_bank = fur_chunks[wave_chunk].bank;
        fur_wave_ptr = fur_chunks[wave_chunk].ptr;
    }
    fur_ac_cnt = 0;
    fur_ac_pos = 0;
    fur_ac_freq = 0;
    fur_nr33 = 0;
    fur_ac_q = 0;
    fur_ac_r = 0;
    fur_ac_x = 0x8000;   /* FUR_AC_MEASURE: the time is not known yet */
    fur_forget_wave();
#ifndef FUR_NO_FRAME
    /* the first fur_frame() plays exactly tick 0 */
    fur_acc = (fur_period > FRAME_TIME) ? fur_period - FRAME_TIME : 0;
#endif

    /* Furnace's chip starts zeroed: power cycle (clears NR10-NR51) and clear wave RAM */
    NR52_REG = 0x00;
    for (i = 0; i < 16; i++) AUD3WAVE[i] = 0;
    fur_playing = 1;
}

void fur_measure_time(uint8_t double_speed) FUR_API {
    if (fur_ac_on) fur_ac_on = FUR_AC_MEASURE | (double_speed ? FUR_AC_DOUBLE_SPEED : 0) | 1;
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

#ifndef FUR_NO_FRAME
void fur_frame(void) FUR_API {
    if (!fur_playing) return;
    fur_acc += FRAME_TIME;
    while (fur_acc >= fur_period) {
        fur_acc -= fur_period;
        fur_tick();
    }
}
#endif
