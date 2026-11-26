; Retro konsol boot ROM: logo and start-up sound, game menu from the cartridge, game loader,
; and a small UART monitor (115200 8N1: D addr / W addr val / G addr / ?).
; When the cartridge's first entry is a launcher (directory flags bit0), it is loaded straight away
; instead of the logo and the menu (hold Select at power-up / reset for the menu). The launcher starts a
; game through the API at $FFF0: JSR $FFF0 with A = directory index (never returns).
        .include "konsol.inc"

        .zeropage
frame:  .res 1          ; incremented by the NMI
ptr:    .res 2
dst:    .res 2
cnt:    .res 2
tmp:    .res 2
col:    .res 1
row:    .res 1
tattr:  .res 1
cursor: .res 1
ngames: .res 1
padold: .res 1
padnow: .res 1
padnew: .res 1
linelen: .res 1
hexv:   .res 2

        .bss
dir:    .res 16 + 8 * 32        ; cartridge header + directory
line:   .res 32                 ; monitor input line

        .code
reset:  sei
        cld
        ldx #$FF
        txs
        lda #0
        sta PPU_CTRL
        sta $8110               ; APU master volume 0 (amplifier off)
        sta SYS_SPEED
        sta frame
        sta linelen
        sta padold
        lda #<boot_nmi
        sta NMI_VEC
        lda #>boot_nmi
        sta NMI_VEC+1
        lda #<boot_irq
        sta IRQ_VEC
        lda #>boot_irq
        sta IRQ_VEC+1
        jsr clear_screen
        jsr load_font
        jsr load_palette
        jsr hide_sprites
        lda #0
        sta PPU_SCROLL_XL
        sta PPU_SCROLL_XH
        sta PPU_SCROLL_Y
        lda PPU_STATUS
        lda #$03                ; NMI + background
        sta PPU_CTRL
        cli
        lda #<msg_hello
        ldx #>msg_hello
        jsr uart_puts
        jsr wait_frame          ; let the pads be read once
        jsr wait_frame
        lda PAD1
        ora PAD2
        and #$20                ; Select held: the boot ROM menu
        bne @menu
        jsr find_dir
        lda dir
        cmp #'K'
        bne @menu
        lda dir+4
        beq @menu
        lda dir+16+29           ; entry 0 flags
        lsr a
        bcc @menu
        lda #0
        sta cursor
        jmp load_game
@menu:  jsr logo
        jmp menu

; directory from the QSPI flash if a cartridge is there, else from the BRAM cartridge
find_dir:
        lda #1
        jsr read_dir
        lda dir
        cmp #'K'
        bne @bram
        lda dir+1
        cmp #'6'
        beq @ok
@bram:  lda #0
        jsr read_dir
@ok:    rts

; API: load and start game A of the directory, from the cartridge the caller was loaded from
api_load_game:
        sei
        pha
        lda #0
        sta PPU_CTRL
        sta $8111               ; sound off
        lda #<boot_nmi
        sta NMI_VEC
        lda #>boot_nmi
        sta NMI_VEC+1
        lda #<boot_irq
        sta IRQ_VEC
        lda #>boot_irq
        sta IRQ_VEC+1
        lda CART_SRC
        and #1
        jsr read_dir
        pla
        sta cursor
        jmp load_game

; ---------------------------------------------------------------- screen
; map entry (col, row) = $4000 + row * 128 + col * 2
set_vaddr:                      ; col, row -> VADDR
        lda col
        asl a
        sta tmp
        lda row
        lsr a                   ; C = row bit 0
        ora #$40
        tax
        lda #0
        ror a                   ; $80 for odd rows
        ora tmp
        sta PPU_VADDR_L
        stx PPU_VADDR_H
        rts

print:                          ; string at A/X (lo/hi) at col, row in tattr
        sta ptr
        stx ptr+1
        jsr set_vaddr
        ldy #0
@l:     lda (ptr),y
        beq @done
        cmp #'a'
        bcc @up
        sbc #$20                ; lower case -> upper case
@up:    sta PPU_VDATA
        lda tattr
        sta PPU_VDATA
        iny
        bne @l
@done:  rts

clear_screen:                   ; whole 64x32 map = space, palette 0
        lda #0
        sta PPU_VADDR_L
        lda #$40
        sta PPU_VADDR_H
        ldx #16                 ; 16 x 256 bytes
        ldy #0
@l:     lda #' '
        sta PPU_VDATA
        lda #0
        sta PPU_VDATA
        iny
        iny
        bne @l
        dex
        bne @l
        rts

load_font:                      ; 1 bpp glyphs -> 4 bpp tiles $20-$5F in colour 1
        lda #<font_data
        sta ptr
        lda #>font_data
        sta ptr+1
        lda #$00                ; tile $20 = VRAM $0400
        sta PPU_VADDR_L
        lda #$04
        sta PPU_VADDR_H
        ldx #64 * 8 / 256 * 2   ; 512 glyph rows = 2 pages
        stx cnt
        ldy #0
@row:   lda (ptr),y
        sta tmp
        ldx #4                  ; 4 bytes per row: pixel pairs
@pair:  lda #0
        asl tmp
        bcc @n1
        ora #$10
@n1:    asl tmp
        bcc @n2
        ora #$01
@n2:    sta PPU_VDATA
        dex
        bne @pair
        iny
        bne @row
        inc ptr+1
        dec cnt
        bne @row
        rts

load_palette:
        lda #0
        sta PPU_VADDR_L
        lda #$50
        sta PPU_VADDR_H
        ldx #0
@l:     lda palette,x
        sta PPU_VDATA
        inx
        cpx #palette_end - palette
        bne @l
        rts

hide_sprites:
        lda #$F0
        ldx #0
@l:     sta OAM_SHADOW,x
        inx
        bne @l
        lda #>OAM_SHADOW
        sta PPU_OAMDMA
        rts

wait_frame:
        lda frame
@w:     cmp frame
        beq @w
        rts

; ---------------------------------------------------------------- sound
note:                           ; A = period low, X = period high: square 0, volume 12, decay
        sta $8100
        stx $8101
        lda #$AC                ; decay on, 50 % duty, volume 12
        sta $8102
        lda #$02                ; decay speed 2
        sta $8103
        rts

; ---------------------------------------------------------------- logo
logo:   lda #15
        sta $8110               ; master volume
        lda #$0F
        sta $8111
        lda #1
        sta tattr
        lda #14
        sta col
        lda #9
        sta row
        lda #<msg_title
        ldx #>msg_title
        jsr print
        lda #0
        sta tattr
        lda #10
        sta col
        lda #12
        sta row
        lda #<msg_sub
        ldx #>msg_sub
        jsr print
        ; jingle: four rising notes, 8 frames each
        ldy #0
@n:     lda jingle,y
        ldx jingle+1,y
        jsr note
        ldx #8
@w:     jsr wait_frame
        jsr monitor_poll
        lda PAD1                ; any button skips the logo
        ora PAD2
        bne @skip
        dex
        bne @w
        iny
        iny
        cpy #8
        bne @n
        ldx #60                 ; ~1 s more, or until a button
@hold:  jsr wait_frame
        jsr monitor_poll
        lda PAD1
        ora PAD2
        bne @skip
        dex
        bne @hold
@skip:  rts

; ---------------------------------------------------------------- menu
menu:   jsr clear_screen
        ; the cartridge in the QSPI flash wins over the one built into the bitstream (BRAM)
        jsr find_dir
        jmp check_dir

read_dir:                       ; A = source (1 flash, 0 BRAM): header + directory into dir
        sta CART_SRC
        lda #0
        sta CART_ADDR_L
        sta CART_ADDR_M
        sta CART_ADDR_H
        ldx #0
@rd:    jsr cart_byte           ; 272 bytes: 256 ...
        sta dir,x
        inx
        bne @rd
@rd2:   jsr cart_byte           ; ... + 16
        sta dir+256,x
        inx
        cpx #16
        bne @rd2
        rts

check_dir:
        lda dir
        cmp #'K'
        bne @bad
        lda dir+1
        cmp #'6'
        bne @bad
        lda dir+4
        beq @bad
        cmp #9
        bcc @good
@bad:   jmp no_cart
@good:  sta ngames
        lda #2
        sta tattr
        lda #14
        sta col
        lda #3
        sta row
        lda #<msg_title
        ldx #>msg_title
        jsr print
        lda #0
        sta tattr
        lda #15
        sta col
        lda #5
        sta row
        lda #<msg_pick
        ldx #>msg_pick
        jsr print
        jsr draw_list
        lda #0
        sta tattr
        lda #6
        sta col
        lda #26
        sta row
        lda #<msg_help
        ldx #>msg_help
        jsr print
        lda #33                 ; where the games come from
        sta col
        lda #28
        sta row
        lda #<msg_bram
        ldx #>msg_bram
        ldy CART_SRC
        beq @src
        lda #<msg_flash
        ldx #>msg_flash
@src:   jsr print
        lda #0
        sta cursor
        jsr draw_cursor
menu_loop:
        jsr wait_frame
        jsr monitor_poll
        jsr kbd_show
        lda PAD1
        ora PAD2
        sta padnow
        eor padold
        and padnow
        sta padnew
        lda padnow
        sta padold
        lda padnew
        and #$08                ; up
        beq @nu
        lda cursor
        beq @nu
        jsr erase_cursor
        dec cursor
        jsr draw_cursor
        jsr click
@nu:    lda padnew
        and #$04                ; down
        beq @nd
        lda cursor
        clc
        adc #1
        cmp ngames
        bcs @nd
        jsr erase_cursor
        inc cursor
        jsr draw_cursor
        jsr click
@nd:    lda padnew
        and #$90                ; A or Start
        beq menu_loop
        jmp load_game

no_cart:
        lda #3
        sta tattr
        lda #10
        sta col
        lda #12
        sta row
        lda #<msg_nocart
        ldx #>msg_nocart
        jsr print
@l:     jsr wait_frame
        jsr monitor_poll
        jmp @l

; keyboard diagnostics: the last four PS/2 bytes on the 7-segment display (newest on the right),
; also sent to the UART as "KB xx"
kbd_show:
@next:  lda KBD_STATUS
        lsr a
        bcc @done
        lda SEG_DIGITS+2
        sta SEG_DIGITS+3
        lda SEG_DIGITS+1
        sta SEG_DIGITS+2
        lda SEG_DIGITS
        sta SEG_DIGITS+1
        lda KBD_DATA
        sta SEG_DIGITS
        pha
        lda #'K'
        jsr uart_putc
        lda #'B'
        jsr uart_putc
        lda #' '
        jsr uart_putc
        pla
        jsr put_hex
        lda #13
        jsr uart_putc
        lda #10
        jsr uart_putc
        jmp @next
@done:  rts

click:  lda #$80
        ldx #$00
        jmp note

entry_ptr:                      ; ptr = dir + 16 + 32 * cursor (A = index)
        asl a
        asl a
        asl a
        asl a
        asl a
        clc
        adc #<(dir + 16)
        sta ptr
        lda #>(dir + 16)
        adc #0
        sta ptr+1
        rts

draw_list:
        ldx #0
@e:     stx tmp+1
        txa
        jsr entry_ptr
        lda #8
        sta col
        txa
        asl a
        clc
        adc #8
        sta row
        lda #0
        sta tattr
        lda ptr
        ldx ptr+1
        jsr print               ; names are NUL padded to 20 bytes
        ; players "1-4P" at column 30
        ldx tmp+1
        txa
        jsr entry_ptr
        lda #30
        sta col
        jsr set_vaddr
        ldy #27                 ; min players
        lda (ptr),y
        ora #'0'
        jsr put_char
        ldy #28
        lda (ptr),y
        dey
        cmp (ptr),y
        beq @one
        lda #'-'
        jsr put_char
        ldy #28
        lda (ptr),y
        ora #'0'
        jsr put_char
@one:   lda #'P'
        jsr put_char
        ldx tmp+1
        inx
        cpx ngames
        bne @e
        rts

put_char:
        sta PPU_VDATA
        lda #0
        sta PPU_VDATA
        rts

cursor_pos:
        lda #6
        sta col
        lda cursor
        asl a
        clc
        adc #8
        sta row
        jmp set_vaddr
draw_cursor:
        jsr cursor_pos
        lda #'>'
        sta PPU_VDATA
        lda #1
        sta PPU_VDATA
        rts
erase_cursor:
        jsr cursor_pos
        lda #' '
        sta PPU_VDATA
        lda #0
        sta PPU_VDATA
        rts

cart_byte:                      ; next cartridge byte in A (waits while the port is busy)
@w:     lda CART_STATUS
        lsr a
        bcc @w
        lda CART_DATA
        rts

; ---------------------------------------------------------------- loader
load_game:
        lda cursor
        jsr entry_ptr
        ldy #20                 ; offset (3 bytes)
        lda (ptr),y
        sta CART_ADDR_L
        iny
        lda (ptr),y
        sta CART_ADDR_M
        iny
        lda (ptr),y
        sta CART_ADDR_H
        iny
        lda (ptr),y             ; size
        sta cnt
        iny
        lda (ptr),y
        sta cnt+1
        iny
        lda (ptr),y             ; load address
        sta dst
        sta tmp
        iny
        lda (ptr),y
        sta dst+1
        sta tmp+1
        lda #<msg_load
        ldx #>msg_load
        jsr uart_puts
        ldy #0
@copy:  lda cnt
        ora cnt+1
        beq @done
        jsr cart_byte
        sta (dst),y
        inc dst
        bne @n
        inc dst+1
@n:     lda cnt
        bne @d
        dec cnt+1
@d:     dec cnt
        jmp @copy
@done:  sei
        lda #0
        sta PPU_CTRL
        sta $8111               ; sound channels off
        jsr hide_sprites
        jsr clear_screen
        ldx #$FF
        txs
        jmp (tmp)

; ---------------------------------------------------------------- UART monitor
uart_putc:
        pha
@w:     lda UART_STATUS
        and #2
        bne @w
        pla
        sta UART_DATA
        rts

uart_puts:                      ; string at A/X
        sta ptr
        stx ptr+1
        ldy #0
@l:     lda (ptr),y
        beq @d
        jsr uart_putc
        iny
        bne @l
@d:     rts

put_hex:                        ; A as two hex digits
        pha
        lsr a
        lsr a
        lsr a
        lsr a
        jsr @dig
        pla
        and #15
@dig:   cmp #10
        bcc @n
        adc #6
@n:     adc #'0'
        jmp uart_putc

monitor_poll:                   ; call once per frame: handles received characters
        txa
        pha
        tya
        pha
@more:  lda UART_STATUS
        lsr a
        bcc @out
        lda UART_DATA
        cmp #13
        beq @exec
        cmp #10
        beq @exec
        ldx linelen
        cpx #31
        bcs @more
        cmp #'a'
        bcc @st
        sbc #$20
@st:    sta line,x
        inc linelen
        jsr uart_putc           ; echo
        jmp @more
@exec:  ldx linelen
        lda #0
        sta line,x
        sta linelen
        lda #13
        jsr uart_putc
        lda #10
        jsr uart_putc
        jsr mon_command
        lda #<msg_prompt
        ldx #>msg_prompt
        jsr uart_puts
@out:   pla
        tay
        pla
        tax
        rts

parse_hex:                      ; line from X: hex number into hexv; X after it; C = 1 if none
        lda #0
        sta hexv
        sta hexv+1
        ldy #0
@sp:    lda line,x
        cmp #' '
        bne @dig
        inx
        bne @sp
@dig:   lda line,x
        sec
        sbc #'0'
        bcc @end
        cmp #10
        bcc @ok
        sbc #7
        cmp #10
        bcc @end
        cmp #16
        bcs @end
@ok:    asl hexv
        rol hexv+1
        asl hexv
        rol hexv+1
        asl hexv
        rol hexv+1
        asl hexv
        rol hexv+1
        ora hexv
        sta hexv
        iny
        inx
        bne @dig
@end:   cpy #1                  ; C = 0 when no digit
        bcc @none
        clc
        rts
@none:  sec
        rts

mon_command:
        lda line
        cmp #'D'
        beq mon_dump
        cmp #'W'
        beq mon_write
        cmp #'G'
        beq mon_go
        cmp #'?'
        bne @n
        lda #<msg_mhelp
        ldx #>msg_mhelp
        jmp uart_puts
@n:     rts

mon_dump:
        ldx #1
        jsr parse_hex
        bcs @r
        lda hexv
        sta dst
        lda hexv+1
        sta dst+1
        jsr put_hex
        lda dst
        jsr put_hex
        lda #':'
        jsr uart_putc
        ldy #0
@b:     lda #' '
        jsr uart_putc
        lda (dst),y
        jsr put_hex
        iny
        cpy #16
        bne @b
        lda #13
        jsr uart_putc
        lda #10
        jsr uart_putc
@r:     rts

mon_write:
        ldx #1
        jsr parse_hex
        bcs @r
        lda hexv
        sta dst
        lda hexv+1
        sta dst+1
        jsr parse_hex
        bcs @r
        lda hexv
        ldy #0
        sta (dst),y
        lda #<msg_ok
        ldx #>msg_ok
        jmp uart_puts
@r:     rts

mon_go:
        ldx #1
        jsr parse_hex
        bcs @r
        jmp (hexv)
@r:     rts

; ---------------------------------------------------------------- interrupts
boot_nmi:
        pha
        lda PPU_STATUS
        inc frame
        pla
boot_irq:
        rti

nmi_entry:
        jmp (NMI_VEC)
irq_entry:
        jmp (IRQ_VEC)

        .rodata
msg_title:  .byte "RETRO KONSOL", 0
msg_sub:    .byte "6502 . NEXYS 4 DDR", 0
msg_pick:   .byte "OYUN SEC", 0
msg_help:   .byte "YUKARI/ASAGI SEC  A/START OYNA", 0
msg_nocart: .byte "KARTUS BULUNAMADI", 0
msg_bram:   .byte "BRAM", 0
msg_flash:  .byte "FLASH", 0
msg_hello:  .byte 13, 10, "RETRO KONSOL BOOT ROM", 13, 10
msg_prompt: .byte "K65> ", 0
msg_load:   .byte 13, 10, "LOADING", 13, 10, 0
msg_ok:     .byte "OK", 13, 10, 0
msg_mhelp:  .byte "D ADDR / W ADDR VAL / G ADDR", 13, 10, 0
jingle:     .word $00F9, $00C5, $00A6, $007C        ; ~784, 988, 1175, 1568 Hz
palette:    ; 16 palettes of 16 colours: [GGGGBBBB, ----RRRR]
            .byte $14, $01,  $FF, $0F               ; palette 0: backdrop dark blue, colour 1 white
            .res 28, 0
            .byte $00, $00,  $C0, $0F               ; palette 1: yellow
            .res 28, 0
            .byte $00, $00,  $FF, $00               ; palette 2: cyan
            .res 28, 0
            .byte $00, $00,  $33, $0F               ; palette 3: red
            .res 28, 0
palette_end:

        .include "font.inc"

        .segment "API"
        jmp api_load_game       ; $FFF0

        .segment "VECTORS"
        .word nmi_entry, reset, irq_entry
