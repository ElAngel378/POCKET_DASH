#ifndef BG_PARALLAX_H
#define BG_PARALLAX_H

#include <gb/gb.h>
#include <stdint.h>

#define BG_PARALLAX_TILE_BASE 0  // In VRAM Bank 1 (tiles 0..47)
#define BG_PARALLAX_NUM_TILES 48

BANKREF_EXTERN(bg_parallax_data_0)
BANKREF_EXTERN(bg_parallax_data_1)
BANKREF_EXTERN(bg_parallax_data_2)
BANKREF_EXTERN(bg_parallax_data_3)

extern const uint8_t bg_parallax_phases_0[16][768];
extern const uint8_t bg_parallax_phases_1[16][768];
extern const uint8_t bg_parallax_phases_2[16][768];
extern const uint8_t bg_parallax_phases_3[16][768];

// Parallax is uploaded by a VBlank interrupt handler so that the 768-byte GDMA
// starts at the very beginning of VBlank regardless of what the main thread or
// the music interrupt is doing. Call bg_parallax_isr_start() when gameplay
// starts, request_bg_parallax() once per frame *before* wait_vbl_done(), and
// bg_parallax_isr_stop() when gameplay ends.
extern volatile uint8_t bg_gdma_pending;
extern volatile uint8_t bg_gdma_phase;
extern volatile uint8_t bg_gdma_isr_on;
extern volatile uint8_t bg_vbl_on;
extern volatile uint8_t bg_vbl_seen;
extern volatile uint8_t bg_vbl_frames;   // incremented by the VBlank handler
extern volatile uint8_t bg_pal_request; // apply famidash_bg_palettes in the VBlank handler (CGB palette RAM is VBlank-only)

// VRAM uploads done by the VBlank handler right after the parallax GDMA (fast,
// no STAT polling). The main thread fills the request and sets *_pending; the
// handler clears it once the data is written (it skips it if too little VBlank
// is left, and the request stays pending for the next one).
extern volatile uint8_t bg_cj_pending;     // 8 tile rows x 2 columns of a streamed map column
extern uint8_t bg_cj_x, bg_cj_y;
extern const uint8_t *bg_cj_tiles;         // 16 bytes: (x, x+1) for each of 8 rows
extern const uint8_t *bg_cj_attrs;
// Row job: one map row (two tile rows at tile row bg_rj_y), BG_RJ_SLOTS ring positions from bg_rj_x
#define BG_RJ_SLOTS 8
extern volatile uint8_t bg_rj_pending;
extern uint8_t bg_rj_x, bg_rj_y;
extern uint8_t bg_rj_tiles[2 * 2 * BG_RJ_SLOTS];   // top tile row, then bottom tile row
extern uint8_t bg_rj_attrs[2 * 2 * BG_RJ_SLOTS];
// Fading cells (mt_renderer.c fade_update): metatile rows (bit r: tile rows 2r, 2r + 1) of the
// map column at tile x bg_fj_x, from mj_out (row r: its 4 tiles at 4r, attributes at 64 + 4r).
// The handler writes as many as fit and clears their bits; 0: nothing to write.
extern volatile uint16_t bg_fj_rows;
extern uint8_t bg_fj_x;
// Saw animation: GDMA of bg_saw_blocks 16-byte tiles from bg_saw_src (in ROM bank bg_saw_bank)
// to VRAM bank 1 at offset bg_saw_dst from 0x8000, after the parallax GDMA (retried when late).
extern volatile uint8_t bg_saw_pending;
extern uint8_t bg_saw_bank, bg_saw_blocks;
extern const uint8_t *bg_saw_src;
extern uint16_t bg_saw_dst;
// DMG version: bg_saw_dmg_n tiles from bg_saw_src (ROM bank bg_saw_bank, 32 bytes apart: normal +
// mirrored) to bg_saw_dmg_dsts[i] (VRAM addresses, 0 = skip)
#define BG_SAW_DMG_MAX 6
extern volatile uint8_t bg_saw_dmg_n;
extern uint8_t * const *bg_saw_dmg_dsts;
// Custom cube icon frame: 64 bytes (4 sprite tiles) from bg_cube_src (ROM bank bg_cube_bank, 16 byte
// aligned) to bg_cube_dst (VRAM bank 0 sprite tiles): set bg_cube_pending to 4 (tiles), done when
// it is back to 0 (DMG: the VBlank handler advances src / dst / pending as tiles are copied)
extern volatile uint8_t bg_cube_pending;
extern uint8_t bg_cube_bank;
extern const uint8_t *bg_cube_src;
extern uint8_t *bg_cube_dst;
void cube_tiles_load_now(const uint8_t *src, uint8_t bank, uint8_t first_tile);
extern volatile uint8_t bg_scroll_pending;
// CGB seamless mirror portal: LCDCF_BG9800 / LCDCF_BG9C00 to switch to with the next scroll
// latch (0xFF: none). The VBlank handler streams to the map LCDC shows.
extern volatile uint8_t bg_lcdc_map;
extern volatile uint8_t bg_scx;
extern volatile uint8_t bg_scy;
// DMG: BGP / OBP0 / OBP1 set by the VBlank handler too
extern volatile uint8_t bg_dmg_pal_pending;
extern uint8_t bg_dmg_bgp, bg_dmg_obp0, bg_dmg_obp1;
#define request_bg_dmg_pals(b, o0, o1) do { bg_dmg_bgp = (b); bg_dmg_obp0 = (o0); bg_dmg_obp1 = (o1); bg_dmg_pal_pending = 1; } while (0)
// Ask the VBlank handler to set SCX/SCY at the start of the next VBlank.
#define request_bg_scroll(x, y) do { bg_scx = (x); bg_scy = (y); bg_scroll_pending = 1; } while (0)
#define request_bg_parallax(p) do { bg_gdma_phase = (p); bg_gdma_pending = 1; } while (0)
void bg_parallax_vbl_isr(void);
// count tiles of bg_extra_tiles (bg_extra_tiles.c) from src (every other 16 bytes: it holds each
// tile normal + mirrored) to BG tile slots dst.. (display off: plain writes)
BANKREF_EXTERN(bg_extra_tiles)
void bg_extra_copy(uint8_t dst, uint8_t count, const uint8_t *src);
void init_bg_parallax(void);
void set_bg_parallax_phase(uint8_t phase);
// Register/unregister the VBlank handler (CGB only). Not in bank 0 to save space there.
#define bg_parallax_isr_start() do {     bg_gdma_pending = 0; bg_fj_rows = 0; bg_scroll_pending = 0; bg_lcdc_map = 0xFF; bg_dmg_pal_pending = 0; bg_vbl_seen = 0;     add_VBL(bg_parallax_vbl_isr); bg_vbl_on = 1;     bg_gdma_isr_on = (_cpu == CGB_TYPE); } while (0)
#define bg_parallax_isr_stop() do {     if (bg_vbl_on) { bg_vbl_on = 0; bg_gdma_isr_on = 0; remove_VBL(bg_parallax_vbl_isr); bg_gdma_pending = 0; } } while (0)

// Waits for the next VBlank. GBDK's wait_vbl_done() can sleep through a whole
// extra frame when a timer interrupt returns right at the start of VBlank (the
// VBlank interrupt lands between its flag check and its HALT), so poll a flag
// set by the VBlank handler instead. Only valid between isr_start/isr_stop.
#define bg_wait_vbl() do { bg_vbl_seen = 0; while (!bg_vbl_seen) { } } while (0)

#endif /* BG_PARALLAX_H */
