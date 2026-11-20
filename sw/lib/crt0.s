; Start-up code of the C games: the boot ROM jumps to $9000 (first byte of STARTUP).
        .export   __STARTUP__ : absolute = 1
        .export   _exit
        .import   _main, zerobss, initlib, donelib
        .import   __RAM_START__, __RAM_SIZE__, __STACKSIZE__
        .include  "zeropage.inc"

        .segment  "STARTUP"
start:  sei
        cld
        ldx #$FF
        txs
        lda #<(__RAM_START__ + __RAM_SIZE__ + __STACKSIZE__)
        sta sp
        lda #>(__RAM_START__ + __RAM_SIZE__ + __STACKSIZE__)
        sta sp+1
        jsr zerobss
        jsr initlib
        jsr _main
_exit:  jsr donelib
        jmp ($FFFC)             ; back to the boot ROM (menu)
