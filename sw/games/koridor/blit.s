; Koridor 64: copies the finished view (22 rows x 40 cells x [tile, attr]) from _rowbuf to the tile map.
; Called right after vblank starts: the copy (about 12 cycles a byte) stays ahead of the beam, so a frame
; never shows half of the old view and half of the new one.
        .export _blit_view
        .import _rowbuf
        .importzp ptr1

VADDR_L = $8000
VADDR_H = $8001
VDATA   = $8002
VIEW_Y  = 2                     ; first tile row of the view
ROWS    = 22

        .code
_blit_view:
        lda #<_rowbuf
        sta ptr1
        lda #>_rowbuf
        sta ptr1+1
        ldx #0                  ; row
@row:   txa                     ; VADDR = $4000 + (VIEW_Y + row) * 128
        clc
        adc #VIEW_Y
        lsr a
        pha
        lda #0
        ror a
        sta VADDR_L
        pla
        ora #$40
        sta VADDR_H
        ldy #0
@cell:  lda (ptr1),y
        sta VDATA
        iny
        lda (ptr1),y
        sta VDATA
        iny
        lda (ptr1),y
        sta VDATA
        iny
        lda (ptr1),y
        sta VDATA
        iny
        cpy #80
        bne @cell
        lda ptr1                ; next row of the buffer
        clc
        adc #80
        sta ptr1
        bcc :+
        inc ptr1+1
:       inx
        cpx #ROWS
        bne @row
        rts
