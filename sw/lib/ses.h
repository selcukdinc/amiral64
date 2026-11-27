/* Ses motoru: music and sound effects for the konsol games (sw/lib/ses.s).
 * Songs and effects are written in MML (games/<name>/muzik.ses, compiled by tools/sesc.py into
 * mus_NAME / sfx_NAME arrays). From the first call on, the engine runs in the NMI once a frame, so
 * the music keeps its tempo whatever the game loop does. */
#ifndef SES_H
#define SES_H

void __fastcall__ ses_muzik(const unsigned char *song);   /* start a song (from its beginning) */
void ses_dur(void);                                       /* stop the music */
void __fastcall__ ses_efekt(const unsigned char *sfx);    /* play a sound effect */
void __fastcall__ ses_perde(signed char semitones);      /* the next ses_efekt plays this much higher */
void __fastcall__ ses_hizlan(unsigned char extra);        /* tempo = the song's + extra (0: normal) */
void __fastcall__ ses_sessiz(unsigned char mask);         /* music channels muted (bit n: channel n) */
void __fastcall__ ses_ses(unsigned char vol);             /* music volume 0..15 (effects stay loud) */
void __fastcall__ ses_sondur(unsigned char speed);        /* fade the music out, then stop it */
unsigned char ses_caliyor(void);                          /* the music is playing */
unsigned char ses_surum(void);                            /* APU version: 1, or 2 with the wave channel */

#endif
