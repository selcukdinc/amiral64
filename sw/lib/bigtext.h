/* Big shaded letters for titles: each font glyph doubled to 16x16 (4 tiles), coloured top to bottom with
 * colours 2..9 of one palette and a one-pixel drop shadow in colour 1. Glyphs are made on demand into
 * "slots" of 4 consecutive tiles the game gives (one slot per different letter on screen). */
#ifndef BIGTEXT_H
#define BIGTEXT_H
void big_init(const unsigned char *slot_tiles, unsigned char slots, unsigned char pal);
void big_reset(void);                        /* forget the glyphs (start a new set of titles) */
void big_text(unsigned char x, unsigned char y, const char *s);   /* 2 rows high, 2 columns per letter */
void big_center(unsigned char y, const char *s);
void big_palette(unsigned char level);       /* 0..8: fade the title colours in */
unsigned char big_len(const char *s);
#endif
