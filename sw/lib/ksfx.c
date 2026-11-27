/* k_sfx: the library's standard sound effects (lib/ses_std.ses), played by the sound engine. */
#include "konsol.h"
#include "ses.h"
#include "ses_std.h"

static const unsigned char *const std_sfx[8] = {
    sfx_std_blip, sfx_std_shoot, sfx_std_boom, sfx_std_pickup, sfx_std_fall, sfx_std_win, sfx_std_lose, sfx_std_tick
};

void k_sfx(unsigned char id) {
    if (id < 8) ses_efekt(std_sfx[id]);
}
