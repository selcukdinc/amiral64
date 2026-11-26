/* Retro konsol game library: see konsol.h */
#include "konsol.h"
#include "font.h"

unsigned char pad[4], pad_new[4];
const unsigned char *const k_font = font_data;    /* for bigtext.c */
static unsigned char pad_old[4];
static unsigned rnd = 0x7A35;
extern unsigned char scroll_xl, scroll_xh, scroll_y;
void k_install_irq(void);

static const unsigned default_pal[4][2] = {
    { 0x113, 0xFFF },   /* 0: backdrop dark blue, white text */
    { 0x000, 0xFC0 },   /* 1: yellow */
    { 0x000, 0x0FF },   /* 2: cyan */
    { 0x000, 0xF33 },   /* 3: red */
};

void k_vaddr(unsigned addr) {
    PPU_VADDR_L = (unsigned char)addr;
    PPU_VADDR_H = addr >> 8;
}

void k_vfill(unsigned addr, unsigned char v, unsigned len) {
    k_vaddr(addr);
    while (len--) PPU_VDATA = v;
}

void k_vcopy(unsigned addr, const unsigned char *src, unsigned len) {
    k_vaddr(addr);
    while (len--) PPU_VDATA = *src++;
}

void k_tile(unsigned char x, unsigned char y, unsigned char tile, unsigned char attr) {
    k_vaddr(0x4000 + ((unsigned)y << 7) + ((unsigned)x << 1));
    PPU_VDATA = tile;
    PPU_VDATA = attr;
}

void k_fill_tiles(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char tile, unsigned char attr) {
    unsigned char i;
    for (; h; --h, ++y) {
        k_vaddr(0x4000 + ((unsigned)y << 7) + ((unsigned)x << 1));
        for (i = w; i; --i) { PPU_VDATA = tile; PPU_VDATA = attr; }
    }
}

void k_print(unsigned char x, unsigned char y, const char *s, unsigned char pal) {
    unsigned char c;
    k_vaddr(0x4000 + ((unsigned)y << 7) + ((unsigned)x << 1));
    while ((c = *s++)) {
        if (c >= 'a' && c <= 'z') c -= 32;
        PPU_VDATA = c;
        PPU_VDATA = pal;
    }
}

void k_print_num(unsigned char x, unsigned char y, unsigned n, unsigned char digits, unsigned char pal) {
    char buf[6];
    unsigned char i = digits;
    buf[digits] = 0;
    while (i) { buf[--i] = '0' + n % 10; n /= 10; }
    k_print(x, y, buf, pal);
}

void k_clear_map(void) {
    unsigned i;
    k_vaddr(0x4000);
    for (i = 0; i < 2048; ++i) { PPU_VDATA = ' '; PPU_VDATA = 0; }
}

void k_palette(unsigned char pal, const unsigned *colors, unsigned char n) {
    k_vaddr(0x5000 + ((unsigned)pal << 5));
    while (n--) {
        PPU_VDATA = (unsigned char)*colors;          /* GGGGBBBB */
        PPU_VDATA = (unsigned char)(*colors >> 8);   /* ----RRRR */
        ++colors;
    }
}

void k_sprite(unsigned char i, int x, int y, unsigned char pattern, unsigned char attr) {
    sprite_t *s = &oam[i];
    if (x < 0 || x >= 320 || y < 0 || y >= 240) { s->y = 0xF0; return; }
    s->y = (unsigned char)y;
    s->x = (unsigned char)x;
    s->pattern = pattern;
    s->attr = (attr & 0x7F) | (x >= 256 ? 0x80 : 0);
}

void k_hide_sprites(unsigned char from) {
    for (; from < 64; ++from) oam[from].y = 0xF0;
}

void k_scroll(unsigned x, unsigned char y) {
    scroll_xl = (unsigned char)x; scroll_xh = x >> 8; scroll_y = y;
}

void k_split(unsigned char line, unsigned x, unsigned char y) {
    split_xl = (unsigned char)x; split_xh = x >> 8; split_y = y;
    split_line = line;
}

unsigned char k_esc_menu = 1;

unsigned char k_key(unsigned char code) {
    return KEYMAP(code >> 3) & (1 << (code & 7));
}

void k_wait_frame(void) {
    unsigned char f = frame_count, i, p;
    while (frame_count == f) ;
    if (k_esc_menu && (KEYMAP(KEY_ESC >> 3) & (1 << (KEY_ESC & 7)))) k_menu();
    for (i = 0; i < 4; ++i) {
        p = PAD_REG(i);
        pad_new[i] = p & ~pad_old[i];
        pad_old[i] = p;
        pad[i] = p;
    }
}

unsigned char k_rand(void) {
    /* 16-bit xorshift */
    rnd ^= rnd << 7;
    rnd ^= rnd >> 9;
    rnd ^= rnd << 8;
    return (unsigned char)rnd;
}

void k_seed(unsigned s) { rnd = s ? s : 0x7A35; }

void k_tone(unsigned char ch, unsigned period, unsigned char ctl, unsigned char env) {
    unsigned char b = ch << 2;
    APU(b) = (unsigned char)period;
    APU(b + 1) = period >> 8;
    APU(b + 2) = ctl;
    APU(b + 3) = env;      /* starts the note */
}

void k_sfx(unsigned char id) {
    switch (id) {
    case SFX_BLIP:   k_tone(0, 0x080, 0xAA, 0x01); break;
    case SFX_SHOOT:  k_tone(1, 0x060, 0x9C, 0x00); break;
    case SFX_BOOM:   k_tone(3, 0x018, 0x8F, 0x02); break;
    case SFX_PICKUP: k_tone(0, 0x050, 0xAC, 0x01); break;
    case SFX_FALL:   k_tone(3, 0x040, 0x88, 0x01); break;
    case SFX_WIN:    k_tone(0, 0x07C, 0xAF, 0x04); k_tone(2, 0x030, 0x8F, 0x04); break;
    case SFX_LOSE:   k_tone(2, 0x0C0, 0x8F, 0x05); break;
    case SFX_TICK:   k_tone(1, 0x100, 0x86, 0x00); break;
    }
}

void k_sound_off(void) { APU(0x11) = 0; }

void k_seg_number(unsigned n) {
    unsigned char d[4], i;
    for (i = 0; i < 4; ++i) { d[i] = n % 10; n /= 10; }
    SEG(0) = d[1] << 4 | d[0];
    SEG(1) = d[3] << 4 | d[2];
    SEG(2) = 0; SEG(3) = 0;
    SEG(4) = 0xF0;          /* only the right four digits */
}

void k_menu(void) {
    __asm__("sei");
    PPU_CTRL = 0;
    k_sound_off();
    __asm__("jmp ($FFFC)");
}

void k_init(void) {
    unsigned i;
    unsigned char r, b, v, k;
    PPU_CTRL = 0;
    split_line = 0;
    k_scroll(0, 0);
    k_install_irq();
    /* font: 1 bpp glyphs -> 4 bpp tiles 32..95 in colour 1 */
    k_vaddr(32 * 32);
    for (i = 0; i < 64 * 8; ++i) {
        r = font_data[i];
        for (k = 0; k < 4; ++k) {
            v = 0;
            if (r & 0x80) v = 0x10;
            if (r & 0x40) v |= 0x01;
            PPU_VDATA = v;
            r <<= 2;
        }
    }
    for (b = 0; b < 4; ++b) k_palette(b, default_pal[b], 2);
    k_clear_map();
    k_hide_sprites(0);
    for (b = 0; b < 4; ++b) pad_old[b] = PAD_REG(b);
    APU(0x10) = 12;        /* master volume */
    APU(0x11) = 0x0F;
    PPU_CTRL = CTRL_NMI | CTRL_BG | CTRL_SPR;
    SYS_TEST = 0xC0;        /* simulation: the C library started ... */
    k_wait_frame();
    SYS_TEST = 0xC1;        /* ... and vblank interrupts arrive */
}
