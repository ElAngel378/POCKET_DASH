// Music player for the SN76489 PSG. The stream (see gg/tools/gg_music.py) holds one chunk of
// register writes per video frame and lives in switchable ROM banks. music_tick() runs from
// the vblank interrupt, so the song keeps its tempo even if a game frame runs long. It swaps
// the 0x4000 ROM window to the music bank and restores whatever bank main() had mapped.

#include <gbdk/platform.h>
#include <stdint.h>

#include "gg_music.h"

#define MUSIC_END        0x7E
#define MUSIC_NEXT_BANK  0x7F

static const uint8_t *mus_ptr;
static uint8_t mus_bank_idx;
static volatile uint8_t mus_playing;

static void psg_silence(void) {
    PSG = PSG_LATCH | PSG_CH0 | PSG_VOLUME | 0x0F;
    PSG = PSG_LATCH | PSG_CH1 | PSG_VOLUME | 0x0F;
    PSG = PSG_LATCH | PSG_CH2 | PSG_VOLUME | 0x0F;
    PSG = PSG_LATCH | PSG_CH3 | PSG_VOLUME | 0x0F;
}

void music_stop(void) {
    mus_playing = 0;
    psg_silence();
}

void music_start(void) {
    mus_playing = 0;
    psg_silence();
#ifdef __TARGET_gg
    GG_SOUND_PAN = 0xFF;
#endif
    mus_bank_idx = 0;
    mus_ptr = gg_music_banks[0];
    mus_playing = 1;
}

void music_tick(void) {
    if (!mus_playing) return;

    uint8_t saved_bank = MAP_FRAME1;
    SWITCH_ROM(GG_MUSIC_FIRST_BANK + mus_bank_idx);

    uint8_t h = *mus_ptr++;
    if (h == MUSIC_NEXT_BANK) {
        mus_bank_idx++;
        SWITCH_ROM(GG_MUSIC_FIRST_BANK + mus_bank_idx);
        mus_ptr = gg_music_banks[mus_bank_idx];
        h = *mus_ptr++;
    }
    if (h == MUSIC_END) {
        mus_playing = 0;
        psg_silence();
    } else {
        // Port 0x06 only exists on the Game Gear; on the SMS skip the stereo byte.
#ifdef __TARGET_gg
        if (h & 0x80) GG_SOUND_PAN = *mus_ptr++;
#else
        if (h & 0x80) mus_ptr++;
#endif
        for (uint8_t n = h & 0x7F; n; n--) PSG = *mus_ptr++;
    }

    SWITCH_ROM(saved_bank);
}
