; UART from the CPU (K4): sends "K65", then echoes each received byte + 1 until it gets "!", then reports $01.
        .include "konsol.inc"
        .code
reset:  sei
        cld
        ldx #$FF
        txs
        ldx #0
@s:     lda hello,x
        beq echo
        jsr putc
        inx
        bne @s
echo:   lda UART_STATUS
        and #1
        beq echo
        lda UART_DATA
        cmp #'!'
        beq done
        clc
        adc #1
        jsr putc
        jmp echo
done:   lda #$01
        sta SYS_TEST
@h:     jmp @h
putc:   pha
@w:     lda UART_STATUS
        and #2
        bne @w
        pla
        sta UART_DATA
        rts
nmi:
irq:    rti
        .rodata
hello:  .byte "K65", 0
        .segment "VECTORS"
        .word nmi, reset, irq
