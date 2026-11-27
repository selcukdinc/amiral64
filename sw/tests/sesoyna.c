/* Sound test player (tools/sesdinle.py): plays the songs and effects of the music file it is linked with
 * (ses_muzikler, ses_efektler; a game's music in the cartridge's extra data is read in first); the tool writes komut (1 song, 2 effect, 3 stop) and secim. */
#include "konsol.h"
#include "ses.h"

extern const unsigned char *const ses_muzikler[];
extern const unsigned char *const ses_efektler[];
unsigned char komut, secim, surum;

void main(void) {
    k_ek_yukle();
    k_init();
    k_print(2, 2, "SES ODASI", 1);
    surum = ses_surum();
    for (;;) {
        k_wait_frame();
        if (komut == 1) ses_muzik(ses_muzikler[secim]);
        else if (komut == 2) ses_efekt(ses_efektler[secim]);
        else if (komut == 3) ses_dur();
        komut = 0;
    }
}
