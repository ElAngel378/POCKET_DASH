; Music state of the game in HRAM (WRAM is full: the stack needs every byte left).
; The Furnace player's own state is in furplayer_tick.s (also moved to _HRAM by
; tools/convert_furnace.py). Nothing here is initialized at boot: main() sets it.

	.module music_hram
	.area	_HRAM

_music_driver::			; MUSIC_HUGE / MUSIC_FUR (music.h)
	.ds	1
_fur_tbase::			; Furnace tick: fur_tbase timer counts + fur_tfrac / 16384
	.ds	1
_fur_tfrac::
	.ds	2
_fur_tacc::
	.ds	2
_fur_left::			; counts to the next tick while a sample drives the timer
	.ds	2
_fur_bp::			; beat timeline of the Furnace song (furnace_songs.c)
	.ds	2
_fur_loop_p::
	.ds	2
_fur_next_t::
	.ds	2
_music_vbl_ticks::		; Furnace ticks put off to the VBlank handler's end (main.c)
	.ds	1
