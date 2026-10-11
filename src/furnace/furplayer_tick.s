; furplayer: per-tick stream interpreter (see tools/fur2gb.py for the bytecode).
;
; void fur_tick(void)
;   1. steps Furnace's anti-click counter (top of DivPlatformGB::tick):
;        cnt += step; pos += cnt / freq; cnt %= freq     (when freq > 0)
;      as cnt += step % freq; pos += step / freq (+1 if cnt >= freq, then cnt -= freq), with
;      step / freq and step % freq computed only when the ch3 period or the step changes
;   2. runs the 5 streams in Furnace's write order (A = row commands, B0-B3 = tick() per channel):
;      a stream with idle ticks pending counts one down, otherwise it runs ops up to an END op
; Writes to channels set in fur_mute (bit n = channel n+1) are skipped; the streams run on.
; Restores the caller's ROM bank. Clobbers all registers.
;
; void fur_reload_wave(void): rewrites the last wave loaded (after something else used wave RAM).

	.module furplayer_tick

	.globl	__current_bank

	rROMB0 = 0x2000
	STREAMS = 5
	FRAMES = 1		; call depth: FUR_STACK_DEPTH in furplayer.h, MAX_DEPTH in fur2gb.py
	CTX_SIZE = 5 + 5 * FRAMES	; idle, bank, ptr lo, ptr hi, depth, frames of 5 bytes
	CTX_DEPTH = 4
	CHUNKS = 4		; FUR_MAX_CHUNKS

	; all of the player's state (STREAMS * CTX_SIZE + CHUNKS * 3 + 28 bytes); a game short of
	; WRAM can put this area in _HRAM
	.area	_HRAM
_fur_ctx::			; per stream: idle, bank, ptr lo, ptr hi, depth, frames
	.ds	STREAMS * CTX_SIZE
_fur_chunks::			; bank, ptr lo, ptr hi
	.ds	CHUNKS * 3
_fur_playing::			; 0 once the song stopped
	.ds	1
_fur_mute::			; muted channels
	.ds	1
_fur_wave_bank::		; the base waves
	.ds	1
_fur_wave_ptr::
	.ds	2
_fur_ac_on::			; anti-click: on, step per tick, period count, position (0-31),
	.ds	1
_fur_ac_step::			; ch3 period (Furnace's chan[2].freq, 0 until set), NR33,
	.ds	2
_fur_ac_cnt::			; step / freq (low byte) and step % freq
	.ds	2
_fur_ac_pos::
	.ds	1
_fur_ac_freq::
	.ds	2
_fur_nr33::
	.ds	1
_fur_ac_q::
	.ds	1
_fur_ac_r::
	.ds	2
_fur_period::			; tick period in 1/16 cycles
	.ds	4
fur_cur:			; context of the stream being run
	.ds	2
fur_bank:			; bank of the stream being run
	.ds	1
fur_tmp:
	.ds	2
fur_wave_last:			; id + 1 of the last wave loaded (0 = none)
	.ds	2

	.area	_CODE

_fur_tick::
	ld	a, (_fur_playing)
	or	a
	ret	z

	; ---- 1. anti-click step
	ld	a, (_fur_ac_on)
	or	a
	jr	z, ac_done
	ld	a, (_fur_ac_freq)
	ld	e, a
	ld	a, (_fur_ac_freq + 1)
	ld	d, a
	or	e
	jr	z, ac_done
	ld	hl, #_fur_ac_cnt
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, (_fur_ac_r)
	ld	c, a
	ld	a, (_fur_ac_r + 1)
	ld	b, a
	add	hl, bc			; cnt + step % freq (< 2 * freq)
	ld	a, (_fur_ac_q)
	ld	b, a
	ld	a, l
	sub	e
	ld	c, a
	ld	a, h
	sbc	a, d
	jr	c, 1$
	ld	h, a			; cnt >= freq: one more period
	ld	l, c
	inc	b
1$:	ld	a, l
	ld	(_fur_ac_cnt), a
	ld	a, h
	ld	(_fur_ac_cnt + 1), a
	ld	a, (_fur_ac_pos)
	add	a, b
	and	#31
	ld	(_fur_ac_pos), a
ac_done:

	; ---- 2. the streams, in Furnace's write order
	ldh	a, (__current_bank)
	push	af
	.irp	k, 0, 1, 2, 3, 4
	ld	hl, #_fur_ctx + k * CTX_SIZE
	ld	a, (hl)			; idle ticks
	or	a
	jr	z, .+5
	dec	(hl)
	jr	.+5
	call	run_stream
	.endm
tick_done:
	pop	af
	ldh	(__current_bank), a
	ld	(rROMB0), a
	ret

; run the stream whose context is at HL up to its END op
run_stream:
	ld	a, l
	ld	(fur_cur), a
	ld	a, h
	ld	(fur_cur + 1), a
	inc	hl
	ld	a, (hl+)
	ld	(fur_bank), a
	ldh	(__current_bank), a
	ld	(rROMB0), a
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
resume:					; back from an op handler: the loop for the mute state
	ld	a, (_fur_mute)
	or	a
	jr	nz, fnext

next:
	ld	a, (hl+)
	cp	#0x30
	jr	nc, not_write
	add	a, #0x10		; register write $FF10+op
	ld	c, a
	ld	a, (hl+)
	ldh	(c), a
	jr	next

fnext:					; the same, skipping writes to muted channels
	ld	a, (hl+)
	cp	#0x30
	jr	nc, not_write
	ld	c, a
	call	chan_bit
	ld	a, (_fur_mute)
	and	b
	ld	a, (hl+)
	jr	nz, fnext
	ld	b, a
	ld	a, c
	add	a, #0x10
	ld	c, a
	ld	a, b
	ldh	(c), a
	jr	fnext

not_write:
	jp	z, wr_nr33
	cp	#0x32
	jp	c, wr_nr34
	cp	#0x80
	jp	nc, special

	; ---- END: (op & 3F) more empty ticks; the innermost call frame has one unit less
	and	#0x3f
	ld	b, a
	ld	a, (fur_cur)
	ld	e, a
	ld	a, (fur_cur + 1)
	ld	d, a			; DE = context
	ld	a, b
	ld	(de), a			; idle
	inc	de
	inc	de
	inc	de
	inc	de			; DE = depth
	ld	a, (de)
	or	a
	jr	z, end_save
	push	de
	call	top_units		; DE = top frame's units lo
	ld	a, (de)
	sub	#1
	ld	(de), a
	inc	de
	ld	a, (de)
	sbc	a, #0
	ld	(de), a
	pop	de
pop_frames:				; return from frames with no units left (DE = depth)
	ld	a, (de)
	or	a
	jr	z, end_save
	push	de
	call	top_units
	ld	a, (de)
	inc	de
	ld	b, a
	ld	a, (de)
	or	b
	jr	nz, 2$
	dec	de
	dec	de
	ld	a, (de)			; return address
	ld	h, a
	dec	de
	ld	a, (de)
	ld	l, a
	dec	de
	ld	a, (de)			; return bank
	ld	(fur_bank), a
	pop	de
	ld	a, (de)
	dec	a
	ld	(de), a
	jr	pop_frames
2$:	pop	de
end_save:				; DE = depth: store bank and position before it
	dec	de
	ld	a, h
	ld	(de), a
	dec	de
	ld	a, l
	ld	(de), a
	dec	de
	ld	a, (fur_bank)
	ld	(de), a
	ret

wr_nr33:				; 30 vv: NR33, remembered for the ch3 period
	ld	a, (_fur_mute)
	bit	2, a
	ld	a, (hl+)
	ld	(_fur_nr33), a
	jp	nz, resume
	ldh	(0x1d), a
	jp	resume
wr_nr34:				; 31 vv qq rl rh: NR34; freq = 2048 - ((NR34 & 7) << 8 | NR33)
	ld	a, (_fur_mute)
	bit	2, a
	ld	a, (hl+)
	jr	nz, 1$
	ldh	(0x1e), a
1$:	and	#7
	ld	d, a
	ld	a, (_fur_nr33)
	ld	e, a
	xor	a
	sub	e			; low byte, carry = borrow
	ld	e, a
	ld	a, #8
	sbc	a, d
	ld	d, a			; DE = new freq
	ld	a, (hl+)		; step / freq, step % freq (from the converter)
	ld	(_fur_ac_q), a
	ld	a, (hl+)
	ld	(_fur_ac_r), a
	ld	a, (hl+)
	ld	(_fur_ac_r + 1), a
	ld	a, e
	ld	(_fur_ac_freq), a
	ld	a, d
	ld	(_fur_ac_freq + 1), a
	; cnt may exceed a lower freq: pos += cnt / freq, cnt %= freq (pos is only read after the
	; next tick's step, where Furnace's single division gives the same total)
	ld	a, (_fur_ac_cnt)
	sub	e
	ld	a, (_fur_ac_cnt + 1)
	sbc	a, d
	jp	c, resume		; cnt < freq
	push	hl
	ld	hl, #_fur_ac_cnt
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	call	div16
	ld	a, c
	ld	(_fur_ac_cnt), a
	ld	a, b
	ld	(_fur_ac_cnt + 1), a
	ld	a, (_fur_ac_pos)
	add	a, l
	and	#31
	ld	(_fur_ac_pos), a
	pop	hl
	jp	resume

; C = write op (register - $10) -> B = its channel's fur_mute bit (0 for NR50-NR52)
chan_bit:
	ld	b, #1
	ld	a, c
	cp	#0x05
	ret	c
	ld	b, #2
	cp	#0x0a
	ret	c
	ld	b, #4
	cp	#0x0f
	ret	c
	ld	b, #8
	cp	#0x14
	ret	c
	ld	b, #4			; wave RAM
	cp	#0x20
	ret	nc
	ld	b, #0
	ret

; DE = depth of a context with depth > 0 -> DE = its top frame's units lo
top_units:
	ld	a, (de)
	dec	a
	ld	c, a
	add	a, a
	add	a, a
	add	a, c
	add	a, #4			; depth + 1 + 5 * (depth - 1) + 3
	add	a, e
	ld	e, a
	ret	nc
	inc	d
	ret

special:
	cp	#0x82
	jp	c, op_wave
	jr	z, op_acreset
	cp	#0x84
	jr	c, op_stop
	jr	z, op_call8
	cp	#0x86
	jr	c, op_call16
	jp	z, op_jump
	cp	#0x88
	jp	z, op_zombie
	; 0x87 SETRATE kl kh p0 p1 p2
	ld	a, (hl+)
	ld	(_fur_ac_step), a
	ld	a, (hl+)
	ld	(_fur_ac_step + 1), a
	ld	a, (hl+)
	ld	(_fur_period), a
	ld	a, (hl+)
	ld	(_fur_period + 1), a
	ld	a, (hl+)
	ld	(_fur_period + 2), a
	xor	a
	ld	(_fur_period + 3), a
	ld	a, (_fur_ac_freq)
	ld	e, a
	ld	a, (_fur_ac_freq + 1)
	ld	d, a
	or	e
	jp	z, resume
	push	hl
	call	ac_divstep
	pop	hl
	jp	resume

op_zombie:				; 88 rr xx nn: $FF00+rr = xx, then nn x (09, 11, 08)
	ld	a, (hl+)
	ld	c, a
	sub	#0x10
	push	bc
	ld	c, a
	call	chan_bit
	ld	a, (_fur_mute)
	and	b
	pop	bc
	jr	z, 2$
	inc	hl			; muted
	inc	hl
	jp	resume
2$:	ld	a, (hl+)
	ldh	(c), a
	ld	a, (hl+)
	ld	b, a
1$:	ld	a, #0x09
	ldh	(c), a
	ld	a, #0x11
	ldh	(c), a
	ld	a, #0x08
	ldh	(c), a
	dec	b
	jr	nz, 1$
	jp	resume

op_acreset:
	xor	a
	ld	(_fur_ac_pos), a
	ld	(_fur_ac_cnt), a
	ld	(_fur_ac_cnt + 1), a
	jp	resume

op_stop:				; song over: the remaining streams stop too
	xor	a
	ld	(_fur_playing), a
	pop	hl			; return address into the stream loop
	jp	tick_done

op_call16:
	ld	b, #1
	jr	call_common
op_call8:
	ld	b, #0
call_common:				; push frame {bank, return, units}, then jump
	ld	a, (hl+)		; chunk
	ld	c, a
	ld	a, (hl+)		; offset
	ld	e, a
	ld	a, (hl+)
	ld	d, a
	push	de
	push	bc
	ld	a, (fur_cur)
	add	a, #CTX_DEPTH
	ld	e, a
	ld	a, (fur_cur + 1)
	adc	a, #0
	ld	d, a			; DE = depth
	pop	bc
	ld	a, (hl+)		; units
	ld	(fur_tmp), a
	xor	a
	bit	0, b
	jr	z, 1$
	ld	a, (hl+)
1$:	ld	(fur_tmp + 1), a
	push	bc			; C = target chunk
	ld	a, (de)
	or	a
	jr	z, 2$
	push	de			; the caller's frame runs these units through the callee
	call	top_units
	ld	a, (fur_tmp)
	ld	b, a
	ld	a, (de)
	sub	b
	ld	(de), a
	inc	de
	ld	a, (fur_tmp + 1)
	ld	b, a
	ld	a, (de)
	sbc	a, b
	ld	(de), a
	pop	de
2$:	ld	a, (de)			; push the new frame
	inc	a
	ld	(de), a
	call	top_units
	ld	a, (fur_tmp)
	ld	(de), a
	inc	de
	ld	a, (fur_tmp + 1)
	ld	(de), a
	dec	de
	dec	de
	ld	a, h
	ld	(de), a
	dec	de
	ld	a, l
	ld	(de), a
	dec	de
	ld	a, (fur_bank)
	ld	(de), a
	pop	bc
	pop	de
	jr	goto_chunk

op_jump:
	ld	a, (hl+)
	ld	c, a
	ld	a, (hl+)
	ld	e, a
	ld	a, (hl+)
	ld	d, a
goto_chunk:				; C = chunk, DE = offset in chunk
	ld	a, c
	add	a, a
	add	a, c
	add	a, #<_fur_chunks
	ld	l, a
	ld	a, #>_fur_chunks
	adc	a, #0
	ld	h, a
	ld	a, (hl+)
	ld	(fur_bank), a
	ldh	(__current_bank), a
	ld	(rROMB0), a
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	add	hl, de
	jp	resume

op_wave:				; NR30 = 0, then the base wave rotated by the anti-click position
	and	#1
	ld	b, a
	ld	a, (hl+)
	ld	c, a
	inc	bc
	ld	a, c
	ld	(fur_wave_last), a
	ld	a, b
	ld	(fur_wave_last + 1), a
	ld	a, (_fur_mute)
	bit	2, a
	jp	nz, resume
	push	hl
	call	wave_copy
	ld	a, (fur_bank)
	ldh	(__current_bank), a
	ld	(rROMB0), a
	pop	hl
	jp	resume

; BC = wave id + 1: NR30 = 0, wave RAM = that base wave rotated by the anti-click position.
; Leaves the wave pool's bank selected.
wave_copy:
	dec	bc
	xor	a
	ldh	(0x1a), a
	ld	h, b
	ld	l, c
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, (_fur_wave_ptr)
	ld	e, a
	ld	a, (_fur_wave_ptr + 1)
	ld	d, a
	add	hl, de			; even variant
	ld	a, (_fur_ac_pos)
	srl	a			; start byte, carry = odd rotation
	ld	(fur_tmp), a
	jr	nc, 1$
	ld	de, #16			; odd variant
	add	hl, de
1$:	ld	d, h
	ld	e, l			; DE = variant start
	add	a, l
	ld	l, a
	ld	a, h
	adc	a, #0
	ld	h, a			; HL = variant + start
	ld	a, (_fur_wave_bank)
	ldh	(__current_bank), a
	ld	(rROMB0), a
	ld	a, (fur_tmp)
	cpl
	add	a, #17
	ld	b, a			; 16 - start
	ld	c, #0x30
2$:	ld	a, (hl+)
	ldh	(c), a
	inc	c
	dec	b
	jr	nz, 2$
	ld	a, (fur_tmp)
	or	a
	ret	z
	ld	b, a
	ld	h, d
	ld	l, e
3$:	ld	a, (hl+)
	ldh	(c), a
	inc	c
	dec	b
	jr	nz, 3$
	ret

_fur_reload_wave::
	ld	a, (fur_wave_last)
	ld	c, a
	ld	a, (fur_wave_last + 1)
	ld	b, a
	or	c
	ret	z
	ldh	a, (__current_bank)
	push	af
	call	wave_copy
	pop	af
	ldh	(__current_bank), a
	ld	(rROMB0), a
	ret

_fur_forget_wave::
	xor	a
	ld	(fur_wave_last), a
	ld	(fur_wave_last + 1), a
	ret

; step / freq and step % freq for the per-tick anti-click update (DE = freq)
ac_divstep:
	ld	a, (_fur_ac_step)
	ld	l, a
	ld	a, (_fur_ac_step + 1)
	ld	h, a
	call	div16
	ld	a, l
	ld	(_fur_ac_q), a
	ld	a, c
	ld	(_fur_ac_r), a
	ld	a, b
	ld	(_fur_ac_r + 1), a
	ret

; HL / DE (DE = 1..2047) -> L = quotient low byte, BC = remainder (rare: a lower ch3 period,
; a tick rate change)
div16:
	ld	bc, #0
	ld	a, #16
1$:	push	af
	add	hl, hl
	rl	c
	rl	b
	ld	a, c
	sub	e
	ld	a, b
	sbc	a, d
	jr	c, 2$
	ld	b, a
	ld	a, c
	sub	e
	ld	c, a
	inc	l
2$:	pop	af
	dec	a
	jr	nz, 1$
	ret
