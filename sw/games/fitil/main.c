/* Fitil Arenasi: 2-4 players drop fuse bombs in an arena, blow up crates, collect power-ups and
 * try to be the last one standing. First to 3 rounds wins.
 * Pads 1-4 (keyboard: players 1 and 2). A or B = bomb (keyboard: Shift / Ctrl).
 * Up/Down and Start on the setup screen pick the player count and start. */
#include "konsol.h"
#include "bigtext.h"
#include "art.h"
#include "ses.h"
#include "muzik.h"

#define AW 15
#define AH 11
#define OX 5            /* arena origin in tiles */
#define OY 4
#define MAXB 16
#define FUSE 150
#define FLAME 24

#define PAL_ARENA 4
#define PAL_FLAME 5
#define PAL_BOMB  6
#define PAL_PWR   7
#define PAL_P1    8
#define PAL_BIG   13

enum { FLOOR, WALL, CRATE, BOMB, FIRE, P_BOMB, P_FIRE, P_SPEED };
/* flame shapes: centre, horizontal, vertical, ends (open towards the bomb) */
enum { F_C, F_H, F_V, F_L, F_R, F_U, F_D };

unsigned char grid[AH][AW];
unsigned char flame[AH][AW];
unsigned char fshape[AH][AW];
unsigned char nplayers, alive_count, round_no;
unsigned char px[4], py[4], palive[4], pmax[4], prange[4], pspeed[4], pwins[4];
signed char pdx[4], pdy[4];
unsigned char pstep[4];            /* pixels moved towards the next cell */
unsigned char pface[4];            /* 0 down, 1 up, 2 right, 3 left */
unsigned char pbombs[4];
unsigned char pdead_t[4];          /* frames since the player was caught (ghost rises) */
unsigned char bx[MAXB], by[MAXB], bt[MAXB], bowner[MAXB], brange[MAXB], bframe[MAXB];
unsigned char winner;
unsigned char anim;                
static const unsigned char start_x[4] = { 1, 13, 1, 13 };
static const unsigned char start_y[4] = { 1, 9, 9, 1 };
static const unsigned char flame_tile[7] = { T_fc, T_fh, T_fv, T_fl, T_fr, T_fu, T_fd };
static const unsigned backdrop_col[1] = { 0x112 };
static const unsigned char slots[16] = { 0, 4, 8, 12, 16, 20, 24, 28, 168, 172, 176, 180, 184, 188, 192, 196 };

/* ---------------------------------------------------------------- drawing */
static void put_big(unsigned char tx, unsigned char ty, unsigned char t, unsigned char pal) {
    unsigned a = 0x4000 + ((unsigned)ty << 7) + ((unsigned)tx << 1);
    k_vaddr(a); PPU_VDATA = t; PPU_VDATA = pal; PPU_VDATA = t + 1; PPU_VDATA = pal;
    k_vaddr(a + 128); PPU_VDATA = t + 2; PPU_VDATA = pal; PPU_VDATA = t + 3; PPU_VDATA = pal;
}

static unsigned char bomb_frame_at(unsigned char x, unsigned char y) {
    unsigned char b;
    for (b = 0; b < MAXB; ++b) if (bt[b] && bx[b] == x && by[b] == y) return bframe[b];
    return 0;
}

static void draw_cell(unsigned char x, unsigned char y) {
    unsigned char g = grid[y][x], t, pal = PAL_ARENA, above;
    if (flame[y][x]) { t = flame_tile[fshape[y][x]]; pal = PAL_FLAME; }
    else switch (g) {
    case WALL:
        t = (x == 0 || y == 0 || x == AW - 1 || y == AH - 1) ? T_border : T_pillar;
        break;
    case CRATE: t = T_crate; break;
    case BOMB: t = bomb_frame_at(x, y) ? T_bomb1 : T_bomb0; pal = PAL_BOMB; break;
    case P_BOMB: t = T_pbomb; pal = PAL_PWR; break;
    case P_FIRE: t = T_pfire; pal = PAL_PWR; break;
    case P_SPEED: t = T_pspeed; pal = PAL_PWR; break;
    default:
        above = y ? grid[y - 1][x] : WALL;
        t = (above == WALL || above == CRATE) ? T_floorsh : T_floor;
    }
    put_big(OX + 2 * x, OY + 2 * y, t, pal);
}

static void backdrop(void) {
    unsigned char x, y;
    for (y = 3; y < 26; ++y) {
        k_vaddr(0x4000 + ((unsigned)y << 7));
        for (x = 0; x < 40; ++x) { PPU_VDATA = T_back; PPU_VDATA = PAL_ARENA; }
    }
}

static void scores(void) {
    unsigned char i, j, x;
    k_print(1, 1, "TUR", 1);
    k_print_num(5, 1, round_no, 1, 0);
    for (i = 0; i < nplayers; ++i) {
        x = 9 + i * 8;
        k_tile(x, 1, T_head, PAL_P1 + i);
        k_print_num(x + 1, 1, i + 1, 1, 0);
        for (j = 0; j < 3; ++j) k_tile(x + 3 + j, 1, j < pwins[i] ? T_staron : T_staroff, PAL_PWR);
    }
}

/* flames flicker, bomb sparks and power-up badges glow: palette cycling */
static void cycle_palettes(void) {
    static const unsigned fire[4][5] = {
        { 0xFFF, 0xFF8, 0xFD3, 0xF92, 0xE42 }, { 0xFFE, 0xFE6, 0xFC2, 0xF81, 0xD32 },
        { 0xFFF, 0xFF9, 0xFE4, 0xFA3, 0xE52 }, { 0xFFD, 0xFD5, 0xFB2, 0xF71, 0xC31 } };
    static const unsigned spark[3] = { 0xFF6, 0xF92, 0xE32 };
    static const unsigned glow[4] = { 0xA6E, 0xB8F, 0xA6E, 0x95D };
    unsigned char f = anim >> 2, i;
    unsigned c;
    if ((anim & 3) == 0) {
        k_vaddr(0x5000 + (PAL_FLAME << 5) + 10);
        for (i = 0; i < 5; ++i) { c = fire[f & 3][i]; PPU_VDATA = (unsigned char)c; PPU_VDATA = c >> 8; }
        k_vaddr(0x5000 + (PAL_BOMB << 5) + 20);
        for (i = 0; i < 3; ++i) { c = spark[(i + f) % 3]; PPU_VDATA = (unsigned char)c; PPU_VDATA = c >> 8; }
    }
    if ((anim & 7) == 0) {
        c = glow[(anim >> 3) & 3];
        k_vaddr(0x5000 + (PAL_PWR << 5) + 12);
        PPU_VDATA = (unsigned char)c; PPU_VDATA = c >> 8;
    }
}

/* ---------------------------------------------------------------- rounds */
static void new_round(void) {
    unsigned char x, y, i, r;
    for (y = 0; y < AH; ++y)
        for (x = 0; x < AW; ++x) {
            flame[y][x] = 0;
            if (x == 0 || y == 0 || x == AW - 1 || y == AH - 1 || (!(x & 1) && !(y & 1))) grid[y][x] = WALL;
            else {
                r = k_rand();
                grid[y][x] = r < 150 ? CRATE : FLOOR;
            }
        }
    for (i = 0; i < 4; ++i) {
        /* keep the corners free so nobody is trapped at the start */
        x = start_x[i]; y = start_y[i];
        grid[y][x] = FLOOR;
        grid[y][x == 1 ? 2 : x - 1] = FLOOR;
        grid[y][x == 1 ? 3 : x - 2] = FLOOR;
        grid[y == 1 ? 2 : y - 1][x] = FLOOR;
        grid[y == 1 ? 3 : y - 2][x] = FLOOR;
    }
    for (i = 0; i < MAXB; ++i) bt[i] = 0;
    for (y = 0; y < AH; ++y) for (x = 0; x < AW; ++x) draw_cell(x, y);
    alive_count = nplayers;
    for (i = 0; i < 4; ++i) {
        px[i] = start_x[i]; py[i] = start_y[i];
        palive[i] = i < nplayers; pmax[i] = 1; prange[i] = 1; pspeed[i] = 1; pbombs[i] = 0;
        pstep[i] = 0; pdx[i] = 0; pdy[i] = 0; pface[i] = 0; pdead_t[i] = 0;
    }
    ++round_no;
    scores();
}

/* inside the border and free to walk on (anything outside the inner area is a wall, whatever grid says) */
static unsigned char walkable(unsigned char x, unsigned char y) {
    unsigned char g;
    if (x < 1 || y < 1 || x > AW - 2 || y > AH - 2) return 0;
    g = grid[y][x];
    return g == FLOOR || g >= P_BOMB;
}

static void place_bomb(unsigned char i) {
    unsigned char b;
    if (pbombs[i] >= pmax[i] || grid[py[i]][px[i]] == BOMB) return;
    for (b = 0; b < MAXB; ++b)
        if (!bt[b]) {
            bx[b] = px[i]; by[b] = py[i]; bt[b] = FUSE; bowner[b] = i; brange[b] = prange[i]; bframe[b] = 0;
            grid[py[i]][px[i]] = BOMB;
            ++pbombs[i];
            draw_cell(px[i], py[i]);
            ses_efekt(sfx_fitil);
            return;
        }
}

static void burn(unsigned char x, unsigned char y, unsigned char shape) {
    unsigned char b, old;
    if (flame[y][x]) {                          /* two flames meet: keep the line, or a cross */
        old = fshape[y][x];
        if (old != shape) {
            if ((old == F_H || old == F_L || old == F_R) && (shape == F_H || shape == F_L || shape == F_R)) shape = F_H;
            else if ((old == F_V || old == F_U || old == F_D) && (shape == F_V || shape == F_U || shape == F_D)) shape = F_V;
            else shape = F_C;
        }
    }
    flame[y][x] = FLAME;
    fshape[y][x] = shape;
    if (grid[y][x] == BOMB)                     /* chain reaction: that bomb goes off next frame */
        for (b = 0; b < MAXB; ++b) if (bt[b] && bx[b] == x && by[b] == y) bt[b] = 1;
    draw_cell(x, y);
}

static void explode(unsigned char b) {
    unsigned char x = bx[b], y = by[b], r, d, mid, end, lx, ly, last;
    static const signed char dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
    static const unsigned char ends[4] = { F_R, F_L, F_D, F_U };
    signed char cx, cy;
    bt[b] = 0;
    if (pbombs[bowner[b]]) --pbombs[bowner[b]];
    grid[y][x] = FLOOR;
    burn(x, y, F_C);
    for (d = 0; d < 4; ++d) {
        cx = x; cy = y;
        mid = d < 2 ? F_H : F_V;
        end = ends[d];
        last = 0;
        for (r = 0; r < brange[b]; ++r) {
            cx += dxs[d]; cy += dys[d];
            if (cx < 1 || cy < 1 || cx > AW - 2 || cy > AH - 2 || grid[cy][cx] == WALL) break;
            if (grid[cy][cx] == CRATE) {
                r = k_rand();
                grid[cy][cx] = r < 40 ? P_BOMB : r < 80 ? P_FIRE : r < 100 ? P_SPEED : FLOOR;
                burn(cx, cy, end);
                if (cy < AH - 1 && !flame[cy + 1][cx] && grid[cy + 1][cx] == FLOOR) draw_cell(cx, cy + 1);
                last = 0;
                break;
            }
            if (grid[cy][cx] >= P_BOMB) grid[cy][cx] = FLOOR;   /* fire destroys power-ups */
            burn(cx, cy, mid);
            lx = cx; ly = cy; last = 1;
        }
        if (last && fshape[ly][lx] == mid) { fshape[ly][lx] = end; draw_cell(lx, ly); }
    }
    ses_efekt(sfx_patlama);
}

static void players(void) {
    unsigned char i, p, nx, ny, g, cx, cy;
    for (i = 0; i < nplayers; ++i) {
        if (!palive[i]) continue;
        p = pad[i];
        if (pad_new[i] & (BTN_A | BTN_B)) place_bomb(i);
        if (pstep[i] == 0) {
            pdx[i] = pdy[i] = 0;
            if (p & BTN_UP) { pdy[i] = -1; pface[i] = 1; }
            else if (p & BTN_DOWN) { pdy[i] = 1; pface[i] = 0; }
            else if (p & BTN_LEFT) { pdx[i] = -1; pface[i] = 3; }
            else if (p & BTN_RIGHT) { pdx[i] = 1; pface[i] = 2; }
            if (pdx[i] || pdy[i]) {
                nx = px[i] + pdx[i]; ny = py[i] + pdy[i];
                if (walkable(nx, ny)) pstep[i] = 1;
                else pdx[i] = pdy[i] = 0;
            }
        } else {
            pstep[i] += pspeed[i] + 1;
            if (pstep[i] >= 16) {
                pstep[i] = 0;
                px[i] += pdx[i]; py[i] += pdy[i];
                g = grid[py[i]][px[i]];
                if (g >= P_BOMB) {
                    if (g == P_BOMB && pmax[i] < 6) ++pmax[i];
                    if (g == P_FIRE && prange[i] < 6) ++prange[i];
                    if (g == P_SPEED && pspeed[i] < 3) ++pspeed[i];
                    grid[py[i]][px[i]] = FLOOR;
                    draw_cell(px[i], py[i]);
                    ses_efekt(sfx_guc);
                }
            }
        }
        /* the flame catches the player in the cell they mostly stand in */
        cx = px[i]; cy = py[i];
        if (pstep[i] >= 8) { cx += pdx[i]; cy += pdy[i]; }
        if (flame[cy][cx]) {
            palive[i] = 0; --alive_count; pdead_t[i] = 0; ses_efekt(sfx_elendi);
        }
    }
}

static void bombs_and_flames(void) {
    unsigned char b, x, y, f;
    for (b = 0; b < MAXB; ++b)
        if (bt[b]) {
            if (--bt[b] == 0) { bt[b] = 1; explode(b); continue; }
            /* the bomb breathes, faster when the fuse is short */
            f = (bt[b] < 50 ? bt[b] >> 2 : bt[b] >> 3) & 1;
            if (f != bframe[b]) {
                bframe[b] = f;
                if (!flame[by[b]][bx[b]]) draw_cell(bx[b], by[b]);
            }
        }
    for (y = 1; y < AH - 1; ++y)
        for (x = 1; x < AW - 1; ++x)
            if (flame[y][x] && --flame[y][x] == 0) draw_cell(x, y);
}

static void draw_players(void) {
    static const unsigned char pat[4] = { S_down0, S_up0, S_side0, S_side0 };
    unsigned char i, a, pt;
    int sx, sy;
    for (i = 0; i < 4; ++i) {
        if (i >= nplayers) { k_sprite(i, -32, 0, 0, 0); continue; }
        sx = (OX + 2 * px[i]) * 8 + pdx[i] * pstep[i];
        sy = (OY + 2 * py[i]) * 8 + pdy[i] * pstep[i] - 3;
        a = PAL_P1 + i;
        if (palive[i]) {
            pt = pat[pface[i]] + (pstep[i] ? (pstep[i] >> 3) & 1 : 0);
            if (pface[i] == 3) a |= ATTR_FLIPX;
        } else {
            pt = S_ghost;
            if (pdead_t[i] < 255) ++pdead_t[i];
            sy -= pdead_t[i] >> 2;            /* the ghost floats up and away */
            if (pdead_t[i] > 120) sy = -64;
            if ((anim >> 3) & 1) ++sy;
        }
        k_sprite(i, sx, sy, pt, a);
    }
}

static void frame(void) {
    k_wait_frame();
    ++anim;
    cycle_palettes();
}

/* ---------------------------------------------------------------- screens */
static void setup(void) {
    unsigned char i;
    k_hide_sprites(0);
    k_clear_map();
    backdrop();
    k_fill_tiles(0, 3, 40, 7, ' ', 0);
    big_reset();
    big_center(3, "FITIL");
    big_center(6, "ARENASI");
    k_fill_tiles(2, 16, 36, 13, ' ', 0);
    k_print(3, 17, "OYUNCU SAYISI:", 2);
    k_print(3, 19, "          YURU       BOMBA", 1);
    k_print(3, 20, "1. OYUNCU W A S D    SOL SHIFT", 0);
    k_print(3, 21, "2. OYUNCU OKLAR      SAG SHIFT", 0);
    k_print(3, 22, "3-4.      NES/SNES KOLU (A)", 0);
    k_print(3, 24, "YUKARI/ASAGI: OYUNCU SAYISI", 0);
    k_print(3, 25, "SPACE/ENTER: BASLA  ESC: MENU", 0);
    k_print(3, 27, "SANDIKLARI PATLAT, GUC TOPLA,", 0);
    k_print(3, 28, "SON AYAKTA KALAN TURU ALIR. 3 TUR!", 0);
    if (nplayers < 2) nplayers = 2;
    ses_muzik(mus_tur);
    for (;;) {
        k_print(18, 17, "<", nplayers > 2 ? 3 : 0);
        k_print_num(19, 17, nplayers, 1, 1);
        k_print(20, 17, ">", nplayers < 4 ? 3 : 0);
        /* the players line up and wave (walking on the spot) */
        for (i = 0; i < 4; ++i) {
            if (i < nplayers) k_sprite(i, 104 + i * 32, 92 + ((anim >> 4) + i & 1), (anim >> 4) & 1 ? S_down1 : S_down0, PAL_P1 + i);
            else k_sprite(i, 104 + i * 32, 92, S_ghost, PAL_P1 + i);
        }
        frame();
        if ((pad_new[0] | pad_new[1]) & (BTN_UP | BTN_RIGHT) && nplayers < 4) { ++nplayers; ses_efekt(sfx_secim); }
        if ((pad_new[0] | pad_new[1]) & (BTN_DOWN | BTN_LEFT) && nplayers > 2) { --nplayers; ses_efekt(sfx_secim); }
        if ((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A)) break;
    }
    k_hide_sprites(0);
}

static void message(const char *s, unsigned char pal) {
    k_fill_tiles(0, 27, 40, 1, ' ', 0);
    k_print((40 - 18) / 2, 27, s, pal);
}

static void match_won(void) {
    unsigned char t;
    static char line[] = "OYUNCU 1";
    line[7] = '1' + winner;
    k_hide_sprites(0);
    k_clear_map();
    backdrop();
    k_fill_tiles(0, 4, 40, 14, ' ', 0);
    big_reset();
    big_center(6, line);
    big_center(10, "KAZANDI");
    ses_muzik(mus_sampiyon);
    for (t = 0; t < 240; ++t) {
        k_sprite(0, 152, 120 + ((t >> 3) & 1 ? -4 : 0), (t >> 4) & 1 ? S_down1 : S_down0, PAL_P1 + winner);
        frame();
        if (t > 60 && ((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A))) break;
    }
}

void main(void) {
    unsigned char i, t;
    k_init();
    big_init(slots, 16, PAL_BIG);
    k_palette(0, backdrop_col, 1);
    k_palette(PAL_ARENA, pal_arena, 16);
    k_palette(PAL_FLAME, pal_flame, 16);
    k_palette(PAL_BOMB, pal_bomb, 16);
    k_palette(PAL_PWR, pal_pwr, 16);
    k_palette(8, pal_p1, 16); k_palette(9, pal_p2, 16); k_palette(10, pal_p3, 16); k_palette(11, pal_p4, 16);
    k_vcopy(TILE_BASE * 32, tiles_data, sizeof(tiles_data));
    k_vcopy(0x2000, sprites_data, sizeof(sprites_data));
    k_palette(PAL_BIG, pal_big, 16);
    for (;;) {
        setup();
        k_seed(0x5151 + frame_count);
        k_clear_map();
        backdrop();
        k_fill_tiles(0, 26, 40, 4, ' ', 0);
        k_print(15, 29, "ESC: MENU", 0);
        for (i = 0; i < 4; ++i) pwins[i] = 0;
        round_no = 0;
        winner = 255;
        while (winner == 255) {
            new_round();
            ses_muzik(mus_tur);
            while (alive_count > 1) {
                frame();
                if (((pad[0] | pad[1]) & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) k_menu();
                players();
                bombs_and_flames();
                draw_players();
            }
            for (t = 0; t < 60; ++t) { frame(); bombs_and_flames(); draw_players(); }
            for (i = 0; i < nplayers; ++i)
                if (palive[i]) {
                    ++pwins[i];
                    message(" . OYUNCU TURU ALDI", 1);
                    k_print_num(11, 27, i + 1, 1, 1);
                    ses_muzik(mus_tur_sonu);
                    if (pwins[i] == 3) winner = i;
                }
            if (!alive_count) { message("     BERABERE!", 1); ses_dur(); }
            scores();
            for (t = 0; t < 90; ++t) { frame(); draw_players(); }
            message("", 0);
        }
        match_won();
    }
}
