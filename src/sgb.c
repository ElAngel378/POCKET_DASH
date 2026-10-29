#pragma bank 228

#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/sgb.h>
#include <string.h>
#include "sgb.h"
#include "famidash_bg.h"

BANKREF(sgb)

uint8_t sgb_present;
uint8_t sgb_pending;
uint8_t sgb_sky_id;
uint8_t sgb_in_level;
uint8_t sgb_dark_sky;
uint8_t sgb_gap;   // frames before the next packet may go out: the SNES needs a few to take one

// a packet every this many frames at most (a closer one can be lost)
#define SGB_PACKET_GAP 6

// GBDK's sgb_transfer ends every packet with a 70 ms wait (4 frames of nothing): fine for the boot,
// not for a level. This sends the 16 bytes only (about 0.2 frames); sgb_gap keeps the packets apart.
static void send_packet(const uint8_t *pkt) {
  uint8_t i, b, bit;
  P1_REG = 0x00; P1_REG = 0x30;   // reset
  for (i = 0; i < 16; i++) {
    b = pkt[i];
    for (bit = 0; bit < 8; bit++) {
      P1_REG = (b & 1u) ? 0x10 : 0x20;
      P1_REG = 0x30;
      b >>= 1;
    }
  }
  P1_REG = 0x20; P1_REG = 0x30;   // stop bit
  sgb_gap = SGB_PACKET_GAP;
}

// level loads and menu entries (no frame loop, maybe no VBlank: the LCD is off): wait the gap out
static void wait_gap(void) {
  if (sgb_gap) { delay((uint16_t)sgb_gap * 17u); sgb_gap = 0; }
}

void sgb_init(void) BANKED {
  sgb_present = 0;
  if (_cpu == CGB_TYPE) return;   // a GBC / GBA is no SGB (and has its own colors)
  sgb_present = sgb_check();
  if (!sgb_present) return;
  sgb_leave_level();   // the menus' colours
}

void sgb_set_palette(uint16_t c0, uint16_t c1, uint16_t c2, uint16_t c3) BANKED {
  uint8_t pkt[16];
  if (!sgb_present) return;
  memset(pkt, 0, sizeof pkt);
  pkt[0] = (SGB_PAL_01 << 3) | 1;   // PAL01, 1 packet
  // color 0 is shared by all four SGB palettes; palette 0 = colors 1..3, palette 1 the same
  pkt[1] = (uint8_t)c0;  pkt[2] = (uint8_t)(c0 >> 8);
  pkt[3] = (uint8_t)c1;  pkt[4] = (uint8_t)(c1 >> 8);
  pkt[5] = (uint8_t)c2;  pkt[6] = (uint8_t)(c2 >> 8);
  pkt[7] = (uint8_t)c3;  pkt[8] = (uint8_t)(c3 >> 8);
  pkt[9] = pkt[3];  pkt[10] = pkt[4];
  pkt[11] = pkt[5]; pkt[12] = pkt[6];
  pkt[13] = pkt[7]; pkt[14] = pkt[8];
  send_packet(pkt);
}

// The level's colours, CGB's palette 0 on the DMG theme bg_pals[1] (0x39: tile colour 3 -> shade 0,
// 0 -> 1, 1 -> 2, 2 -> 3): white (block outlines), sky, darker sky, black
static void send_sky(void) {
  palette_color_t sky[2];
  famidash_sky_colors(sgb_sky_id, sky);
  sgb_set_palette(RGB(31, 31, 31), sky[0], sky[1], RGB(0, 0, 0));
  sgb_pending = 0;
  // every channel below a third: sky, darker sky and black look alike, only white stands out
  sgb_dark_sky = ((sky[0] & 31u) < 10u && ((sky[0] >> 5) & 31u) < 10u && ((sky[0] >> 10) & 31u) < 10u);
}

void sgb_set_sky(uint8_t color_id) BANKED {
  if (!sgb_present) return;
  sgb_sky_id = color_id;
  sgb_pending = 1;
  sgb_in_level = 1;
}

void sgb_flush(void) BANKED {
  if (!sgb_present || !sgb_pending) return;
  wait_gap();
  send_sky();
}

void sgb_update(void) BANKED {
  if (sgb_gap) { sgb_gap--; return; }
  if (sgb_pending) send_sky();
}

void sgb_leave_level(void) BANKED {
  sgb_in_level = 0;
  sgb_dark_sky = 0;
  sgb_pending = 0;
  wait_gap();
  sgb_set_palette(RGB8(255, 255, 255), RGB8(160, 190, 255), RGB8(70, 100, 190), RGB8(15, 15, 50));
}
