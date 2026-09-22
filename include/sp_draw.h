#ifndef SP_DRAW_H
#define SP_DRAW_H

#include <gb/gb.h>
#include <stdint.h>
#include "player.h"
#include "assets.h"

// OAM sprite index written to by draw_sprites; read back by gameplay.c
// for the "hide old sprites" cleanup step.
uint8_t draw_sprites(SpCache *cache, uint16_t cam_px, uint16_t cam_py,
                     uint8_t reversed, uint8_t oam_start);

void process_sprite_logic(SpCache *cache, uint16_t cam_px,
                          Player *p, uint8_t joy, uint8_t *target_bg_idx);

void setup_menu_font(void) BANKED;
void draw_text(uint8_t x, uint8_t y, const char *str) BANKED;

void sp_cache_reset(SpCache *cache, uint16_t *stream_idx);
void sp_cache_update(const Level *l, uint16_t cam_px,
                     SpCache *cache, uint16_t *stream_idx);

#endif
