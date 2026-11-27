/* Retro konsol game library (cc65). Screen: 320x240, tile map 64x32 of 8x8 tiles, 64 sprites of 16x16.
 * Tiles 32..95 hold the font (ASCII); games use tiles 0..31 and 96..255 and sprite patterns 0..63.
 * VRAM can be written at any time (it is dual ported); sprites go through the OAM shadow, copied every frame. */
#ifndef KONSOL_H
#define KONSOL_H

#define REG(a) (*(volatile unsigned char *)(a))
#define PPU_VADDR_L REG(0x8000)
#define PPU_VADDR_H REG(0x8001)
#define PPU_VDATA   REG(0x8002)
#define PPU_CTRL    REG(0x8003)
#define PPU_STATUS  REG(0x8004)
#define PPU_SCROLL_XL REG(0x8005)
#define PPU_SCROLL_XH REG(0x8006)
#define PPU_SCROLL_Y REG(0x8007)
#define PPU_OAMDMA  REG(0x8008)
#define PPU_LINE    REG(0x8009)
#define PPU_RASTER  REG(0x800A)
#define APU(n)      REG(0x8100 + (n))
#define PAD_REG(n)  REG(0x8200 + (n))
#define SEG(n)      REG(0x8400 + (n))
#define LEDS_LO     REG(0x8406)
#define LEDS_HI     REG(0x8407)
#define SYS_TEST    REG(0x87FF)
#define KEYMAP(n)   REG(0x8210 + (n))   /* 256 key-down bits, PS/2 set 2 codes (E0 keys at code + $80) */

/* PS/2 set 2 codes for k_key (E0 keys + $80) */
#define KEY_ESC   0x76
#define KEY_Z     0x1A
#define KEY_X     0x22
#define KEY_C     0x21
#define KEY_SPACE 0x29
#define KEY_ENTER 0x5A

/* pad bits */
#define BTN_A      0x80
#define BTN_B      0x40
#define BTN_SELECT 0x20
#define BTN_START  0x10
#define BTN_UP     0x08
#define BTN_DOWN   0x04
#define BTN_LEFT   0x02
#define BTN_RIGHT  0x01

/* tile map attributes / sprite attributes */
#define ATTR_PAL(p) (p)
#define ATTR_FLIPX  0x10
#define ATTR_FLIPY  0x20
#define SPR_BEHIND  0x40

#define CTRL_NMI 0x01
#define CTRL_BG  0x02
#define CTRL_SPR 0x04
#define CTRL_RASTER 0x08

typedef struct { unsigned char y, x, pattern, attr; } sprite_t;
#define oam ((sprite_t *)0x0300)

extern volatile unsigned char frame_count;
extern unsigned char pad[4], pad_new[4];
extern unsigned char split_line, split_xl, split_xh, split_y;   /* raster split (0 = none) */
extern unsigned char k_esc_menu;           /* 1 (default): Esc on the keyboard goes back to the menu */

void k_init(void);                         /* NMI handler, font, default palettes, clear screen and sprites */
void k_wait_frame(void);                   /* wait for the next vblank, then read the pads (Esc: k_menu) */
unsigned char k_key(unsigned char code);   /* a keyboard key is down (PS/2 code, see KEY_...) */
void k_vaddr(unsigned addr);
void k_vfill(unsigned addr, unsigned char v, unsigned len);
void k_vcopy(unsigned addr, const unsigned char *src, unsigned len);
void k_vunpack(unsigned addr, const unsigned char *src);   /* run-length packed data (tools/gfx.py --rle) */
void k_tile(unsigned char x, unsigned char y, unsigned char tile, unsigned char attr);
void k_fill_tiles(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char tile, unsigned char attr);
void k_print(unsigned char x, unsigned char y, const char *s, unsigned char pal);
void k_print_num(unsigned char x, unsigned char y, unsigned n, unsigned char digits, unsigned char pal);
void k_clear_map(void);
void k_palette(unsigned char pal, const unsigned *colors, unsigned char n);   /* 12-bit 0xRGB colours */
void k_sprite(unsigned char i, int x, int y, unsigned char pattern, unsigned char attr);   /* off screen = hidden */
void k_hide_sprites(unsigned char from);
void k_scroll(unsigned x, unsigned char y);
void k_split(unsigned char line, unsigned x, unsigned char y);   /* from line+2 on use this scroll */
unsigned char k_rand(void);
void k_seed(unsigned s);
void k_tone(unsigned char ch, unsigned period, unsigned char ctl, unsigned char env);
void k_sfx(unsigned char id);
void k_sound_off(void);
void k_seg_number(unsigned n);             /* decimal on the 7-segment display */
void k_menu(void);                         /* back to the boot ROM menu */
void k_ek_yukle(void);                     /* extra data from the cartridge (game_c_ek.cfg, lib/ek.c) */

/* sound effects for k_sfx */
#define SFX_BLIP    0
#define SFX_SHOOT   1
#define SFX_BOOM    2
#define SFX_PICKUP  3
#define SFX_FALL    4
#define SFX_WIN     5
#define SFX_LOSE    6
#define SFX_TICK    7

#endif
