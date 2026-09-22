#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include <gb/gb.h>
#include "assets.h"
#include "player.h"
#include "famidash_bg.h"
#include "sp_draw.h"

extern uint8_t music_ready;
extern uint8_t redraw;
extern uint8_t selected;
extern volatile uint8_t current_song_bank;
extern volatile uint8_t cgb_music_tick;

// End-animation state shared between gameplay.c and sp_draw.c
#define END_ANIM_INACTIVE 0
#define END_ANIM_PULL     1
#define END_ANIM_SHAKE    2

extern uint8_t  end_anim_state;
extern uint8_t  end_trigger_requested;
extern uint16_t end_trigger_obj_x;
extern uint16_t end_trigger_obj_y;

#define FONT_PUSAB_START 0xD0

void draw_text(uint8_t x, uint8_t y, const char *str) BANKED;
void setup_menu_font(void) BANKED;
void play_level(uint8_t idx) BANKED;

// SP stream loading is bank-safe and only reads new entries as the camera advances.
void sp_cache_reset(SpCache *cache, uint16_t *stream_idx);
void sp_cache_load(uint8_t sp_bank, const SpDef *sp_list, uint16_t cam_px,
                   SpCache *cache, uint16_t *stream_idx, uint16_t map_h);
void sp_cache_update(const Level *l, uint16_t cam_px,
                     SpCache *cache, uint16_t *stream_idx);

#endif
