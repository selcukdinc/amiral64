; Ses motoru: the konsol's sound engine (music and sound effects), run once a frame from the NMI.
;
; Music: a song has a stream for each of the five channels (square 1, square 2, triangle, noise and, on an
; APU v2, the wave channel). Streams are written in MML (tools/sesc.py compiles them) and use instruments:
; per-frame sequences for volume, arpeggio, pitch and duty (wave on the wave channel), with a loop point
; and a release part that a rest jumps to. Per channel: volume, vibrato (with a short delay), portamento,
; detune; per song: tempo (rows a frame x 256), which a game can raise (ses_hizlan).
; Sound effects borrow a channel (one stream per channel they use) and give it back to the music when
; they end; an effect only replaces one of the same or lower priority.
;
; The game talks to the engine through mailboxes (single-byte writes), so it never races the NMI.
;
; Data formats (made by tools/sesc.py):
;   song:  tempo, flags (bits 1-0: the APU v2 output low-pass), instrument table (word), wave table (word),
;          5 stream pointers (word, 0 = none)
;   sfx:   instrument table (word), wave table (word), n, then n x (channel, priority, stream (word))
;   instrument: 4 words: volume, arpeggio, pitch, duty sequences (0 = none)
;   sequence: length, loop (255 = none), release (255 = none), values...
;   stream: $00-$5F note (0 = C2), $60 rest (release), $61 hold, $62 i instrument, $63 v volume,
;           $64 dv vibrato (depth << 4 | speed), $65 s portamento speed, $66 d detune, $67 t tempo,
;           $68 w call, $69 return, $6A w jump, $6B n loop start, $6C loop end, $6D end,
;           $80 | d: rows per note from now on (1..127)
;
; APU v2 (version byte $A2 at $8112) adds the wave channel ($8114..$8117, 32 4-bit samples at $8120);
; on an older APU the wave streams are followed but not heard.

        .export _ses_muzik, _ses_dur, _ses_efekt, _ses_hizlan, _ses_sessiz, _ses_ses, _ses_sondur
        .export _ses_caliyor, _ses_surum, _ses_tick, _ses_perde
        .export _ses_on, _ses_playing           ; (for the tests and tools/sesdinle.py)
        .import _k_nmi_hook, _k_nmi_hook_on

APU  = $8100
NTRK = 10                       ; tracks 0-4: music on channels 0-4, 5-9: effects on channels 0-4
NMUS = 5

        .zeropage
zp_p:   .res 2                  ; stream being read
zp_q:   .res 2                  ; song / effect header
zp_t:   .res 2                  ; instrument record, sequence

        .bss
_ses_on:
t_on:   .res NTRK
t_plo:  .res NTRK
t_phi:  .res NTRK
t_wait: .res NTRK               ; rows to the next event
t_dur:  .res NTRK
t_ilo:  .res NTRK               ; instrument record
t_ihi:  .res NTRK
t_tlo:  .res NTRK               ; instrument table
t_thi:  .res NTRK
t_wlo:  .res NTRK               ; wave table
t_whi:  .res NTRK
t_note: .res NTRK
t_pos:  .res 4 * NTRK           ; sequence positions: volume, arpeggio, pitch, duty
t_rel:  .res NTRK               ; 0 held, 1 released, 2 silent (cut)
t_paclo: .res NTRK              ; pitch sequence sum
t_pachi: .res NTRK
t_cvol: .res NTRK
t_vib:  .res NTRK
t_vph:  .res NTRK
t_vdel: .res NTRK
t_port: .res NTRK
t_curlo: .res NTRK              ; portamento: the period now
t_curhi: .res NTRK
t_det:  .res NTRK
t_rlo:  .res NTRK               ; return address of a call
t_rhi:  .res NTRK
t_llo:  .res NTRK               ; loop start and count
t_lhi:  .res NTRK
t_lcnt: .res NTRK
t_pri:  .res NTRK
t_tr:   .res NTRK               ; transposition of an effect (ses_perde)
t_outv: .res NTRK               ; this frame: volume, duty bits (wave number), period
t_outd: .res NTRK
t_perlo: .res NTRK
t_perhi: .res NTRK
t_bn:   .res NTRK               ; note of the cached period below ($FF: none)
t_blo:  .res NTRK
t_bhi:  .res NTRK
ch_last: .res 5                 ; control byte last written ($FF: write again)
ch_src: .res 5                  ; track heard on the channel ($FF: none)
ch_wave: .res 5
m_req:  .res 1                  ; 1 start the song at m_slo/m_shi, 2 stop
m_slo:  .res 1
m_shi:  .res 1
m_tempo: .res 1
m_extra: .res 1
m_acc:  .res 1
_ses_playing:
m_playing: .res 1
m_mute: .res 1
m_vol:  .res 1
m_fade: .res 1
m_fcnt: .res 1
q_lo:   .res 4                  ; effects waiting to start
q_hi:   .res 4
q_tr:   .res 4
m_ntr:  .res 1
s_tr:   .res 1
q_wr:   .res 1
q_rd:   .res 1
s_ver:  .res 1
s_a:    .res 1
s_x:    .res 1
s_s:    .res 1
s_pi:   .res 1
s_pos:  .res 1
s_len:  .res 1
s_loop: .res 1
s_relp: .res 1
s_lend: .res 1
s_val:  .res 1
s_rlo:  .res 1
s_rhi:  .res 1
s_v:    .res 1
s_arp:  .res 1
s_d:    .res 1
s_n:    .res 1
s_ch:   .res 1
s_o:    .res 1
s_plo:  .res 1
s_phi:  .res 1
s_t:    .res 1
s_ma:   .res 1
s_src:  .res 1
s_y:    .res 1
s_cnt:  .res 1
s_pri:  .res 1
s_guard: .res 1

        .code
; ---------------------------------------------------------------- C interface
; void __fastcall__ ses_muzik(const unsigned char *song)
_ses_muzik:
        jsr ensure
        ldy #0
        sty m_req
        sta m_slo
        stx m_shi
        iny
        sty m_req
        rts

; void ses_dur(void)
_ses_dur:
        jsr ensure
        lda #2
        sta m_req
        rts

; void __fastcall__ ses_efekt(const unsigned char *sfx)
_ses_efekt:
        jsr ensure
        ldy q_wr
        sta q_lo,y
        txa
        sta q_hi,y
        lda m_ntr
        sta q_tr,y
        lda #0
        sta m_ntr
        iny
        tya
        and #3
        sta q_wr
        rts

; void __fastcall__ ses_perde(signed char semitones): the next ses_efekt plays this much higher
_ses_perde:
        sta m_ntr
        rts

; void __fastcall__ ses_hizlan(unsigned char extra): tempo = the song's + extra
_ses_hizlan:
        sta m_extra
        rts

; void __fastcall__ ses_sessiz(unsigned char mask): music channels not heard (bit n = channel n)
_ses_sessiz:
        sta m_mute
        rts

; void __fastcall__ ses_ses(unsigned char vol): music volume 0..15
_ses_ses:
        sta m_vol
        rts

; void __fastcall__ ses_sondur(unsigned char speed): fade the music out (1 step every speed frames), then stop
_ses_sondur:
        sta m_fcnt
        sta m_fade
        rts

; unsigned char ses_caliyor(void): music playing (or about to start)
_ses_caliyor:
        lda m_req
        cmp #1
        beq @y
        lda m_playing
        beq @n
@y:     lda #1
@n:     ldx #0
        rts

; unsigned char ses_surum(void): APU version (1 or 2)
_ses_surum:
        jsr ensure
        lda s_ver
        ldx #0
        rts

; makes the engine the NMI hook (after k_init, which clears the hook); keeps A and X
ensure:
        sta s_a
        stx s_x
        lda _k_nmi_hook_on
        beq @inst
        lda _k_nmi_hook
        cmp #<_ses_tick
        bne @inst
        lda _k_nmi_hook+1
        cmp #>_ses_tick
        beq @ok
@inst:  lda #0
        sta _k_nmi_hook_on
        lda #<_ses_tick
        sta _k_nmi_hook
        lda #>_ses_tick
        sta _k_nmi_hook+1
        ldx #1
        lda APU+$12             ; APU v2 answers $A2, v1 the channel activity (0..15)
        and #$F0
        cmp #$A0
        bne @v
        inx
        lda #$1F                ; v2: the wave channel on too
        sta APU+$11
@v:     stx s_ver
        lda #15
        sta m_vol
        lda #$FF
        ldx #4
@c:     sta ch_src,x
        sta ch_last,x
        sta ch_wave,x
        dex
        bpl @c
        lda #1
        sta _k_nmi_hook_on
@ok:    lda s_a
        ldx s_x
        rts

; ---------------------------------------------------------------- once a frame (NMI)
_ses_tick:
        lda m_req
        beq @nreq
        cmp #1
        bne @stop
        jsr song_start
        jmp @rq0
@stop:  jsr music_stop
@rq0:   lda #0
        sta m_req
@nreq:
@q:     ldx q_rd                ; start the effects asked for
        cpx q_wr
        beq @qd
        lda q_lo,x
        sta zp_q
        lda q_hi,x
        sta zp_q+1
        lda q_tr,x
        sta s_tr
        jsr sfx_start
        lda q_rd
        clc
        adc #1
        and #3
        sta q_rd
        jmp @q
@qd:    lda m_fade              ; fade-out
        beq @nf
        dec m_fcnt
        bne @nf
        sta m_fcnt
        lda m_vol
        beq @fend
        dec m_vol
        bne @nf
@fend:  jsr music_stop
        lda #0
        sta m_fade
        lda #15
        sta m_vol
@nf:    lda m_playing           ; music rows
        beq @nm
        lda m_tempo
        clc
        adc m_extra
        bcc @t
        lda #255
@t:     clc
        adc m_acc
        sta m_acc
        bcc @nm
        ldx #0
@ml:    lda t_on,x
        beq @mn
        dec t_wait,x
        bne @mn
        jsr parse
@mn:    inx
        cpx #NMUS
        bne @ml
@nm:    ldx #NMUS               ; effect rows: one a frame
@sl:    lda t_on,x
        beq @sn
        dec t_wait,x
        bne @sn
        jsr parse
@sn:    inx
        cpx #NTRK
        bne @sl
        ldx #0                  ; sequences, periods, volumes
@ul:    lda t_on,x
        beq @un
        jsr update
@un:    inx
        cpx #NTRK
        bne @ul
        ldx #0                  ; to the APU
@ol:    jsr output
        inx
        cpx #NMUS
        bne @ol
        rts

music_stop:
        lda #0
        sta m_playing
        ldx #NMUS - 1
@l:     sta t_on,x
        dex
        bpl @l
        rts

song_start:
        lda m_slo
        sta zp_q
        lda m_shi
        sta zp_q+1
        ldy #0
        lda (zp_q),y
        sta m_tempo
        lda #$FF                ; the first row at once
        sta m_acc
        lda s_ver               ; APU v2: the song's output filter
        cmp #2
        bne @nf
        iny
        lda (zp_q),y
        and #3
        sta APU+$13
@nf:
        lda #0
        sta m_playing
        ldx #0
@l:     txa
        asl a
        adc #6
        tay
        lda (zp_q),y
        sta t_plo,x
        iny
        lda (zp_q),y
        sta t_phi,x
        jsr header_tables
        jsr track_reset
        lda t_on,x
        ora m_playing
        sta m_playing
        inx
        cpx #NMUS
        bne @l
        rts

; instrument and wave tables of track X from the header at zp_q (song: at +2, effect: at +0)
header_tables:
        ldy #2
        cpx #NMUS
        bcc @s
        ldy #0
@s:     lda (zp_q),y
        sta t_tlo,x
        iny
        lda (zp_q),y
        sta t_thi,x
        iny
        lda (zp_q),y
        sta t_wlo,x
        iny
        lda (zp_q),y
        sta t_whi,x
        rts

track_reset:
        lda t_phi,x
        beq @off
        lda #1
@off:   sta t_on,x
        lda #1
        sta t_wait,x
        lda #4
        sta t_dur,x
        lda #15
        sta t_cvol,x
        lda #2                  ; silent until the first note
        sta t_rel,x
        lda #0
        sta t_vib,x
        sta t_port,x
        sta t_det,x
        sta t_curlo,x
        sta t_curhi,x
        sta t_note,x
        sta t_paclo,x
        sta t_pachi,x
        sta t_outv,x
        sta t_tr,x
        lda #$FF
        sta t_bn,x
        lda #0
        ; (instrument 0)

; A = instrument number
set_inst:
        asl a
        tay
        lda t_tlo,x
        sta zp_t
        lda t_thi,x
        sta zp_t+1
        lda (zp_t),y
        sta t_ilo,x
        iny
        lda (zp_t),y
        sta t_ihi,x
        rts

; zp_q: effect header; starts each part whose channel is free or plays a lower priority
sfx_start:
        ldy #4
        lda (zp_q),y
        sta s_cnt
        iny
@l:     lda (zp_q),y
        iny
        clc
        adc #NMUS
        tax
        lda (zp_q),y
        iny
        sta s_pri
        lda t_on,x
        beq @take
        lda s_pri
        cmp t_pri,x
        bcc @skip
@take:  lda (zp_q),y
        sta t_plo,x
        iny
        lda (zp_q),y
        sta t_phi,x
        iny
        sty s_y
        lda s_pri
        sta t_pri,x
        jsr header_tables
        jsr track_reset
        lda s_tr
        sta t_tr,x
        ldy s_y
        jmp @nx
@skip:  iny
        iny
@nx:    dec s_cnt
        bne @l
        rts

; ---------------------------------------------------------------- stream events of track X
getb:   ldy #0
        lda (zp_p),y
        inc zp_p
        bne @r
        inc zp_p+1
@r:     rts

parse:  lda t_plo,x
        sta zp_p
        lda t_phi,x
        sta zp_p+1
        lda #64                 ; a broken stream never hangs the NMI
        sta s_guard
p_next: dec s_guard
        beq p_end
        jsr getb
        cmp #$80
        bcs @dur
        cmp #$60
        bcc h_note
        sbc #$60
        cmp #14
        bcs p_end
        asl a
        tay
        lda ptab+1,y
        pha
        lda ptab,y
        pha
        rts
@dur:   and #$7F
        sta t_dur,x
        jmp p_next

p_end:  lda #0
        sta t_on,x
        cpx #NMUS
        bcs @r
        ldy #NMUS - 1
@l:     ora t_on,y
        dey
        bpl @l
        sta m_playing
@r:     rts

ptab:   .word h_rest-1, row_done-1, h_inst-1, h_vol-1, h_vib-1, h_port-1, h_det-1, h_tempo-1
        .word h_call-1, h_ret-1, h_jump-1, h_lstart-1, h_lend-1, p_end-1

h_note: clc
        adc t_tr,x
        cmp #96
        bcc @in
        lda #95
@in:    sta t_note,x
        lda #0
        sta t_rel,x
        sta t_paclo,x
        sta t_pachi,x
        sta t_vph,x
        lda #3
        sta s_s
@p:     lda s_s
        jsr posidx
        lda #0
        sta t_pos,y
        dec s_s
        bpl @p
        lda #8
        sta t_vdel,x
row_done:
        lda t_dur,x
        sta t_wait,x
save:   lda zp_p
        sta t_plo,x
        lda zp_p+1
        sta t_phi,x
        rts

h_rest: lda #1
        sta t_rel,x
        lda #3
        sta s_s
@l:     jsr rel_seq
        dec s_s
        bpl @l
        jmp row_done

; sequence s_s of the instrument: go to its release part; a volume sequence without one: silence
rel_seq:
        jsr seq_ptr
        beq @none
        ldy #2
        lda (zp_t),y
        cmp #$FF
        beq @none
        pha
        lda s_s
        jsr posidx
        pla
        sta t_pos,y
        rts
@none:  lda s_s
        bne @r
        lda #2
        sta t_rel,x
@r:     rts

h_inst: jsr getb
        jsr set_inst
        jmp p_next
h_vol:  jsr getb
        sta t_cvol,x
        jmp p_next
h_vib:  jsr getb
        sta t_vib,x
        jmp p_next
h_port: jsr getb
        sta t_port,x
        jmp p_next
h_det:  jsr getb
        sta t_det,x
        jmp p_next
h_tempo: jsr getb
        cpx #NMUS
        bcs @r
        sta m_tempo
@r:     jmp p_next
h_call: jsr getb
        pha
        jsr getb
        pha
        lda zp_p
        sta t_rlo,x
        lda zp_p+1
        sta t_rhi,x
        pla
        sta zp_p+1
        pla
        sta zp_p
        jmp p_next
h_ret:  lda t_rlo,x
        sta zp_p
        lda t_rhi,x
        sta zp_p+1
        jmp p_next
h_jump: jsr getb
        pha
        jsr getb
        sta zp_p+1
        pla
        sta zp_p
        jmp p_next
h_lstart:
        jsr getb
        sta t_lcnt,x
        lda zp_p
        sta t_llo,x
        lda zp_p+1
        sta t_lhi,x
        jmp p_next
h_lend: dec t_lcnt,x
        beq @r
        lda t_llo,x
        sta zp_p
        lda t_lhi,x
        sta zp_p+1
@r:     jmp p_next

; Y = A * NTRK + X
posidx: sta s_t
        asl a
        asl a
        adc s_t
        asl a
        stx s_t
        adc s_t
        tay
        rts

; zp_t = sequence s_s of track X's instrument (Z set: none)
seq_ptr:
        lda t_ilo,x
        sta zp_t
        lda t_ihi,x
        sta zp_t+1
        lda s_s
        asl a
        tay
        lda (zp_t),y
        pha
        iny
        lda (zp_t),y
        sta zp_t+1
        pla
        sta zp_t
        ora zp_t+1
        rts

; value of sequence s_s now, and step it (C clear: the instrument has none)
seq_get:
        jsr seq_ptr
        bne @have
        clc
        rts
@have:  lda s_s
        jsr posidx
        sty s_pi
        lda t_pos,y
        sta s_pos
        ldy #0
        lda (zp_t),y
        sta s_len
        iny
        lda (zp_t),y
        sta s_loop
        sta s_lend
        iny
        lda (zp_t),y
        sta s_relp
        lda s_pos
        clc
        adc #3
        tay
        lda (zp_t),y
        sta s_val
        lda t_rel,x             ; released: a loop of the sustain part no longer applies
        beq @a
        lda s_relp
        cmp #$FF
        beq @a
        lda s_loop
        cmp s_relp
        bcs @a
        lda #$FF
        sta s_lend
@a:     ldy s_pos
        iny
        lda t_rel,x
        bne @tail
        lda s_relp
        cmp #$FF
        beq @tail
        cpy s_relp
        bcc @ok
        lda s_loop              ; held at the release point: loop, or wait on the last sustain value
        cmp s_relp
        bcs @hold
        tay
        jmp @ok
@hold:  ldy s_relp
        dey
        jmp @ok
@tail:  cpy s_len
        bcc @ok
        lda s_lend
        cmp #$FF
        beq @hend
        tay
        jmp @ok
@hend:  ldy s_len
        dey
@ok:    tya
        ldy s_pi
        sta t_pos,y
        lda s_val
        sec
        rts

; A * Y / 15 (A, Y = 0..15), rounded
mul15:  asl a
        asl a
        asl a
        asl a
        sta s_ma
        tya
        ora s_ma
        tay
        lda voltab,y
        rts

; s_plo/s_phi += A (signed)
add_s8: ldy #0
        cmp #$80
        bcc @p
        dey
@p:     clc
        adc s_plo
        sta s_plo
        tya
        adc s_phi
        sta s_phi
        rts

; ---------------------------------------------------------------- per frame: track X's sound now
update: txa
        cmp #NMUS
        bcc @c
        sbc #NMUS
@c:     sta s_ch
        lda #0                  ; volume
        sta s_s
        jsr seq_get
        bcs @v
        lda #15
@v:     sta s_v
        lda #1                  ; arpeggio
        sta s_s
        jsr seq_get
        bcs @a
        lda #0
@a:     sta s_arp
        lda #2                  ; pitch: added up
        sta s_s
        jsr seq_get
        bcc @np
        ldy #0
        cmp #$80
        bcc @pp
        dey
@pp:    clc
        adc t_paclo,x
        sta t_paclo,x
        tya
        adc t_pachi,x
        sta t_pachi,x
@np:    lda #3                  ; duty / wave
        sta s_s
        jsr seq_get
        bcs @d
        lda #2
        ldy s_ch
        cpy #4
        bne @d
        lda #0
@d:     sta s_d
        ; note + arpeggio, 0..95
        lda t_note,x
        clc
        adc s_arp
        cmp #96
        bcc @nok
        lda s_arp
        bmi @lo
        lda #95
        bne @nok
@lo:    lda #0
@nok:   sta s_n
        cmp t_bn,x              ; the same note as last frame: its period is known
        bne @calc
        lda t_blo,x
        sta s_plo
        lda t_bhi,x
        sta s_phi
        jmp @port
@calc:  sta t_bn,x
        lda s_ch
        cmp #3
        bne @tone
        lda s_n                 ; noise: 16 rates
        and #15
        asl a
        tay
        lda noisetab,y
        sta s_plo
        lda noisetab+1,y
        sta s_phi
        jmp @cache
@tone:  lda s_n
        ldy s_ch
        cpy #2                  ; triangle and wave: 32 steps a cycle, so two octaves up in the table
        beq @up
        cpy #4
        bne @oct
@up:    clc
        adc #24
        cmp #96
        bcc @oct
        lda #95
@oct:   ldy #0
@dv:    cmp #12
        bcc @dd
        sbc #12
        iny
        bne @dv
@dd:    sty s_o
        asl a
        tay
        lda basetab,y
        sta s_plo
        lda basetab+1,y
        sta s_phi
        ldy s_o
        iny
        iny
        iny
@sh:    lsr s_phi
        ror s_plo
        dey
        bne @sh
        lsr s_phi               ; one more, rounding with the bit shifted out
        ror s_plo
        lda s_plo
        adc #0
        sta s_plo
        lda s_phi
        adc #0
        sta s_phi
        lda s_plo               ; - 1
        bne @m1
        dec s_phi
@m1:    dec s_plo
@cache: lda s_plo
        sta t_blo,x
        lda s_phi
        sta t_bhi,x
@port:  lda t_port,x            ; portamento: glide from the period now
        beq @snap
        lda t_curlo,x
        ora t_curhi,x
        beq @snap
        lda s_plo               ; target - now
        sec
        sbc t_curlo,x
        sta s_t
        lda s_phi
        sbc t_curhi,x
        bmi @down
        bne @upc
        lda s_t
        cmp t_port,x
        bcc @snap               ; closer than one step
@upc:   lda t_curlo,x
        clc
        adc t_port,x
        sta s_plo
        lda t_curhi,x
        adc #0
        sta s_phi
        jmp @snap
@down:  cmp #$FF
        bne @dnc
        lda s_t
        clc
        adc t_port,x
        bcs @snap               ; closer than one step
@dnc:   lda t_curlo,x
        sec
        sbc t_port,x
        sta s_plo
        lda t_curhi,x
        sbc #0
        sta s_phi
@snap:  lda s_plo
        sta t_curlo,x
        lda s_phi
        sta t_curhi,x
        lda s_plo               ; + pitch sequence
        clc
        adc t_paclo,x
        sta s_plo
        lda s_phi
        adc t_pachi,x
        sta s_phi
        lda t_det,x             ; + detune
        jsr add_s8
        lda t_vib,x             ; + vibrato
        beq @nv
        lda t_vdel,x
        beq @vr
        dec t_vdel,x
        jmp @nv
@vr:    lda t_vib,x
        and #$0F
        clc
        adc t_vph,x
        sta t_vph,x
        lsr a
        lsr a
        lsr a
        lsr a
        tay
        lda lfotab,y
        sta s_t
        lda t_vib,x
        lsr a
        lsr a
        lsr a
        lsr a
        tay
        lda #0
@vm:    dey
        bmi @vd
        clc
        adc s_t
        jmp @vm
@vd:    cmp #$80                ; / 4, signed
        ror a
        cmp #$80
        ror a
        jsr add_s8
@nv:    lda s_phi               ; 1..4095
        bpl @pos
        lda #0
        sta s_phi
        lda #1
        sta s_plo
@pos:   cmp #$10
        bcc @pok
        lda #$0F
        sta s_phi
        lda #$FF
        sta s_plo
@pok:   lda s_plo
        sta t_perlo,x
        lda s_phi
        sta t_perhi,x
        ; volume: sequence x channel volume (x music volume)
        lda t_rel,x
        cmp #2
        bne @vol
        lda #0
        beq @vset
@vol:   lda t_cvol,x
        ldy s_v
        jsr mul15
        cpx #NMUS
        bcs @vset
        ldy m_vol
        jsr mul15
@vset:  sta t_outv,x
        ; duty bits 5-4, noise short mode bit 6 (sequence values 0-3, +4 short); wave: the wave number
        lda s_d
        ldy s_ch
        cpy #4
        beq @dset
        and #7
        asl a
        asl a
        asl a
        asl a
@dset:  sta t_outd,x
        rts

; ---------------------------------------------------------------- channel X to the APU
output: cpx #4
        bne @go
        lda s_ver
        cmp #2
        beq @go
        rts
@go:    lda t_on+NMUS,x         ; an effect first
        beq @mus
        txa
        clc
        adc #NMUS
        tay
        jmp @src
@mus:   lda t_on,x
        beq @none
        lda m_mute
        and bitmask,x
        bne @none
        txa
        tay
        jmp @src
@none:  lda ch_src,x
        cmp #$FF
        beq @r
        lda #$FF
        sta ch_src,x
        sta ch_last,x
        ldy chbase,x
        lda #0
        sta APU+2,y
        sta APU+3,y
@r:     rts
@src:   sty s_src
        lda t_perlo,y
        sta s_plo
        lda t_perhi,y
        sta s_phi
        lda t_outv,y
        sta s_v
        lda t_outd,y
        sta s_d
        cpx #4
        bne @nw
        cmp ch_wave+4
        beq @nw0
        jsr load_wave
@nw0:   lda #0
        sta s_d
@nw:    ldy chbase,x
        lda s_plo
        sta APU,y
        lda s_phi
        sta APU+1,y
        lda s_d
        ora s_v
        cmp ch_last,x
        bne @w
        lda s_src
        cmp ch_src,x
        beq @r
        lda s_d
        ora s_v
@w:     sta ch_last,x
        sta APU+2,y
        lda #0                  ; (re)start: volume from the control byte, no length limit
        sta APU+3,y
        lda s_src
        sta ch_src,x
        rts

; wave s_d of track s_src's wave table into the wave RAM
load_wave:
        sta ch_wave+4
        ldy s_src
        lda t_wlo,y
        sta zp_t
        lda t_whi,y
        sta zp_t+1
        ora zp_t
        beq @r
        lda s_d
        asl a
        asl a
        asl a
        asl a
        tay
        ldx #0
@l:     lda (zp_t),y
        sta APU+$20,x
        iny
        inx
        cpx #16
        bne @l
        ldx #4
@r:     rts

        .rodata
chbase: .byte $00, $04, $08, $0C, $14
bitmask: .byte 1, 2, 4, 8, 16
; square period x 32 of C2..B2 (P = round(this / 2^(octave + 4)) - 1)
basetab: .word 47778, 45097, 42566, 40177, 37922, 35793, 33784, 31888, 30098, 28409, 26815, 25310
noisetab: .word 0, 1, 3, 7, 15, 23, 31, 47, 63, 95, 127, 191, 255, 383, 511, 1023
voltab:
        .repeat 16, I
        .repeat 16, J
        .byte (I * J * 2 + 15) / 30
        .endrepeat
        .endrepeat
lfotab: .byte 0, 2, 4, 6, 8, 6, 4, 2, 0, <-2, <-4, <-6, <-8, <-6, <-4, <-2
