; Retro konsol boot ROM (K1): counts frames on the 7-segment display and mirrors the switches
; on the LEDs through the CPU. Grows into the logo + menu in K6.
        .include "konsol.inc"
        .zeropage
count:  .res 2
        .code
reset:  sei
        cld
        ldx #$FF
        txs
        lda #0
        sta count
        sta count+1
        sta SEG_BLANK
loop:   lda SW_LO
        sta LED_LO
        lda SW_HI
        sta LED_HI
        lda TIMER_FRAMES_L
        sta SEG_DIGITS
        lda TIMER_FRAMES_H
        sta SEG_DIGITS+1
        inc count
        bne @n
        inc count+1
@n:     lda count
        sta SEG_DIGITS+2
        lda count+1
        sta SEG_DIGITS+3
        jmp loop
nmi:
irq:    rti
        .segment "VECTORS"
        .word nmi, reset, irq
