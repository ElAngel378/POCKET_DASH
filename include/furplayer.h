/* Copied from fur2gb by tools/convert_furnace.py */
/*
 * furplayer - replays Furnace Game Boy songs compiled by tools/fur2gb.py.
 *
 * A song is the exact register-write stream Furnace's own GB backend produces, tick by tick.
 * The player only adds the backend's anti-click wave rotation, which it computes like Furnace
 * (or from the real time, see fur_measure_time()).
 *
 * Usage:
 *     fur_play(&my_song, BANK(my_song));
 *     then once per Furnace tick:   fur_tick();
 *     or once per VBlank:           fur_frame();   (runs 0-2 ticks to keep the song's tick rate)
 *
 * Files: furplayer_tick.s and furplayer_core.c must be in bank 0 (they switch banks);
 * furplayer.c may be banked (define FUR_API as BANKED then).
 * fur_tick() restores the caller's ROM bank and may be called from an interrupt handler.
 */
#ifndef FURPLAYER_H
#define FURPLAYER_H

#include <gbdk/platform.h>
#include <stdint.h>

#define FUR_API BANKED  /* furplayer.c is banked here */

#define FUR_MAX_CHUNKS 4    /* 16 KB each: CHUNKS in furplayer_tick.s, MAX_CHUNKS in fur2gb.py */
#define FUR_STREAMS 5       /* A (row commands), B0-B3 (per-channel tick): NSTREAMS in fur2gb.py */
#define FUR_STACK_DEPTH 1   /* FRAMES in furplayer_tick.s, MAX_DEPTH in fur2gb.py */
#define FUR_CTX_SIZE (5 + FUR_STACK_DEPTH * 5)

typedef struct {
    uint8_t bank;
    const uint8_t *ptr;
} fur_chunk_t;

typedef struct {
    uint8_t chunk;
    uint16_t offset;
} fur_entry_t;

typedef struct {
    uint8_t chunk_count;
    uint8_t wave_chunk;      /* chunk holding the base waves, 0xFF if none */
    uint8_t anticlick;       /* Furnace's anti-click wave rotation enabled */
    uint16_t ac_step;        /* anti-click period step per tick: trunc(2097152 / tick Hz) */
    uint32_t period;         /* tick period in 1/16 CPU cycles (4194304 Hz clock) */
    fur_entry_t entries[FUR_STREAMS];
    const fur_chunk_t *chunks;
} fur_song_t;

/* Start a song (bank = BANK() of the song header). Powers the APU off and on so it starts
   from the same state as Furnace's emulated chip. Keeps fur_mute. */
void fur_play(const fur_song_t *song, uint8_t bank) FUR_API;
/* After fur_play(): base the anti-click wave rotation on the real time (DIV) instead of the
   song's tick count. Furnace writes each tick at its exact time; a game's ticks come a little
   early or late (interrupt latency, a busy frame), and in a song that changes the wave every
   tick (wavetable synths) every such write clicks. Ticks must stay within 7.8 ms (3.9 ms in
   double speed) of the song's tick rate; DIV must not be reset while a song plays. A wave write
   then happens right before its NR34 trigger. No effect on songs without anti-click. Not for
   checking the writes against Furnace (tests/verify_rom.py). */
#define FUR_AC_MEASURE      0x80
#define FUR_AC_DOUBLE_SPEED 0x40
void fur_measure_time(uint8_t double_speed) FUR_API;
/* Stop playback and silence the APU. */
void fur_stop(void) FUR_API;
/* Call once per frame (VBlank, 70224 cycles): plays as many ticks as the song's tick rate needs. */
void fur_frame(void) FUR_API;
/* Mute channels (bit n = channel n+1): their register writes are skipped while the song goes on,
   so sound effects can use them. Unmuting channel 3 reloads the song's current wave. */
void fur_set_mute(uint8_t mask) FUR_API;

/* Bank 0 */
void fur_tick(void) NONBANKED;
void fur_reload_wave(void) NONBANKED;
void fur_forget_wave(void) NONBANKED;
void fur_copy_banked(void *dst, const void *src, uint16_t len, uint8_t bank) NONBANKED;

extern uint8_t fur_playing;  /* 0 once the song stopped (songs without a loop) */
extern uint8_t fur_mute;     /* muted channels, see fur_set_mute() */
extern uint32_t fur_period;  /* current tick period in 1/16 CPU cycles */

#endif
