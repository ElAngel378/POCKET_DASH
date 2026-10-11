#include <gb/gb.h>
#include <gb/cgb.h>
#include "assets.h"
#include "gameplay.h"
#include "hUGEDriver.h"
#include "states.h"
#include "sample_player.h"
#include "bg_parallax.h"
#include "music.h"
#include "furplayer.h"

extern const hUGESong_t menuloop;

uint8_t music_ready = 0;
volatile uint16_t music_ticks;   // hUGE_dosound calls since init_music_banked (music_beats.c)
uint8_t redraw = 1;
uint8_t selected = 0;
volatile uint8_t current_song_bank = 0;
volatile uint8_t current_music_divider = 176;
volatile uint8_t cgb_music_tick = 0;
static uint16_t music_time_acc = 0;

// music_driver and the fur_t* pacing state are in HRAM (src/furnace/music_hram.s)

// timer counts until the next Furnace tick: fur_tbase, one more when the fraction carries over
static uint8_t fur_period_counts(void) {
  uint8_t n = fur_tbase;
  fur_tacc += fur_tfrac;
  if (fur_tacc >= 16384u) {
    fur_tacc -= 16384u;
    n++;
  }
  return n;
}

#ifdef DEBUG_PROFILE
GameState current_state = STATE_PLAY_LEVEL;
volatile uint8_t gplevel;
#else
GameState current_state = STATE_MENU;
#endif

volatile uint8_t level_banner_scx = 0;
static volatile uint8_t active_banner_scx = 0;

// One line early, then waits for the line itself (see menu_stat_isr in state_menu.c)
void level_select_stat_isr(void) {
  if (LYC_REG == 30) {
    while (LY_REG < 31u);
    SCX_REG = active_banner_scx;
    LYC_REG = 119;
  } else {
    while (LY_REG < 120u);
    SCX_REG = 0;
    LYC_REG = 255;
  }
}

void level_select_vbl_isr(void) {
  SCX_REG = 0;
  LYC_REG = 30;
  active_banner_scx = level_banner_scx;
}

#define HUGE_ORDER_CNT     (*((volatile uint8_t *)(&hUGE_mute_mask - 0x1D)))
#define HUGE_CURRENT_ORDER (*((volatile uint8_t *)(&hUGE_mute_mask + 6)))

// not inline: it is called from three places and bank 0 is full
static void step_music(void) {
  uint8_t order_before = HUGE_CURRENT_ORDER;
  uint8_t prev_bank = _current_bank;
  uint8_t prev_ie = IE_REG;
  // A music tick takes up to ~3k dots. If VBlank starts meanwhile, the VBlank
  // handler (scroll, palettes, parallax GDMA, map uploads) must be able to preempt
  // it, or its work would land outside VBlank. Only VBlank may nest, and the scanline
  // (STAT) handler when it is on: the DMG menus change SCX at a scanline, and a music
  // tick in the way made that land up to 7 lines late (the sky's top rows jumped).
  // With a scanline handler on, VBlank nests too (the level select has no bg handler).
  uint8_t nest = (bg_vbl_on || (prev_ie & LCD_IFLAG)) ? (uint8_t)(VBL_IFLAG | (prev_ie & LCD_IFLAG)) : 0;
  if (nest) {
    IE_REG = nest;
    enable_interrupts();
  }
  if (music_driver == MUSIC_FUR) {
    fur_tick();   // bank 0, restores the bank
  } else {
    SWITCH_ROM(current_song_bank);
    hUGE_dosound();
    SWITCH_ROM(prev_bank);
  }
  music_ticks++;
  if (nest) {
    disable_interrupts();
    IE_REG = prev_ie;
  }

  if (music_driver == MUSIC_FUR) {
    if (!fur_playing) {   // FFxx: the song ended, silence it like the hUGE level songs below
      music_ready = 0;
      NR12_REG = 0; NR14_REG = 0x80;
      NR22_REG = 0; NR24_REG = 0x80;
      NR30_REG = 0;
      NR42_REG = 0; NR44_REG = 0x80;
    }
  } else if (current_song_bank != 1 && current_song_bank != 212) {
    if (order_before == (uint8_t)(HUGE_ORDER_CNT - 2) && HUGE_CURRENT_ORDER == 0) {
      music_ready = 0;
      NR12_REG = 0; NR14_REG = 0x80;
      NR22_REG = 0; NR24_REG = 0x80;
      NR30_REG = 0;
      NR42_REG = 0; NR44_REG = 0x80;
    }
  }
}

#include "save_manager.h"
#include "settings.h"

// Called by the timer interrupt to update music or stream samples
#ifdef DEBUG_PROFILE
extern volatile uint8_t gpmark;
static void play_music_safe_(void);
void play_music_safe(void) {
  uint8_t prof_prev = gpmark;
  gpmark = 20;   // tools/profile.py: "music (timer)"
  play_music_safe_();
  gpmark = prof_prev;
}
static void play_music_safe_(void) {
#else
void play_music_safe(void) {
#endif
  if (sample_playing) {
    sample_play_isr();
    if (!sample_playing) {
      if (music_ready && setting_music_enabled) {
        music_give_back(4);
        hUGE_reset_wave();
        TMA_REG = current_music_divider;
        TIMA_REG = current_music_divider;
        IF_REG &= ~TIM_IFLAG;
        TAC_REG = 0x04;
        cgb_music_tick = 0;
        music_time_acc = 0;
      }
      sample_keeps_music = 0;
      return;
    }
    if (music_ready && sample_keeps_music && setting_music_enabled && music_driver == MUSIC_FUR) {
      // the timer runs at 256 Hz for the sample: 16 counts of 4096 Hz (32 of 8192 Hz)
      fur_left -= (_cpu == CGB_TYPE) ? 32 : 16;
      while (music_ready && fur_left <= 0) {
        step_music();
        fur_left += fur_period_counts();
      }
    } else if (music_ready && sample_keeps_music && setting_music_enabled) {
      music_time_acc += 16;
      uint16_t period = 256 - current_music_divider;
      while (music_ready && music_time_acc >= period) {
        music_time_acc -= period;
        step_music();
      }
    }
    return;
  }
  if (music_ready && setting_music_enabled) {
    if (music_driver == MUSIC_FUR) {
      TMA_REG = (uint8_t)(0 - fur_period_counts());   // the period after this one
      step_music();
      return;
    }
    if ((_cpu == CGB_TYPE) && (cgb_music_tick++ & 1u)) return;
    step_music();
  }
}

void main(void) {
  music_ready = 0;
  sample_playing = 0;
  music_driver = MUSIC_HUGE;   // HRAM: not initialized at boot
  fur_playing = 0;
  fur_beats_start(FUR_NONE);

  if (_cpu == CGB_TYPE) cpu_fast();

#ifdef DEBUG_PROFILE
  // Profiling build: tools/mesen_profile.lua writes level + 1 to gplevel; then straight in
  while (gplevel == 0) wait_vbl_done();
  selected = (uint8_t)(gplevel - 1u);
#endif

  init_save_system();

  // Enable sound hardware
  NR52_REG = 0x80;
  NR51_REG = 0xFF;
  NR50_REG = 0x77;

  TAC_REG = 0x04;
  add_TIM(play_music_safe);
  set_interrupts(VBL_IFLAG | TIM_IFLAG);

  if (setting_music_enabled) {
    music_play_menu();
    music_ready = 1;
  }
  enable_interrupts();

  while (1) {
    switch (current_state) {
      case STATE_MENU:
        current_state = update_menu_state();
        break;
      case STATE_LEVEL_SELECT:
        current_state = update_level_select_state();
        break;
      case STATE_NEW_MENU_SELECT:
        current_state = update_new_menu_select_state();
        break;
      case STATE_PLAY_LEVEL:
        current_state = update_play_level_state();
        break;
      case STATE_MUSIC_TEST:
        current_state = update_music_test_state();
        break;
      case STATE_SETTINGS:
        current_state = update_settings_state();
        break;
      case STATE_LEVEL_COMPLETE:
        current_state = update_level_complete_state();
        break;
      case STATE_ICON_SELECT:
        current_state = update_icon_select_state();
        break;
    }
  }
}
