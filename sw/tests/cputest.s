; CPU and bus self-test (K1). Runs from the boot ROM; writes the number of the failing check to SYS_TEST,
; or $01 when everything passed. Each check is small and independent.
        .include "konsol.inc"
        .zeropage
zp0:    .res 2
ptr:    .res 2
irqs:   .res 1
        .code
reset:  sei
        cld
        ldx #$FF
        txs
        lda #0
        sta irqs

; 2: zero page and absolute RAM
        lda #$5A
        sta zp0
        lda #$C3
        sta $1234
        lda zp0
        cmp #$5A
        bne fail2
        lda $1234
        cmp #$C3
        bne fail2
; 3: stack and subroutine
        lda #$77
        pha
        jsr sub_inc
        pla
        cmp #$77
        bne fail3
        cpx #$01
        bne fail3
; 4: binary and decimal arithmetic
        clc
        lda #$7F
        adc #$01
        bvc fail4          ; overflow expected
        cmp #$80
        bne fail4
        sed
        clc
        lda #$19
        adc #$28           ; 19 + 28 = 47 (BCD)
        cld
        cmp #$47
        bne fail4
        sec
        lda #$10
        sbc #$11
        bcs fail4          ; borrow expected
        cmp #$FF
        bne fail4
        jmp t5
fail2:  lda #2
        jmp fail
fail3:  lda #3
        jmp fail
fail4:  lda #4
        jmp fail
; 5: indexed and indirect addressing across the RAM / game RAM boundary
t5:     ldx #0
@fill:  txa
        sta $9000,x        ; game RAM
        inx
        bne @fill
        lda #$00
        sta ptr
        lda #$90
        sta ptr+1
        ldy #$80
        lda (ptr),y
        cmp #$80
        bne fail5
        ldx #$FF
        lda $8F01,x        ; $9000 via page crossing
        cmp #$00
        bne fail5
        lda $DFFF          ; last game RAM byte is writable
        lda #$A5
        sta $DFFF
        lda $DFFF
        cmp #$A5
        bne fail5
; 6: I/O registers
        lda SYS_ID
        cmp #'K'
        bne fail6
        lda #$3C
        sta LED_LO
        lda LED_LO
        cmp #$3C
        bne fail6
; 7: timer interrupt (10 us period, wait for 3 IRQs)
        lda #10
        sta TIMER_PERIOD_L
        lda #0
        sta TIMER_PERIOD_H
        lda #3
        sta TIMER_CTRL
        cli
        ldx #0
        ldy #0
@wait:  lda irqs
        cmp #3
        bcs @done
        dex
        bne @wait
        dey
        bne @wait
        jmp fail7
@done:  sei
        lda #0
        sta TIMER_CTRL
; 8: ROM data
        lda message
        cmp #'O'
        bne fail8
        lda #$01
        sta SYS_TEST
pass:   jmp pass

fail5:  lda #5
        jmp fail
fail6:  lda #6
        jmp fail
fail7:  lda #7
        jmp fail
fail8:  lda #8
fail:   sta SYS_TEST
@hang:  jmp @hang

sub_inc:
        ldx #0
        inx
        lda #$12           ; clobber A; the caller's value is on the stack
        rts

irq:    pha
        lda #1
        sta TIMER_STATUS
        inc irqs
        pla
nmi:    rti

        .rodata
message: .byte "OK", 0

        .segment "VECTORS"
        .word nmi, reset, irq
