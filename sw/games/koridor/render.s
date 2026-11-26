; Koridor 64: the ray caster. For each of the 40 tile columns one ray is cast through the map (DDA on
; 8.8 fixed point), its perpendicular distance goes to _zbuf, and the column's 22 cells (tile, attr)
; are written into _rowbuf: ceiling fill, the tile where the wall starts, full wall tiles, and the lower
; half as the mirror image (flipped tiles, floor palettes).
;
; Tiles: wall type w (1..4), texture column u (0..3), k = pixels of ceiling above the wall (0..7):
;        WALL_T + (w-1)*32 + u*8 + k; T_FILL is solid colour 1. Palettes: PAL_W + shade*2 (+1 below).
        .export _render_view
        .import _rowbuf, _zbuf, _map, _px, _py, _ang
        .import _sin_lo, _sin_hi, _recip_lo, _recip_hi, _colang_t, _colcos_t, _height_t

WALL_T  = 96
T_FILL  = 224                   ; main.c checks that art.h agrees
PAL_W   = 4
HALF    = 88                    ; half the view height, pixels
FLIPY   = $20

        .zeropage
col:    .res 1
xoff:   .res 1                  ; col * 2
ra:     .res 2                  ; ray angle
ptr:    .res 2
mp:     .res 2                  ; map pointer
fx:     .res 1
fy:     .res 1
sgnx:   .res 1                  ; bit7: ray goes left
sgny:   .res 1                  ; bit7: ray goes up
adx:    .res 2                  ; |dx|, |dy| (0..256)
ady:    .res 2
ddx:    .res 2
ddy:    .res 2
sdx:    .res 2
sdy:    .res 2
dist:   .res 2
side:   .res 1
wall:   .res 1
m16:    .res 2
m8:     .res 1
r0:     .res 1
r1:     .res 1
r2:     .res 1
perp:   .res 2
u:      .res 1
tfull:  .res 1
tpart:  .res 1
kr:     .res 1
palt:   .res 1
palb:   .res 1

        .code
; r2:r1:r0 = m16 * m8 (m8 is destroyed)
mul16x8:
        lda #0
        sta r1
        sta r2
        ldy #8
@l:     lsr m8
        bcc @n
        clc
        lda r1
        adc m16
        sta r1
        lda r2
        adc m16+1
        sta r2
@n:     ror r2
        ror r1
        ror r0
        dey
        bne @l
        rts

_render_view:
        lda #0
        sta col
@column:
        lda col
        asl a
        sta xoff
        ; ---- ray angle = ang + colang[col] (mod 1024)
        ldx col
        ldy #0
        lda _colang_t,x
        bpl :+
        dey
:       clc
        adc _ang
        sta ra
        tya
        adc _ang+1
        and #3
        sta ra+1
        ; ---- dy = sin(ra), dx = sin(ra + 256): signs and magnitudes
        clc
        lda #<_sin_lo
        adc ra
        sta ptr
        lda #>_sin_lo
        adc ra+1
        sta ptr+1
        ldy #0
        lda (ptr),y
        sta ady
        inc ptr+1               ; + 256: cos
        lda (ptr),y
        sta adx
        clc                     ; same offsets in sin_hi
        lda #<_sin_hi
        adc ra
        sta ptr
        lda #>_sin_hi
        adc ra+1
        sta ptr+1
        lda (ptr),y
        sta ady+1
        sta sgny
        inc ptr+1
        lda (ptr),y
        sta adx+1
        sta sgnx
        bit sgnx
        bpl :+
        sec                     ; |dx|
        lda #0
        sbc adx
        sta adx
        lda #0
        sbc adx+1
        sta adx+1
:       bit sgny
        bpl :+
        sec
        lda #0
        sbc ady
        sta ady
        lda #0
        sbc ady+1
        sta ady+1
:
        ; ---- ddx = recip[ra], ddy = recip[(ra + 768) & 1023]
        clc
        lda #<_recip_lo
        adc ra
        sta ptr
        lda #>_recip_lo
        adc ra+1
        sta ptr+1
        lda (ptr),y
        sta ddx
        clc
        lda #<_recip_hi
        adc ra
        sta ptr
        lda #>_recip_hi
        adc ra+1
        sta ptr+1
        lda (ptr),y
        sta ddx+1
        lda ra+1
        clc
        adc #3
        and #3
        tax                     ; X = high byte of (ra + 768) & 1023
        clc
        lda #<_recip_lo
        adc ra
        sta ptr
        txa
        adc #>_recip_lo
        sta ptr+1
        lda (ptr),y
        sta ddy
        clc
        lda #<_recip_hi
        adc ra
        sta ptr
        txa
        adc #>_recip_hi
        sta ptr+1
        lda (ptr),y
        sta ddy+1
        ; ---- map pointer = map + (py >> 8) * 32 + (px >> 8)
        lda _py+1
        lsr a
        lsr a
        lsr a
        sta mp+1
        lda _py+1
        asl a
        asl a
        asl a
        asl a
        asl a
        ora _px+1
        clc
        adc #<_map
        sta mp
        lda mp+1
        adc #>_map
        sta mp+1
        lda _px
        sta fx
        lda _py
        sta fy
        ; ---- sdx = distance along the ray to the first vertical grid line
        lda ddx
        sta m16
        lda ddx+1
        sta m16+1
        bit sgnx
        bpl @xpos
        lda fx
        sta m8
        jsr mul16x8
        jmp @xset
@xpos:  lda fx
        bne :+
        lda ddx
        sta r1
        lda ddx+1
        sta r2
        jmp @xset
:       eor #$FF
        clc
        adc #1
        sta m8
        jsr mul16x8
@xset:  lda r1
        sta sdx
        lda r2
        sta sdx+1
        ; ---- sdy
        lda ddy
        sta m16
        lda ddy+1
        sta m16+1
        bit sgny
        bpl @ypos
        lda fy
        sta m8
        jsr mul16x8
        jmp @yset
@ypos:  lda fy
        bne :+
        lda ddy
        sta r1
        lda ddy+1
        sta r2
        jmp @yset
:       eor #$FF
        clc
        adc #1
        sta m8
        jsr mul16x8
@yset:  lda r1
        sta sdy
        lda r2
        sta sdy+1
        ; ---- DDA: step to the nearer grid line until a wall
@dda:   lda sdx+1
        cmp sdy+1
        bcc @xstep
        bne @ystep
        lda sdx
        cmp sdy
        bcs @ystep
@xstep: lda sdx
        sta dist
        lda sdx+1
        sta dist+1
        clc
        lda sdx
        adc ddx
        sta sdx
        lda sdx+1
        adc ddx+1
        sta sdx+1
        lda #0
        sta side
        bit sgnx
        bpl @xr
        lda mp                  ; left: map - 1
        bne :+
        dec mp+1
:       dec mp
        jmp @hit
@xr:    inc mp
        bne @hit
        inc mp+1
        jmp @hit
@ystep: lda sdy
        sta dist
        lda sdy+1
        sta dist+1
        clc
        lda sdy
        adc ddy
        sta sdy
        lda sdy+1
        adc ddy+1
        sta sdy+1
        lda #1
        sta side
        bit sgny
        bpl @yd
        sec                     ; up: map - 32
        lda mp
        sbc #32
        sta mp
        bcs @hit
        dec mp+1
        jmp @hit
@yd:    clc
        lda mp
        adc #32
        sta mp
        bcc @hit
        inc mp+1
@hit:   ldy #0
        lda (mp),y
        beq @dda
        sta wall
        ; ---- perpendicular distance (fisheye corrected)
        lda dist
        sta m16
        lda dist+1
        sta m16+1
        ldx col
        lda _colcos_t,x
        sta m8
        jsr mul16x8
        lda r1
        sta perp
        lda r2
        sta perp+1
        bne :+
        lda perp
        cmp #16
        bcs :+
        lda #16
        sta perp
:       ldx xoff
        lda perp
        sta _zbuf,x
        lda perp+1
        sta _zbuf+1,x
        ; ---- texture column: where along the wall the ray hit
        lda side
        bne @uy
        lda ady+1               ; vertical wall: fy +- dist * |dy|
        beq :+
        lda dist                ; |dy| = 256
        jmp @ux1
:       lda ady
        sta m8
        jsr mul16x8
        lda r1
@ux1:   bit sgny
        bmi :+
        clc
        adc fy
        jmp @ux2
:       eor #$FF
        sec
        adc fy
@ux2:   bit sgnx
        bmi :+
        eor #$FF
:       sta u
        jmp @tile
@uy:    lda adx+1               ; horizontal wall: fx +- dist * |dx|
        beq :+
        lda dist
        jmp @uy1
:       lda adx
        sta m8
        jsr mul16x8
        lda r1
@uy1:   bit sgnx
        bmi :+
        clc
        adc fx
        jmp @uy2
:       eor #$FF
        sec
        adc fx
@uy2:   bit sgny
        bpl :+
        eor #$FF
:       sta u
        ; ---- tiles: full wall and the partial tile at the wall's top edge
@tile:  lda wall
        sec
        sbc #1
        asl a
        asl a
        asl a
        asl a
        asl a                   ; (w-1) * 32
        sta tfull
        lda u
        lsr a
        lsr a
        lsr a                   ; (u >> 6) * 8 = (u >> 3) & $18
        and #$18
        clc
        adc tfull
        adc #WALL_T
        sta tfull
        ; height: height_t[min(perp >> 3, 1023)] / 2
        lda perp+1
        cmp #$20
        bcc :+
        lda #$1F
        sta perp+1
        lda #$FF
        sta perp
:       lda perp+1
        sta ptr+1
        lda perp
        lsr ptr+1
        ror a
        lsr ptr+1
        ror a
        lsr ptr+1
        ror a
        clc
        adc #<_height_t
        sta ptr
        lda ptr+1
        adc #>_height_t
        sta ptr+1
        ldy #0
        lda (ptr),y
        lsr a                   ; half height
        cmp #HALF
        bcc :+
        lda #HALF
:       eor #$FF                ; top = HALF - half
        sec
        adc #HALF
        tay
        lsr a
        lsr a
        lsr a
        sta kr                  ; row where the wall starts
        tya
        and #7
        clc
        adc tfull
        sta tpart
        ; shade = min(3, perp >> 9 + side)
        lda perp+1
        lsr a
        clc
        adc side
        cmp #4
        bcc :+
        lda #3
:       asl a
        adc #PAL_W
        sta palt
        ora #FLIPY
        clc
        adc #1
        sta palb
        ; ---- write the column's 22 cells
        ldx xoff
        .repeat 11, R
        ldy #T_FILL
        lda #R
        cmp kr
        bcc :+                  ; R < kr: ceiling
        ldy tfull
        cmp kr
        bne :+                  ; R > kr: full wall
        ldy tpart
:       tya
        sta _rowbuf + R * 80, x
        sta _rowbuf + (21 - R) * 80, x
        lda palt
        sta _rowbuf + R * 80 + 1, x
        lda palb
        sta _rowbuf + (21 - R) * 80 + 1, x
        .endrepeat
        inc col
        lda col
        cmp #40
        beq :+
        jmp @column
:       rts
