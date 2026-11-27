/* k_ek_yukle: reads the game's extra data (segment EKDATA, game_c_ek.cfg) from the cartridge into
 * $B000... Call it first thing in main: the boot ROM's loader leaves the cartridge port right after the
 * game, where tools/mkcart.py put the extra data. */
#include "konsol.h"

extern unsigned char _EKDATA_RUN__[], _EKDATA_SIZE__[];

/* a first byte, so the segment (and its size symbols) exists even when a game has no extra data yet */
#pragma rodata-name (push, "EKDATA")
const unsigned char k_ek_imza = 'E';
#pragma rodata-name (pop)

void k_ek_yukle(void) {
    unsigned char *p = _EKDATA_RUN__;
    unsigned n = (unsigned)_EKDATA_SIZE__;
    while (n--) {
        while (!(REG(0x8714) & 1)) ;
        *p++ = REG(0x8713);
    }
}
