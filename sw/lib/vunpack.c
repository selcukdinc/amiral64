/* k_vunpack: run-length packed graphics (tools/gfx.py --rle) into VRAM. */
#include "konsol.h"

void k_vunpack(unsigned addr, const unsigned char *src) {
    unsigned len = src[0] | (unsigned)src[1] << 8;
    unsigned char c, v;
    src += 2;
    k_vaddr(addr);
    while (len) {
        c = *src++;
        if (c & 0x80) {
            c = (c & 0x7F) + 2;
            v = *src++;
            len -= c;
            do PPU_VDATA = v; while (--c);
        } else {
            ++c;
            len -= c;
            do PPU_VDATA = *src++; while (--c);
        }
    }
}
