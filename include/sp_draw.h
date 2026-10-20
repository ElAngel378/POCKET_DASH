#ifndef SP_DRAW_H
#define SP_DRAW_H

#include <gb/gb.h>
#include <stdint.h>
#include "player.h"
#include "assets.h"

// OAM sprite index written to by draw_sprites; read back by gameplay.c
// for the "hide old sprites" cleanup step.
uint8_t draw_sprites(SpCache *cache, uint16_t cam_px, uint16_t cam_py,
                     uint8_t reversed, uint8_t oam_start) BANKED;

// CGB: OAM slots reserved before the player for the front half of the portal it is in, which
// draw_sprites fills (or hides) and clears (see sp_draw.c). 0: none.
#define PORTAL_FRONT_SLOTS 6
extern uint8_t sp_front_slot;
extern uint8_t sp_front_want;   // reserve them next frame (a portal is near the player)

void process_sprite_logic(SpCache *cache, uint16_t cam_px,
                          Player *p, uint8_t joy, uint8_t *target_bg_idx) BANKED;

// Famidash coins: collected this attempt / already in the save for this level (bit per coin)
extern uint8_t coins_collected;
extern uint8_t coins_saved;
void coins_reset(void) BANKED;

void setup_menu_font(void) BANKED;
void draw_text(uint8_t x, uint8_t y, const char *str) BANKED;

void sp_cache_reset(SpCache *cache, uint16_t *stream_idx) BANKED;
void sp_cache_retire(uint16_t cam_px) BANKED;
void sp_cache_fill(const Level *l, uint16_t cam_px, uint16_t *stream_idx) BANKED;

#endif
