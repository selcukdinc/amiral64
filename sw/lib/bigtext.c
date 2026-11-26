/* Big shaded letters: see bigtext.h */
#include "konsol.h"
#include "bigtext.h"

extern const unsigned char *const k_font;    /* konsol.c: 8x8 glyphs for ASCII 32..95 */

static const unsigned char *slot_tile;
static unsigned char nslots, used, bpal;
static char slot_char[16];
static unsigned m16[17];
static const unsigned char dbl[16] = { 0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F, 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF };

void big_init(const unsigned char *slot_tiles, unsigned char slots, unsigned char pal) {
    slot_tile = slot_tiles; nslots = slots > 16 ? 16 : slots; bpal = pal;
    big_reset();
}

void big_reset(void) {
    unsigned char i;
    used = 0;
    for (i = 0; i < 16; ++i) slot_char[i] = 0;
}

static void render(unsigned char tile, char c) {
    unsigned char y, ty, tx, py, px, b, v, sh;
    unsigned m, shadow, bit;
    const unsigned char *g = k_font + (unsigned)(c - 32) * 8;
    m16[0] = 0;
    for (y = 0; y < 16; ++y) {
        b = g[y >> 1];
        m16[y + 1] = (unsigned)dbl[b >> 4] << 8 | dbl[b & 15];
    }
    for (ty = 0; ty < 2; ++ty)
        for (tx = 0; tx < 2; ++tx) {
            k_vaddr((unsigned)(tile + ty * 2 + tx) * 32);
            for (py = 0; py < 8; ++py) {
                y = ty * 8 + py;
                m = m16[y + 1];
                shadow = (m16[y] >> 1) & ~m;
                bit = tx ? 0x0080 : 0x8000;
                v = 2 + (y >> 1);
                for (px = 0; px < 4; ++px) {
                    sh = (m & bit) ? v << 4 : (shadow & bit) ? 0x10 : 0;
                    bit >>= 1;
                    sh |= (m & bit) ? v : (shadow & bit) ? 1 : 0;
                    bit >>= 1;
                    PPU_VDATA = sh;
                }
            }
        }
}

static unsigned char glyph(char c) {
    unsigned char s;
    for (s = 0; s < used; ++s) if (slot_char[s] == c) return slot_tile[s];
    if (used == nslots) used = 0;
    s = used++;
    slot_char[s] = c;
    render(slot_tile[s], c);
    return slot_tile[s];
}

unsigned char big_len(const char *s) {
    unsigned char n = 0;
    while (s[n]) ++n;
    return n;
}

void big_text(unsigned char x, unsigned char y, const char *s) {
    unsigned char tile;
    unsigned a;
    char c;
    for (; (c = *s) && x < 63; ++s, x += 2) {
        if (c == ' ') continue;
        if (c >= 'a' && c <= 'z') c -= 32;
        tile = glyph(c);
        a = 0x4000 + ((unsigned)y << 7) + ((unsigned)x << 1);
        k_vaddr(a); PPU_VDATA = tile; PPU_VDATA = bpal; PPU_VDATA = tile + 1; PPU_VDATA = bpal;
        k_vaddr(a + 128); PPU_VDATA = tile + 2; PPU_VDATA = bpal; PPU_VDATA = tile + 3; PPU_VDATA = bpal;
    }
}

void big_center(unsigned char y, const char *s) {
    unsigned char n = big_len(s);
    if (n > 20) n = 20;
    big_text((40 - 2 * n) / 2, y, s);
}

void big_palette(unsigned char level) {
    static const unsigned grad[8] = { 0xFFF, 0xEFF, 0xDFF, 0xBEF, 0x9DF, 0x7BF, 0x59E, 0x37C };
    unsigned char k, r, g, b;
    unsigned c;
    k_vaddr(0x5000 + ((unsigned)bpal << 5) + 2);
    PPU_VDATA = 0x01; PPU_VDATA = 0x00;
    for (k = 0; k < 8; ++k) {
        c = grad[k];
        r = ((c >> 8) * level) >> 3; g = (((c >> 4) & 15) * level) >> 3; b = ((c & 15) * level) >> 3;
        c = (unsigned)r << 8 | g << 4 | b;
        PPU_VDATA = (unsigned char)c; PPU_VDATA = c >> 8;
    }
}
