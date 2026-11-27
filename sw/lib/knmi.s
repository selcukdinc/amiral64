; Interrupt handlers of the C game library: NMI = frame count + OAM DMA + raster split set-up,
; IRQ = raster split (new scroll from the split line on).
; After its own work the NMI calls a hook (the sound engine, sw/lib/ses.s) when _k_nmi_hook_on is not zero:
; set _k_nmi_hook first, then _k_nmi_hook_on (one byte, so an NMI never sees half an address).
        .export _frame_count, _split_line, _split_xl, _split_xh, _split_y, _k_install_irq
        .export _k_nmi_hook, _k_nmi_hook_on
        .export _scroll_xl, _scroll_xh, _scroll_y
        .include "konsol.inc"

        .bss
_frame_count: .res 1
_split_line: .res 1
_split_xl:  .res 1
_split_xh:  .res 1
_split_y:   .res 1
_scroll_xl: .res 1
_scroll_xh: .res 1
_scroll_y:  .res 1
_k_nmi_hook: .res 2
_k_nmi_hook_on: .res 1

        .code
_k_install_irq:
        sei
        lda #0
        sta _k_nmi_hook_on
        lda #<nmi
        sta NMI_VEC
        lda #>nmi
        sta NMI_VEC+1
        lda #<irq
        sta IRQ_VEC
        lda #>irq
        sta IRQ_VEC+1
        cli
        rts

nmi:    pha
        lda PPU_STATUS
        lda #>OAM_SHADOW
        sta PPU_OAMDMA
        lda _scroll_xl          ; top of the frame: the main scroll
        sta PPU_SCROLL_XL
        lda _scroll_xh
        sta PPU_SCROLL_XH
        lda _scroll_y
        sta PPU_SCROLL_Y
        lda #$10                ; drop a raster flag left from the previous frame
        sta PPU_STATUS
        lda _split_line
        beq @nosplit
        sta PPU_RASTER
        lda PPU_CTRL
        ora #$08
        sta PPU_CTRL
        bne @done
@nosplit:
        lda PPU_CTRL
        and #$F7
        sta PPU_CTRL
@done:  inc _frame_count
        lda _k_nmi_hook_on
        beq @nohook
        txa
        pha
        tya
        pha
        jsr callhook
        pla
        tay
        pla
        tax
@nohook:
        pla
        rti

callhook:
        jmp (_k_nmi_hook)

irq:    pha
        lda #$10                ; acknowledge the raster IRQ
        sta PPU_STATUS
        lda _split_xl
        sta PPU_SCROLL_XL
        lda _split_xh
        sta PPU_SCROLL_XH
        lda _split_y
        sta PPU_SCROLL_Y
        pla
        rti
