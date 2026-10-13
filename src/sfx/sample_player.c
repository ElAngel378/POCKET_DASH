#include <gb/gb.h>
#include "music.h"
#include <gb/hardware.h>
#include "sample_player.h"
#include "hUGEDriver.h"

volatile uint8_t sample_playing = 0;
volatile uint8_t sample_keeps_music = 0;
static uint8_t play_bank = 0;
static const uint8_t *play_sample_ptr = 0;
static uint16_t play_length = 0;

void sample_play_isr(void) __nonbanked __naked {
    __asm
        push af
        push bc
        push de
        push hl

        ld hl, #_play_length    ; something left to play?
        ld a, (hl+)
        or (hl)
        jr nz, 1$
        ; Already finished
        xor a
        ld (#_sample_playing), a
        ldh (_NR30_REG), a       ; Disable CH3
        ld a, (#_sample_keeps_music)
        or a
        jr nz, 2$
        xor a
        ldh (_TAC_REG), a        ; Stop timer only if not keeping music
2$:
        ld a, #0xFF
        ldh (_NR51_REG), a       ; Restore stereo panning for music
        pop hl
        pop de
        pop bc
        pop af
        ret

1$:
        ld hl, #_play_sample_ptr
        ld a, (hl+)
        ld h, (hl)
        ld l, a                 ; HL = current position inside the sample

        ; Save current bank and switch to sample bank
        ld a, (#__current_bank)
        ld e, a
        ld a, (#_play_bank)
        ld (#__current_bank), a
        ld (_rROMB0), a

        ; Disconnect CH3 to prevent audio clicks during wave RAM update
        ldh a, (_NR51_REG)
        ld c, a
        and #0b10111011
        ldh (_NR51_REG), a

        ; Turn off CH3 to access wave RAM
        xor a
        ldh (_NR30_REG), a       

        .irp ofs,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
            ld a, (hl+)
            ldh (__AUD3WAVERAM+ofs), a
        .endm

        ; Turn CH3 back on
        ld a, #0x80             ; Enable CH3 DAC
        ldh (_NR30_REG), a
        ld a, #0xFE             ; Length of wave (2 ticks)
        ldh (_NR31_REG), a
        ld a, #0x20             ; Volume (100%)
        ldh (_NR32_REG), a
        xor a                   ; Low freq bits = 0
        ldh (_NR33_REG), a
        ld a, #0xC7             ; Trigger playback with freq high bits = 7 (freq 0x700 = 8192 Hz), length enabled
        ldh (_NR34_REG), a       

        ; Reconnect CH3
        ld a, c
        ldh (_NR51_REG), a

        ; Restore bank
        ld a, e
        ld (#__current_bank), a
        ld (_rROMB0), a

        ; Save updated sample pointer
        ld a, l
        ld (#_play_sample_ptr), a
        ld a, h
        ld (#_play_sample_ptr+1), a

        ; Decrement play_length
        ld hl, #_play_length
        ld a, (hl)
        sub #1
        ld (hl+), a
        ld a, (hl)
        sbc #0
        ld (hl), a

        pop hl
        pop de
        pop bc
        pop af
        ret
    __endasm;
}

#include "settings.h"

void play_sample(uint8_t bank, const uint8_t *sample, uint16_t length) {
    if (!setting_sfx_enabled) return;
    disable_interrupts();
    sample_keeps_music = 0;
    TAC_REG = 0x00;

    // Cut off all music channels immediately so no held notes continue droning
    NR12_REG = 0x00; NR14_REG = 0x80;
    NR22_REG = 0x00; NR24_REG = 0x80;
    NR30_REG = 0x00;
    NR42_REG = 0x00; NR44_REG = 0x80;

    NR52_REG = 0x80;
    NR50_REG = 0x77;
    // Route ONLY Channel 3 in stereo pan (left & right), isolating sample playback
    NR51_REG = 0x44;

    play_bank = bank;
    play_sample_ptr = sample;
    play_length = length >> 4; // 16 bytes per interrupt block
    sample_playing = 1;

    if (_cpu == CGB_TYPE) {
        TMA_REG = 0x80; // In CGB double-speed mode: 32768 / 128 = 256 Hz
    } else {
        TMA_REG = 0xC0; // In DMG single-speed mode: 16384 / 64 = 256 Hz
    }
    TIMA_REG = TMA_REG;
    IF_REG &= ~TIM_IFLAG;
    TAC_REG = 0x07; // Clock 16384 Hz (or 32768 in CGB fast), Timer Enabled
    enable_interrupts();
}

void play_sample_with_music(uint8_t bank, const uint8_t *sample, uint16_t length) {
    if (!setting_sfx_enabled) return;
    disable_interrupts();
    sample_keeps_music = 1;
    TAC_REG = 0x00;

    // Reset ONLY Channel 3 (wave channel) for sample playback (Pulse 1, 2 and Noise keep playing!)
    NR30_REG = 0x00;

    NR52_REG = 0x80;
    NR50_REG = 0x77;
    // Keep all channels routed in stereo
    NR51_REG = 0xFF;

    // Mute Channel 3 in hUGEDriver so it doesn't overwrite wave RAM
    music_borrow(4);

    play_bank = bank;
    play_sample_ptr = sample;
    play_length = length >> 4; // 16 bytes per interrupt block
    sample_playing = 1;

    if (_cpu == CGB_TYPE) {
        TMA_REG = 0x80; // In CGB double-speed mode: 32768 / 128 = 256 Hz
    } else {
        TMA_REG = 0xC0; // In DMG single-speed mode: 16384 / 64 = 256 Hz
    }
    TIMA_REG = TMA_REG;
    IF_REG &= ~TIM_IFLAG;
    TAC_REG = 0x07; // Clock 16384 Hz (or 32768 in CGB fast), Timer Enabled
    enable_interrupts();
}

uint8_t is_sample_playing(void) {
    return sample_playing;
}

extern uint8_t music_ready;
extern volatile uint8_t current_music_divider;

void stop_sample(void) {
    disable_interrupts();
    sample_playing = 0;
    sample_keeps_music = 0;
    play_length = 0;
    NR30_REG = 0x00;
    NR51_REG = 0xFF;
    hUGE_reset_wave();
    if (music_ready) {
        music_give_back(4);
        TMA_REG = current_music_divider;
        TIMA_REG = current_music_divider;
        IF_REG &= ~TIM_IFLAG;
        TAC_REG = 0x04;
    } else {
        TAC_REG = 0x00;
    }
    enable_interrupts();
}

