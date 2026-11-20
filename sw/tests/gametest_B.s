; Cartridge test game B: shows its name, reports $B2 on SYS_TEST, then waits; Select + Start goes back
; to the boot ROM menu.
        .include "konsol.inc"
        .code
start:  lda #$B2
        sta SYS_TEST
        lda #$82               ; row 3, col 1: $4000 + 3 * 128 + 2
        sta PPU_VADDR_L
        lda #$41
        sta PPU_VADDR_H
        ldx #0
@p:     lda text,x
        beq @wait
        sta PPU_VDATA
        lda #1
        sta PPU_VDATA
        inx
        bne @p
@wait:  lda #$02
        sta PPU_CTRL
@l:     lda PAD1
        and #$30
        cmp #$30
        bne @l
        jmp ($FFFC)
text:   .byte "TEST OYUN B CALISIYOR", 0
