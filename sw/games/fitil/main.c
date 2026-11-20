/* Fitil Arenasi: 2-4 players drop fuse bombs in an arena, blow up crates, collect power-ups and
 * try to be the last one standing. First to 3 rounds wins.
 * Pads 1-4 (keyboard: players 1 and 2). A = bomb. Start on the setup screen picks the player count. */
#include "konsol.h"
#include "art.h"

#define AW 15
#define AH 11
#define OX 5            /* arena origin in tiles */
#define OY 4
#define MAXB 16
#define FUSE 150
#define FLAME 24

enum { FLOOR, WALL, CRATE, BOMB, FIRE, P_BOMB, P_FIRE, P_SPEED };

unsigned char grid[AH][AW];
unsigned char flame[AH][AW];
unsigned char nplayers, alive_count, round_no;
unsigned char px[4], py[4], palive[4], pmax[4], prange[4], pspeed[4], pwins[4];
signed char pdx[4], pdy[4];
unsigned char pstep[4];            /* pixels moved towards the next cell */
unsigned char pbombs[4];
unsigned char bx[MAXB], by[MAXB], bt[MAXB], bowner[MAXB], brange[MAXB];
unsigned char winner;
static const unsigned char start_x[4] = { 1, 13, 1, 13 };
static const unsigned char start_y[4] = { 1, 9, 9, 1 };
static const unsigned char cell_tile[8] = { T_floor, T_wall, T_crate, T_bomb, T_flame, T_pbomb, T_pfire, T_pspeed };

static void draw_cell(unsigned char x, unsigned char y) {
    unsigned char t = cell_tile[grid[y][x]];
    unsigned a = 0x4000 + ((unsigned)(OY + 2 * y) << 7) + ((unsigned)(OX + 2 * x) << 1);
    if (flame[y][x]) t = T_flame;
    k_vaddr(a); PPU_VDATA = t; PPU_VDATA = 4; PPU_VDATA = t + 1; PPU_VDATA = 4;
    k_vaddr(a + 128); PPU_VDATA = t + 2; PPU_VDATA = 4; PPU_VDATA = t + 3; PPU_VDATA = 4;
}

static void scores(void) {
    unsigned char i;
    k_print(2, 1, "TUR", 1);
    k_print_num(6, 1, round_no, 1, 0);
    for (i = 0; i < nplayers; ++i) {
        k_print(10 + i * 7, 1, "P", 1);
        k_print_num(11 + i * 7, 1, i + 1, 1, 1);
        k_print(12 + i * 7, 1, ":", 0);
        k_print_num(13 + i * 7, 1, pwins[i], 1, 0);
    }
}

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
    for (y = 0; y < AH; ++y) for (x = 0; x < AW; ++x) draw_cell(x, y);
    for (i = 0; i < MAXB; ++i) bt[i] = 0;
    alive_count = nplayers;
    for (i = 0; i < 4; ++i) {
        px[i] = start_x[i]; py[i] = start_y[i];
        palive[i] = i < nplayers; pmax[i] = 1; prange[i] = 1; pspeed[i] = 1; pbombs[i] = 0;
        pstep[i] = 0; pdx[i] = 0; pdy[i] = 0;
    }
    ++round_no;
    scores();
}

static unsigned char walkable(unsigned char x, unsigned char y) {
    unsigned char g = grid[y][x];
    return g == FLOOR || g >= P_BOMB;
}

static void place_bomb(unsigned char i) {
    unsigned char b;
    if (pbombs[i] >= pmax[i] || grid[py[i]][px[i]] == BOMB) return;
    for (b = 0; b < MAXB; ++b)
        if (!bt[b]) {
            bx[b] = px[i]; by[b] = py[i]; bt[b] = FUSE; bowner[b] = i; brange[b] = prange[i];
            grid[py[i]][px[i]] = BOMB;
            ++pbombs[i];
            draw_cell(px[i], py[i]);
            k_sfx(SFX_TICK);
            return;
        }
}

static void burn(unsigned char x, unsigned char y);

static void explode(unsigned char b) {
    unsigned char x = bx[b], y = by[b], r, d;
    static const signed char dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
    signed char cx, cy;
    bt[b] = 0;
    if (pbombs[bowner[b]]) --pbombs[bowner[b]];
    grid[y][x] = FLOOR;
    burn(x, y);
    for (d = 0; d < 4; ++d) {
        cx = x; cy = y;
        for (r = 0; r < brange[b]; ++r) {
            cx += dxs[d]; cy += dys[d];
            if (grid[cy][cx] == WALL) break;
            if (grid[cy][cx] == CRATE) {
                r = k_rand();
                grid[cy][cx] = r < 40 ? P_BOMB : r < 80 ? P_FIRE : r < 100 ? P_SPEED : FLOOR;
                burn(cx, cy);
                break;
            }
            if (grid[cy][cx] >= P_BOMB) grid[cy][cx] = FLOOR;   /* fire destroys power-ups */
            burn(cx, cy);
        }
    }
    k_sfx(SFX_BOOM);
}

static void burn(unsigned char x, unsigned char y) {
    unsigned char b;
    flame[y][x] = FLAME;
    if (grid[y][x] == BOMB)                     /* chain reaction: that bomb goes off next frame */
        for (b = 0; b < MAXB; ++b) if (bt[b] && bx[b] == x && by[b] == y) bt[b] = 1;
    draw_cell(x, y);
}

static void players(void) {
    unsigned char i, p, nx, ny, g;
    for (i = 0; i < nplayers; ++i) {
        if (!palive[i]) continue;
        p = pad[i];
        if (pad_new[i] & BTN_A) place_bomb(i);
        if (pstep[i] == 0) {
            pdx[i] = pdy[i] = 0;
            if (p & BTN_UP) pdy[i] = -1;
            else if (p & BTN_DOWN) pdy[i] = 1;
            else if (p & BTN_LEFT) pdx[i] = -1;
            else if (p & BTN_RIGHT) pdx[i] = 1;
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
                    k_sfx(SFX_PICKUP);
                }
            }
        }
        if (flame[py[i]][px[i]] && pstep[i] < 8) {
            palive[i] = 0; --alive_count; k_sfx(SFX_LOSE);
        }
    }
}

static void bombs_and_flames(void) {
    unsigned char b, x, y;
    for (b = 0; b < MAXB; ++b)
        if (bt[b] && --bt[b] == 0) { bt[b] = 1; explode(b); }
    for (y = 1; y < AH - 1; ++y)
        for (x = 1; x < AW - 1; ++x)
            if (flame[y][x] && --flame[y][x] == 0) draw_cell(x, y);
}

static void draw_players(void) {
    unsigned char i;
    int sx, sy;
    for (i = 0; i < 4; ++i) {
        if (i >= nplayers) { k_sprite(i, -32, 0, 0, 0); continue; }
        sx = (OX + 2 * px[i]) * 8 + pdx[i] * pstep[i];
        sy = (OY + 2 * py[i]) * 8 + pdy[i] * pstep[i] - 2;
        k_sprite(i, sx, sy, palive[i] ? S_player : S_dead, 8 + i);
    }
}

static void setup(void) {
    k_hide_sprites(0);
    k_clear_map();
    k_print(13, 5, "FITIL ARENASI", 1);
    k_print(6, 9, "BOMBA BIRAK, SANDIKLARI PATLAT,", 0);
    k_print(6, 11, "GUC TOPLA, SON AYAKTA KALAN KAZANIR.", 0);
    k_print(6, 13, "UC TUR KAZANAN MACI ALIR.", 0);
    k_print(10, 17, "OYUNCU SAYISI:", 2);
    k_print(6, 21, "YUKARI/ASAGI SEC   START BASLAT", 0);
    if (nplayers < 2) nplayers = 2;
    for (;;) {
        k_print_num(25, 17, nplayers, 1, 1);
        k_wait_frame();
        if ((pad_new[0] | pad_new[1]) & BTN_UP && nplayers < 4) { ++nplayers; k_sfx(SFX_BLIP); }
        if ((pad_new[0] | pad_new[1]) & BTN_DOWN && nplayers > 2) { --nplayers; k_sfx(SFX_BLIP); }
        if ((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A)) break;
    }
}

void main(void) {
    unsigned char i, t;
    k_init();
    k_palette(4, pal_arena, 16);
    k_palette(8, pal_p1, 16); k_palette(9, pal_p2, 16); k_palette(10, pal_p3, 16); k_palette(11, pal_p4, 16);
    k_palette(0, pal_arena, 1);
    k_vcopy(TILE_BASE * 32, tiles_data, sizeof(tiles_data));
    k_vcopy(0x2000, sprites_data, sizeof(sprites_data));
    for (;;) {
        setup();
        k_seed(0x5151 + frame_count);
        k_clear_map();
        for (i = 0; i < 4; ++i) pwins[i] = 0;
        round_no = 0;
        winner = 255;
        while (winner == 255) {
            new_round();
            while (alive_count > 1) {
                k_wait_frame();
                if (((pad[0] | pad[1]) & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) k_menu();
                players();
                bombs_and_flames();
                draw_players();
            }
            for (t = 0; t < 60; ++t) { k_wait_frame(); bombs_and_flames(); draw_players(); }
            for (i = 0; i < nplayers; ++i)
                if (palive[i]) {
                    ++pwins[i];
                    k_print(14, 27, "OYUNCU   TURU ALDI", 1);
                    k_print_num(21, 27, i + 1, 1, 1);
                    k_sfx(SFX_WIN);
                    if (pwins[i] == 3) winner = i;
                }
            if (!alive_count) k_print(16, 27, "BERABERE!", 1);
            scores();
            for (t = 0; t < 90; ++t) k_wait_frame();
            k_print(14, 27, "                  ", 0);
        }
        k_print(12, 27, "OYUNCU   MACI KAZANDI!", 2);
        k_print_num(19, 27, winner + 1, 1, 2);
        for (t = 0; t < 200; ++t) k_wait_frame();
    }
}
