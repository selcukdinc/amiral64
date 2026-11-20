; Retro konsol hardware registers (see the plan's register map).

; PPU ($8000)
PPU_VADDR_L  = $8000
PPU_VADDR_H  = $8001
PPU_VDATA    = $8002
PPU_CTRL     = $8003   ; bit0 NMI on vblank, bit1 background, bit2 sprites
PPU_STATUS   = $8004   ; bit7 vblank, bit6 sprite-background hit, bit5 sprite-sprite hit (read clears)
PPU_SCROLL_XL = $8005
PPU_SCROLL_XH = $8006
PPU_SCROLL_Y = $8007
PPU_OAMDMA   = $8008   ; write a RAM page: copies 256 bytes to OAM
PPU_LINE     = $8009
PPU_RASTER   = $800A   ; IRQ when this line starts (bit3 of CTRL enables)

; Input ($8200)
PAD1         = $8200   ; A B Sel Start Up Down Left Right (bit7..bit0), 1 = pressed
PAD2         = $8201
PAD3         = $8202
PAD4         = $8203
KBD_DATA     = $8204   ; next PS/2 scan code byte (read pops)
KBD_STATUS   = $8205   ; bit0 = byte available
SW_LO        = $8208
SW_HI        = $8209
BUTTONS      = $820A   ; bit0 C, 1 U, 2 L, 3 R, 4 D
KEYMAP       = $8210   ; $8210-$822F: 256 key-down bits (scan code set 2, E0 keys at +$80)

; Timer ($8300)
TIMER_FRAMES_L = $8300
TIMER_FRAMES_H = $8301
TIMER_PERIOD_L = $8302
TIMER_PERIOD_H = $8303
TIMER_CTRL     = $8304   ; bit0 run, bit1 IRQ
TIMER_STATUS   = $8305   ; bit0 expired (write 1 to clear)

; Display ($8400)
SEG_DIGITS   = $8400   ; 4 bytes, $8400 = rightmost two digits
SEG_BLANK    = $8404
SEG_DOTS     = $8405
LED_LO       = $8406
LED_HI       = $8407
RGB16        = $8408
RGB17        = $8409

; UART ($8500)
UART_DATA    = $8500
UART_STATUS  = $8501   ; bit0 rx byte available, bit1 tx busy

; System ($8700)
SYS_ID       = $8700   ; "K"
SYS_VERSION  = $8701
SYS_SPEED    = $8702   ; bit0 1 = 1.79 MHz
SYS_TEST     = $87FF   ; simulation: test result ($01 = pass)

; Cartridge port ($8710)
CART_ADDR_L  = $8710
CART_ADDR_M  = $8711
CART_ADDR_H  = $8712
CART_DATA    = $8713   ; read: byte at CART_ADDR, then CART_ADDR + 1
CART_STATUS  = $8714   ; bit0 data ready

; RAM conventions shared by the boot ROM and the games
NMI_VEC      = $0200   ; the boot ROM's NMI/IRQ handlers jump through these (JMP (NMI_VEC))
IRQ_VEC      = $0202
OAM_SHADOW   = $0300   ; 256-byte sprite table copied by OAM DMA
GAME_START   = $9000
