; PPU seen from the CPU (K2/K3): VRAM write + buffered read-back through VADDR/VDATA, OAM DMA from a RAM
; page, vblank NMI (3 frames). Writes the failing check number or $01 to SYS_TEST.
        .include "konsol.inc"
        .zeropage
nmis:   .res 1
        .code
reset:  sei
        cld
        ldx #$FF
        txs
        lda #0
        sta nmis
        sta PPU_CTRL
; 2: write 16 bytes at $1230, read them back
        lda #$30
        sta PPU_VADDR_L
        lda #$12
        sta PPU_VADDR_H
        ldx #0
@w:     txa
        eor #$A5
        sta PPU_VDATA
        inx
        cpx #16
        bne @w
        lda #$30
        sta PPU_VADDR_L
        lda #$12
        sta PPU_VADDR_H
        ldx #0
@r:     txa
        eor #$A5
        cmp PPU_VDATA
        bne fail2
        inx
        cpx #16
        bne @r
; 3: palette and map regions keep their bytes
        lda #$05
        sta PPU_VADDR_L
        lda #$51
        sta PPU_VADDR_H     ; palette entry 130, high byte
        lda #$0C
        sta PPU_VDATA
        lda #$05
        sta PPU_VADDR_L
        lda PPU_VDATA
        cmp #$0C
        bne fail3
; 4: OAM DMA of page $0300 (byte n = n * 3), check a few OAM bytes
        ldx #0
@f:     txa
        sta $0300,x
        asl a
        clc
        adc $0300,x
        sta $0300,x
        inx
        bne @f
        lda #$03
        sta PPU_OAMDMA
        lda #$00
        sta PPU_VADDR_L
        lda #$52
        sta PPU_VADDR_H
        ldx #0
@c:     lda PPU_VDATA
        cmp $0300,x
        bne fail4
        inx
        bne @c
; 5: three vblank NMIs
        lda #$01
        sta PPU_CTRL
@n:     lda nmis
        cmp #3
        bcc @n
        lda #$00
        sta PPU_CTRL
        lda #$01
        sta SYS_TEST
pass:   jmp pass
fail2:  lda #2
        bne fail
fail3:  lda #3
        bne fail
fail4:  lda #4
fail:   sta SYS_TEST
@h:     jmp @h

nmi:    pha
        lda PPU_STATUS      ; acknowledge (clears vblank, releases NMI)
        inc nmis
        pla
irq:    rti
        .segment "VECTORS"
        .word nmi, reset, irq
