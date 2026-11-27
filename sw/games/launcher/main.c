/* Konsol launcher: the cartridge's first entry (flagged in the directory), started by the boot ROM instead
 * of its text menu. Cold start: an animated splash. Then the home screen: a living gradient background with
 * drifting motes, the chosen game's title in big shaded letters with its description, and a strip of game
 * cards that glides to the selection. Left/Right (or Up/Down) choose, A or Start plays: the boot ROM's API
 * at $FFF0 loads the game. Coming back from a game skips the splash and keeps the selection. */
#include "konsol.h"
#include "art.h"
#include "ses.h"
#include "muzik.h"
#include "bigtext.h"

#define CART_ADDR_L REG(0x8710)
#define CART_ADDR_M REG(0x8711)
#define CART_ADDR_H REG(0x8712)
#define CART_DATA   REG(0x8713)
#define CART_STATUS REG(0x8714)
#define CART_SRC    REG(0x8715)
#define WARM ((unsigned char *)0x02FC)      /* 'K', 'W', last selection: survives a game */

#define PAL_BG 4
#define PAL_FRAME 5
#define PAL_SEL 6
#define PAL_TITLE 7
#define PAL_ICON 8
#define PAL_SPR 15

#define MAXG 8
#define STRIP_ROW 17            /* top row of the cards */
#define SPLIT_LINE 118          /* the strip scrolls on its own from line 120 on */
#define CARD_W 8                /* tiles per card slot (6 card + 2 gap) */

/* the cartridge directory (games only, the launcher itself left out) */
unsigned char ngames, sel;
static char names[MAXG][21];
static unsigned char pmin[MAXG], pmax[MAXG], icon[MAXG], index[MAXG];
static unsigned char dir_raw[32];

static unsigned t;              /* frames since start */
static int cam, cam_goal;       /* strip scroll (pixels) */
static unsigned char title_fade;

void clear_rows(unsigned char y, unsigned char h);

/* ---------------------------------------------------------------- text for each icon */
static const char *const desc1[8] = {
    "KARTUSTAKI BIR OYUN",
    "MADENI KAZ, KRISTALLERI TOPLA.",
    "UZAYDA KAYAN BIR NISANCI OYUNU.",
    "ARENAYA FITILLI BOMBALAR KOY,",
    "KOSU, UZUN ATLAMA VE REFLEKS:",
    "DUSEN BLOKLARI YERLESTIR,",
    "POKER ELLERI VE TILSIMLAR:",
    "USSUN KORIDORLARINDA GOLGE AVI.",
};
static const char *const desc2[8] = {
    "",
    "DUSEN KAYALARA DIKKAT ET!",
    "DALGALARI ATLAT, SKORU KATLA.",
    "AYAKTA KALAN SON OYUNCU OL.",
    "MAHALLENIN SAMPIYONU KIM?",
    "SIRALARI TAMAMLA, HIZA DAYAN.",
    "HER TOHUMDA BASKA BIR KADER.",
    "BIRINCI SAHIS, GERCEK ZAMANLI 3B!",
};
static const char *const genre[8] = {
    "OYUN", "MACERA", "AKSIYON", "PARTI", "PARTI", "BULMACA", "KART  ROGUELIKE", "3B NISANCI",
};
static const unsigned char icon_tile[8] = { T_icon0_0, T_icon1_0, T_icon2_0, T_icon3_0, T_icon4_0, T_icon5_0, T_icon6_0, T_icon7_0 };
static const unsigned *const icon_pal[8] = { pal_icon0, pal_icon1, pal_icon2, pal_icon3, pal_icon4, pal_icon5, pal_icon6, pal_icon7 };

/* ---------------------------------------------------------------- cartridge */
static unsigned char cart_byte(void) {
    while (!(CART_STATUS & 1)) ;
    return CART_DATA;
}

static void read_directory(void) {
    unsigned char i, k, n;
    CART_ADDR_L = 0; CART_ADDR_M = 0; CART_ADDR_H = 0;   /* CART_SRC stays where the launcher came from */
    for (k = 0; k < 16; ++k) dir_raw[k] = cart_byte();
    n = dir_raw[4];
    if (n > MAXG) n = MAXG;
    ngames = 0;
    for (i = 0; i < n; ++i) {
        for (k = 0; k < 32; ++k) dir_raw[k] = cart_byte();
        if (dir_raw[29] & 1) continue;               /* the launcher */
        for (k = 0; k < 20; ++k) names[ngames][k] = dir_raw[k];
        names[ngames][20] = 0;
        pmin[ngames] = dir_raw[27]; pmax[ngames] = dir_raw[28];
        icon[ngames] = dir_raw[30] < 8 ? dir_raw[30] : 0;
        index[ngames] = i;
        ++ngames;
    }
}

/* ---------------------------------------------------------------- big letters (lib/bigtext.c)
 * glyph slots: tiles 0-31 and the tiles after the art */
/* (a constant table: cc65 2.18 -O miscompiled the loop that computed it, using the wrong stack offset) */
#define SLOT_HI (TILE_BASE + TILE_COUNT)
/* (10 slots: the most different letters in a title, MAHALLE OLIMPIYATI, BLOK YAGMURU, is 10) */
static const unsigned char slots[10] = { 0, 4, 8, 12, 16, 20, 24, 28, SLOT_HI, SLOT_HI + 4 };
static unsigned char str_len(const char *s) { return big_len(s); }

static void big_title(unsigned char y, const char *s) {
    big_reset();
    clear_rows(y, 2);
    big_center(y, s);
}

/* ---------------------------------------------------------------- background */
static unsigned char band_tile(unsigned char row) {
    /* the top half is band 1 (the backdrop colour, so text sits on it cleanly); below, 8 bands glow
     * towards the bottom; the last row of a band is a dither into the next one */
    unsigned char b, nb;
    if (row < 15) return T_bg1;
    b = ((row - 15) * 8) / 15; nb = ((row - 14) * 8) / 15;
    if (nb != b && b < 7) return T_bgd1 + b;
    return T_bg1 + b;
}

void clear_rows(unsigned char y, unsigned char h) {
    unsigned char i, tile;
    for (; h; --h, ++y) {
        tile = band_tile(y);
        k_vaddr(0x4000 + ((unsigned)y << 7));
        for (i = 0; i < 64; ++i) { PPU_VDATA = tile; PPU_VDATA = PAL_BG; }
    }
}

static void text(unsigned char x, unsigned char y, const char *s, unsigned char pal) {
    /* font tiles are transparent around the letters: the backdrop colour shows there, so the text sits
     * on a row painted in the backdrop colour (band 0 colour = backdrop) */
    k_print(x, y, s, pal);
}

/* living gradient: each band drifts in red and blue with a slow wave */
static const unsigned char wave[32] = { 0,0,1,1,2,2,3,3,3,4,4,4,4,4,3,3,3,2,2,1,1,0,0,0,0,0,0,0,0,0,0,0 };
static const unsigned char base_r[9] = { 0, 1, 2, 2, 3, 4, 5, 6, 7 };
static const unsigned char base_g[9] = { 0, 0, 0, 0, 1, 1, 1, 2, 2 };
static const unsigned char base_b[9] = { 0, 3, 4, 5, 6, 7, 8, 9, 10 };
static unsigned char bright = 16;      /* 0..16: fades the whole screen */

static unsigned scale(unsigned char r, unsigned char g, unsigned char b) {
    if (r > 15) r = 15;
    if (g > 15) g = 15;
    if (b > 15) b = 15;
    r = (r * bright) >> 4; g = (g * bright) >> 4; b = (b * bright) >> 4;
    return (unsigned)r << 8 | g << 4 | b;
}

static void animate_bg(void) {
    unsigned char k, ph;
    unsigned col;
    k_vaddr(0x5000 + PAL_BG * 32 + 2);
    for (k = 1; k <= 8; ++k) {
        ph = (unsigned char)(t >> 2) + k * 3;
        col = scale(base_r[k] + wave[ph & 31], base_g[k], base_b[k] + wave[(ph + 11) & 31]);
        PPU_VDATA = (unsigned char)col; PPU_VDATA = col >> 8;
    }
    /* backdrop (palette 0 colour 0) = the top band, so text rows blend in */
    ph = (unsigned char)(t >> 2) + 3;
    col = scale(base_r[1] + wave[ph & 31], base_g[1], base_b[1] + wave[(ph + 11) & 31]);
    k_vaddr(0x5000);
    PPU_VDATA = (unsigned char)col; PPU_VDATA = col >> 8;
}

/* selected card frame: pulses cyan -> white */
static const unsigned pulse[16] = { 0x0CF, 0x2DF, 0x4DF, 0x6EF, 0x8EF, 0xAFF, 0xCFF, 0xEFF,
                                    0xEFF, 0xCFF, 0xAFF, 0x8EF, 0x6EF, 0x4DF, 0x2DF, 0x0CF };
static void animate_sel(void) {
    unsigned c = pulse[(t >> 1) & 15];
    k_vaddr(0x5000 + PAL_SEL * 32 + 2);
    PPU_VDATA = (unsigned char)c; PPU_VDATA = c >> 8;          /* colour 1 border */
    k_vaddr(0x5000 + PAL_SEL * 32 + 8);
    PPU_VDATA = 0xFF; PPU_VDATA = 0x0F;                          /* colour 4 highlight: white */
}

#define title_palette big_palette

/* ---------------------------------------------------------------- sprites: drifting motes */
#define MOTES 10
static unsigned char mote_x[MOTES], mote_y[MOTES], mote_s[MOTES];
static void motes_init(void) {
    unsigned char i;
    for (i = 0; i < MOTES; ++i) { mote_x[i] = k_rand(); mote_y[i] = k_rand() % 240; mote_s[i] = 1 + (k_rand() & 3); }
}
static void motes_step(unsigned char first) {
    unsigned char i, w;
    for (i = 0; i < MOTES; ++i) {
        if ((t & 3) < mote_s[i] || mote_s[i] == 4) {
            if (mote_y[i] == 0) { mote_y[i] = 239; mote_x[i] = k_rand(); } else --mote_y[i];
        }
        w = wave[(mote_y[i] >> 2) + i & 31];
        k_sprite(first + i, (int)mote_x[i] + (mote_x[i] & 1 ? 32 : 0) + w - 8, mote_y[i] - 8, S_mote, PAL_SPR);
    }
}


/* ---------------------------------------------------------------- the strip of cards */
static void draw_card(unsigned char g, unsigned char selected) {
    unsigned char x = g * CARD_W + 1, y = STRIP_ROW, i, j, p = selected ? PAL_SEL : PAL_FRAME;
    unsigned char ic = icon[g], t0 = icon_tile[ic];
    unsigned a;
    for (j = 0; j < 6; ++j) {
        a = 0x4000 + ((unsigned)(y + j) << 7) + ((unsigned)x << 1);
        k_vaddr(a);
        for (i = 0; i < 6; ++i) {
            unsigned char tile, pal = p;
            if (j == 0) tile = i == 0 ? T_frTL : i == 5 ? T_frTR : T_frT;
            else if (j == 5) tile = i == 0 ? T_frBL : i == 5 ? T_frBR : T_frB;
            else if (i == 0) tile = T_frL;
            else if (i == 5) tile = T_frR;
            else {
                /* 32x32 icon = 4 bigs (TL TR BL BR), each 4 tiles (TL TR BL BR) */
                unsigned char ix = i - 1, iy = j - 1;
                tile = t0 + ((iy >> 1) * 2 + (ix >> 1)) * 4 + (iy & 1) * 2 + (ix & 1);
                pal = PAL_ICON + (g & 7);
            }
            PPU_VDATA = tile; PPU_VDATA = pal;
        }
    }
}

static void draw_strip(void) {
    unsigned char g;
    clear_rows(STRIP_ROW - 1, 8);
    for (g = 0; g < ngames; ++g) {
        k_palette(PAL_ICON + (g & 7), icon_pal[icon[g]], 16);
        draw_card(g, g == sel);
    }
}

/* ---------------------------------------------------------------- the chosen game's text */
static void draw_info(void) {
    unsigned char g = sel, ic = icon[g], x;
    char pl[12];
    title_fade = 0;
    title_palette(0);
    big_title(5, names[g]);
    clear_rows(8, 6);
    /* "1 OYUNCU" / "2-4 OYUNCU" . genre */
    x = 0;
    pl[x++] = '0' + pmin[g];
    if (pmax[g] != pmin[g]) { pl[x++] = '-'; pl[x++] = '0' + pmax[g]; }
    pl[x] = 0;
    x = (40 - (str_len(pl) + 10 + str_len(genre[ic]))) / 2;
    text(x, 8, pl, 1);
    text(x + str_len(pl), 8, " OYUNCU", 1);
    text(x + str_len(pl) + 8, 8, "/", 0);
    text(x + str_len(pl) + 10, 8, genre[ic], 2);
    text((40 - str_len(desc1[ic])) / 2, 10, desc1[ic], 0);
    text((40 - str_len(desc2[ic])) / 2, 11, desc2[ic], 0);
}

static void draw_frame_static(void) {
    unsigned char i;
    for (i = 0; i < 30; ++i) clear_rows(i, 1);
    text(2, 1, "RETRO KONSOL", 2);
    text(25, 1, (CART_SRC & 1) ? "FLASH" : "BRAM", 0);
    k_print_num(31, 1, ngames, 1, 1);
    text(33, 1, "OYUN", 0);
    text(6, 14, "OKLAR SEC", 0);
    text(18, 14, "ENTER/SPACE OYNA", 1);
}

/* ---------------------------------------------------------------- splash (cold start) */
#define SPARKS 16
static const signed char ring_x[16] = { 0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49 };
static const signed char ring_y[16] = { -127, -117, -90, -49, 0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117 };

static unsigned char skip(void) { return (pad_new[0] | pad_new[1]) & (BTN_A | BTN_START); }

static void splash(void) {
    unsigned char f, i, r;
    static const char sub[] = "6502 . NEXYS 4 DDR . FPGA";
    unsigned col;
    bright = 0;
    k_palette(0, (const unsigned *)"\0\0", 1);
    for (i = 0; i < 30; ++i) clear_rows(i, 1);
    animate_bg();
    /* sparks fly in from a ring to the centre (the opening jingle climbs with them) */
    ses_muzik(mus_acilis);
    for (f = 0; f < 64; ++f) {
        k_wait_frame(); ++t;
        if (skip()) return;
        r = 64 - f;                     /* radius shrinks (scaled by 2 below) */
        for (i = 0; i < SPARKS; ++i) {
            unsigned char k = (i + (f >> 2)) & 15;
            k_sprite(i, 152 + ((ring_x[k] * (int)r) >> 6) + (i & 1 ? 0 : 3),
                         88 + ((ring_y[k] * (int)r) >> 7), (f + i) & 4 ? S_spark : S_dot, PAL_SPR);
        }
    }
    k_hide_sprites(0);
    /* flash (the jingle's crash), then the title fades in over the backdrop gradient */
    big_title(10, "RETRO KONSOL");
    for (f = 0; f < 40; ++f) {
        k_wait_frame(); ++t;
        if (skip()) return;
        if (f < 8) {
            col = 0xFFF - (unsigned)f * 0x222;
            k_vaddr(0x5000); PPU_VDATA = (unsigned char)col; PPU_VDATA = col >> 8;
        } else {
            if (bright < 16) ++bright;
            animate_bg();
        }
        title_palette(f < 32 ? f / 4 : 8);
    }
    /* subtitle typed out */
    for (i = 0; sub[i]; ++i) {
        char one[2];
        one[0] = sub[i]; one[1] = 0;
        k_print(7 + i, 14, one, 2);
        if (sub[i] != ' ') ses_efekt(sfx_yazi);
        k_wait_frame(); k_wait_frame(); t += 2;
        animate_bg();
        if (skip()) return;
    }
    for (f = 0; f < 50; ++f) {
        k_wait_frame(); ++t;
        animate_bg();
        if (skip()) return;
    }
    /* fade out */
    for (f = 0; f < 16; ++f) {
        k_wait_frame(); ++t;
        --bright;
        animate_bg();
        title_palette(8 - (f >> 1));
    }
}

/* ---------------------------------------------------------------- start a game */
static unsigned char launch_idx;
static void launch(void) {
    unsigned char f;
    WARM[0] = 'K'; WARM[1] = 'W'; WARM[2] = sel;
    ses_dur();
    ses_efekt(sfx_basla);
    for (f = 0; f < 20; ++f) {
        k_wait_frame(); ++t;
        if (bright) --bright;
        if (bright) --bright;
        animate_bg();
        title_palette(bright >> 1);
    }
    k_hide_sprites(0);
    k_wait_frame();
    for (f = 0; f < 30; ++f) clear_rows(f, 1);
    k_palette(0, (const unsigned *)"\0\0", 1);
    k_print(15, 14, "YUKLENIYOR", 0);
    k_palette(0, (const unsigned *)"\0\0\xff\x0f", 2);
    k_wait_frame();
    launch_idx = index[sel];
    __asm__("lda %v", launch_idx);
    __asm__("jmp $FFF0");
}

/* ---------------------------------------------------------------- main */
static void choose(signed char d) {
    unsigned char old = sel;
    if (d < 0) sel = sel ? sel - 1 : ngames - 1;
    else sel = sel + 1 < ngames ? sel + 1 : 0;
    if (sel == old) return;
    draw_card(old, 0);
    draw_card(sel, 1);
    draw_info();
    ses_efekt(sfx_secim);
}

void main(void) {
    unsigned char p, i, warm;
    k_init();
    k_esc_menu = 0;                /* Esc here would only restart the launcher */
    PPU_CTRL = 0;
    big_init(slots, 10, PAL_TITLE);
    k_vcopy(TILE_BASE * 32, tiles_data, sizeof(tiles_data));
    k_vcopy(0x2000, sprites_data, sizeof(sprites_data));
    k_palette(PAL_BG, pal_bg, 16);
    k_palette(PAL_FRAME, pal_frame, 16);
    k_palette(PAL_SEL, pal_frame, 16);
    k_palette(PAL_SPR, pal_spr, 16);
    title_palette(0);
    PPU_CTRL = CTRL_NMI | CTRL_BG | CTRL_SPR;
    k_seed(0xC0DE);
    read_directory();
    warm = WARM[0] == 'K' && WARM[1] == 'W';
    sel = warm && WARM[2] < ngames ? WARM[2] : 0;
    WARM[0] = 0;                   /* a reset button press from here on is a cold start again */
    SYS_TEST = 0xD0;               /* simulation: the launcher runs */
    if (!ngames) {
        k_print(10, 14, "KARTUSTA OYUN YOK", 3);
        for (;;) k_wait_frame();
    }
    if (!warm) splash();
    ses_muzik(mus_menu);
    bright = 0;
    draw_frame_static();
    draw_strip();
    draw_info();
    cam = (int)sel * (CARD_W * 8) + 32 - 160;
    k_split(SPLIT_LINE, (unsigned)cam & 511, 0);
    motes_init();
    SYS_TEST = 0xD1;               /* home screen drawn */
    for (;;) {
        k_wait_frame(); ++t;
        if (bright < 16) ++bright;
        animate_bg();
        animate_sel();
        if (title_fade < 8) title_palette(++title_fade);
        /* the strip glides to the selection */
        cam_goal = (int)sel * (CARD_W * 8) + 32 - 160;
        if (cam != cam_goal) {
            int d = (cam_goal - cam) / 4;
            if (d == 0) d = cam_goal > cam ? 1 : -1;
            cam += d;
        }
        k_split(SPLIT_LINE, (unsigned)cam & 511, 0);
        motes_step(0);
        /* two sparkles orbit the chosen card (centre: screen x 160 once the strip has settled, y 160) */
        i = (t >> 2) & 15;
        k_sprite(MOTES, 152 + (cam_goal - cam) + ((ring_x[i] * 30) >> 7), 152 + ((ring_y[i] * 30) >> 7),
                 (t & 8) ? S_spark : S_dot, PAL_SPR);
        k_sprite(MOTES + 1, 152 + (cam_goal - cam) - ((ring_x[i] * 30) >> 7), 152 - ((ring_y[i] * 30) >> 7),
                 (t & 8) ? S_dot : S_spark, PAL_SPR);
        p = pad_new[0] | pad_new[1];
        if (p & (BTN_LEFT | BTN_UP)) choose(-1);
        if (p & (BTN_RIGHT | BTN_DOWN)) choose(1);
        if (p & (BTN_A | BTN_START)) launch();
    }
}
