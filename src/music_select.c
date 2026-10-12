#pragma bank 25
// Music driver selection: hUGEDriver or Furnace songs (include/music.h)

#include <gb/gb.h>
#include "music.h"
#include "furplayer.h"
#include "hUGEDriver.h"
#include "music_beats.h"
#include "settings.h"
#include "collision.h"
#include "sample_player.h"

extern const hUGESong_t menuloop;
extern uint8_t music_ready;
extern volatile uint8_t current_song_bank;
extern volatile uint8_t current_music_divider;
extern volatile uint16_t music_ticks;
extern volatile uint8_t fur_half, cgb_music_tick;

static const uint8_t level_fur[] = FUR_LEVEL_SONGS;
uint8_t fur_bgroup;   // beat timeline group of the Furnace song playing (furnace_songs.c)

void music_play_fur(uint8_t id) BANKED {
    disable_interrupts();
    music_ready = 0;
    sample_playing = 0;
    sample_keeps_music = 0;
    TAC_REG = 0x00;
    music_driver = MUSIC_FUR;
    // with music off the song still runs silently (the music test plays it), like hUGE's mutes
    fur_mute = setting_music_enabled ? 0 : 0x0F;
    fur_play(fur_songs[id].song, fur_songs[id].bank);
    music_ticks = 0;
    music_beats_start(0);   // no hUGE timeline
    fur_beats_start(id);

    // timer: one interrupt per Furnace tick, TMA set for each period (main.c)
    // 1/16 cycles per timer count: 16384, or 8192 in double speed (shifts: SDCC's 32-bit
    // division would land in the full bank 0)
    fur_half = 0;
    cgb_music_tick = 0;
    if (_cpu == CGB_TYPE && (fur_period >> 13) < 256) {
        fur_tbase = (uint8_t)(fur_period >> 13);
        fur_tfrac = ((uint16_t)fur_period & 8191u) << 1;   // in 1/16384 counts
    } else {
        // DMG, or a CGB song slower than 32 Hz (every other interrupt ticks, main.c)
        if (_cpu == CGB_TYPE) fur_half = 1;
        fur_tbase = (uint8_t)(fur_period >> 14);
        fur_tfrac = (uint16_t)fur_period & 16383u;
    }
    fur_tacc = 0;
    fur_left = fur_tbase;
    current_music_divider = (uint8_t)(0 - fur_tbase);   // for code that restarts the timer
    TMA_REG = current_music_divider;
    TIMA_REG = current_music_divider;
    IF_REG &= ~TIM_IFLAG;
    TAC_REG = 0x04;
    enable_interrupts();
}

void music_play_menu(void) BANKED {
    if (!setting_legacy_music && FUR_MENULOOP != FUR_NONE) {
        music_play_fur(FUR_MENULOOP);
    } else {
        music_driver = MUSIC_HUGE;
        init_music_banked(&menuloop, 1, 176);
        current_song_bank = 1;
    }
}

uint8_t music_play_level_fur(uint8_t level_idx) BANKED {
    if (level_idx >= sizeof(level_fur) || level_fur[level_idx] == FUR_NONE) return 0;
    if (setting_legacy_music) return 0;
    music_play_fur(level_fur[level_idx]);
    return 1;
}

void music_reload_wave(void) BANKED {
    if (music_driver == MUSIC_FUR) fur_reload_wave();
    else hUGE_reset_wave();
}
