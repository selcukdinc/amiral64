/* Koridor 64: a first-person shooter in the spirit of the 90s corridor games, on the 6502.
 * One ray per 8-pixel tile column (40 rays), walls drawn with tiles: for each texture column there is a
 * full tile and 7 tiles where the wall starts k pixels down; the lower half of the view uses the same
 * tiles flipped, with palettes whose colour 1 is the floor. The finished view (22 rows) is copied to the
 * tile map at the start of vblank (blit.s), ahead of the beam, so there is no tearing.
 * Enemies and items are sprites in three sizes, hidden column by column behind nearer walls.
 * Controls: Up/Down walk, Left/Right turn, Ctrl (B) + Left/Right sidestep, Shift (A) fire,
 * walking into a door opens it, Space/Enter (Start) pause, Esc menu. */
#include <cc65.h>
#include "konsol.h"
#include "bigtext.h"
#include "art.h"
#include "tables.h"

#define COLS 40
#define VIEW_Y 2                /* first tile row of the view */
#define VROWS 22
#define HALF 88                 /* half the view height in pixels */
#define CENTER_Y (VIEW_Y * 8 + HALF)
#define WALL_T 96               /* wall tiles made at start: 96 + w*32 + u*8 + k */

#define PAL_W 4                 /* wall palettes 4..11: shade s -> 4 + 2s (ceiling half), 5 + 2s (floor half) */
#define PAL_EN 12
#define PAL_GUN 13
#define PAL_ITEM 14
#define PAL_BIG 15

/* 64-pixel demons: the 32-pixel frames doubled at start, after the sprites of art.h */
#define S_ew64_0 SPRITE_COUNT
#define S_ea64_0 (SPRITE_COUNT + 16)

#define MAXE 24
enum { E_DEMON, E_MED, E_AMMO, E_EXIT };
enum { S_GONE, S_IDLE, S_CHASE, S_DEAD };

unsigned char map[1024];        /* (y << 5) | x: 0 floor, 1 stone, 2 tech, 3 brick, 4 door */
unsigned char rowbuf[VROWS * 80];
unsigned px, py;                /* player position, 8.8 cells */
unsigned ang;                   /* 0..1023 */
unsigned zbuf[COLS];

unsigned char ne;
unsigned ex[MAXE], ey[MAXE];
unsigned char etype[MAXE], estate[MAXE], ehp[MAXE], etimer[MAXE], eatk[MAXE], epain[MAXE];
unsigned char hp, ammo, kills, nkill, level, fire_t, hurt_t, spark_t, bob, moving, dead, won;
static unsigned char hud_hp = 255, hud_ammo = 255, hud_kills = 255;
unsigned char ticks_run;        /* for the tests: logic ticks so far (wraps) */
unsigned char frames_drawn;

void blit_view(void);

static const unsigned char slots[14] = { 0, 4, 8, 12, 16, 20, 24, 28, 232, 236, 240, 244, 248, 252 };

/* ---------------------------------------------------------------- levels (20 x 20) */
/* # stone  T tech  W brick  D door  . floor  P player (facing east)  E demon  H medkit  A ammo  X exit */
static const char level1[20][21] = {
    "####################",
    "#P..#......#.......#",
    "#...#..E...D...H...#",
    "#...#......#.......#",
    "#.......####TTTT.TTT",
    "#...#......T.......T",
    "##D##..A...T...E...T",
    "#...#......D.......T",
    "#.E.#......T...E...T",
    "#...########TTTDTTTT",
    "#H..W..........#...#",
    "#...W...E......#.X.#",
    "#...WWWDWWW....D...#",
    "#...W.....W....#...#",
    "#.A.W..E..W....#####",
    "#...W.....W..E.....#",
    "#...WWWWWWW........#",
    "#.......A.....H....#",
    "#..E...............#",
    "####################",
};
static const char level2[20][21] = {
    "TTTTTTTTTTTTTTTTTTTT",
    "T.....T......T.....T",
    "T.P...D...E..D..E..T",
    "T.....T......T.....T",
    "TTDTTTT..A...TTTDTTT",
    "W.....T......T.....W",
    "W..E..TTTTDTTT..H..W",
    "W.....W......W.....W",
    "W..A..D...E..D..E..W",
    "W.....W......W.....W",
    "WWWDWWW..##..WWWDWWW",
    "#.....#..##..#.....#",
    "#..E..#......#..A..#",
    "#.....D..E...D.....#",
    "#.H...#......#...E.#",
    "###D######D#####D###",
    "#.....#.....E#.....#",
    "#..E..D..H...D..X..#",
    "#.....#..E...#.....#",
    "####################",
};

static void add_entity(unsigned char t, unsigned char x, unsigned char y) {
    if (ne == MAXE) return;
    ex[ne] = ((unsigned)x << 8) | 0x80; ey[ne] = ((unsigned)y << 8) | 0x80;
    etype[ne] = t; estate[ne] = S_IDLE; ehp[ne] = 3; etimer[ne] = 0; eatk[ne] = 0; epain[ne] = 0;
    if (t == E_DEMON) ++nkill;
    ++ne;
}

static void load_level(void) {
    const char (*L)[21] = level == 1 ? level1 : level2;
    unsigned char x, y, c, v;
    ne = 0; nkill = 0; kills = 0;
    for (x = 0; x < 255; ++x) { map[x] = 1; map[x + 256] = 1; map[x + 512] = 1; map[x + 768] = 1; }
    map[255] = map[511] = map[767] = map[1023] = 1;
    for (y = 0; y < 20; ++y)
        for (x = 0; x < 20; ++x) {
            c = L[y][x];
            v = 0;
            if (c == '#') v = 1; else if (c == 'T') v = 2; else if (c == 'W') v = 3; else if (c == 'D') v = 4;
            map[((unsigned)y << 5) | x] = v;
            if (c == 'P') { px = ((unsigned)x << 8) | 0x80; py = ((unsigned)y << 8) | 0x80; ang = 0; }
            else if (c == 'E') add_entity(E_DEMON, x, y);
            else if (c == 'H') add_entity(E_MED, x, y);
            else if (c == 'A') add_entity(E_AMMO, x, y);
            else if (c == 'X') add_entity(E_EXIT, x, y);
        }
}

/* ---------------------------------------------------------------- rays: render.s */
void render_view(void);
#if T_fill != 224
#error "render.s expects T_fill = 224"
#endif

/* ---------------------------------------------------------------- sprites */
#define OAM_GUN 0
#define OAM_FIRST 7
static unsigned char oam_n;
static unsigned char vis_i[MAXE], vis_n;
static unsigned vis_f[MAXE];
static int vis_x[MAXE];
static unsigned char vis_h[MAXE];

static void spr(int x, int y, unsigned char pat, unsigned char attr) {
    if (oam_n < 64) k_sprite(oam_n++, x, y, pat, attr);
}

/* a size x size sprite block (size 16, 32 or 64) centred on sx, standing on the floor at foot_y;
 * columns behind a nearer wall are left out */
static void draw_block(int sx, int foot_y, unsigned f, unsigned char size, unsigned char pat0, unsigned char attr) {
    unsigned char n = size >> 4, i, j, col;
    int x0 = sx - (size >> 1), y0 = foot_y - size, x;
    for (i = 0; i < n; ++i) {
        x = x0 + (i << 4) + 8;
        if (x < 0 || x >= 320) continue;
        col = (unsigned char)(x >> 3);
        if (zbuf[col] < f) continue;
        for (j = 0; j < n; ++j) {
            x = y0 + (j << 4);
            if (x > VIEW_Y * 8 - 12 && x < VIEW_Y * 8 + 2 * HALF - 4)   /* not over the HUD */
                spr(x0 + (i << 4), x, pat0 + j * n + i, attr);
        }
    }
}

static void place_sprites(void) {
    unsigned char i, j, k, t, size, pat;
    int dx, dy, s, cs, sn, f;
    unsigned uf;
    unsigned char h;
    cs = COS(ang); sn = SIN(ang);
    vis_n = 0;
    for (i = 0; i < ne; ++i) {
        if (estate[i] == S_GONE) continue;
        dx = (int)(ex[i] - px); dy = (int)(ey[i] - py);
        if (dx > 0x0A00 || dx < -0x0A00 || dy > 0x0A00 || dy < -0x0A00) continue;
        /* forward and side distances in 16-bit math: positions in 1/16 cell, sin/cos halved */
        dx >>= 4; dy >>= 4;
        f = ((dx * (cs >> 1)) >> 3) + ((dy * (sn >> 1)) >> 3);
        if (f < 48 || f > 0x0A00) continue;
        s = ((dy * (cs >> 1)) >> 3) - ((dx * (sn >> 1)) >> 3);
        uf = (unsigned)f;
        h = height_t[uf >> 3];
        /* screen x = s * 246 / f = s * h * 1.23 (height_t is 200 px per cell) */
        dx = (((s >> 5) * h) >> 3) * 5 >> 2;
        if (dx < -200 || dx > 200) continue;
        /* insert sorted by distance, nearest first */
        for (j = vis_n; j && vis_f[j - 1] > uf; --j) {
            vis_f[j] = vis_f[j - 1]; vis_i[j] = vis_i[j - 1]; vis_x[j] = vis_x[j - 1]; vis_h[j] = vis_h[j - 1];
        }
        vis_f[j] = uf; vis_i[j] = i; vis_x[j] = 160 + dx; vis_h[j] = h; ++vis_n;
    }
    oam_n = OAM_FIRST;
    if (spark_t) {           /* the hit spark in the middle of the view */
        spr(152, CENTER_Y - 12, S_spark, PAL_ITEM);
    }
    for (k = 0; k < vis_n; ++k) {
        i = vis_i[k]; h = vis_h[k]; t = etype[i];
        if (t == E_DEMON) {
            if (estate[i] == S_DEAD) {
                draw_block(vis_x[k] - 8, CENTER_Y + (h >> 1), vis_f[k], 16, S_dead_0, PAL_EN);
                draw_block(vis_x[k] + 8, CENTER_Y + (h >> 1), vis_f[k], 16, S_dead_1, PAL_EN);
                continue;
            }
            size = h >= 100 ? 64 : h >= 48 ? 32 : 16;
            if (eatk[i]) pat = size == 64 ? S_ea64_0 : size == 32 ? S_ea32_0 : S_ea16_0;
            else pat = size == 64 ? S_ew64_0 : size == 32 ? S_ew32_0 : S_ew16_0;
            draw_block(vis_x[k], CENTER_Y + (h >> 1) - ((estate[i] == S_CHASE && (etimer[i] & 8)) ? 2 : 0),
                       vis_f[k], size, pat, PAL_EN | (epain[i] && (epain[i] & 2) ? ATTR_FLIPX : 0));
        } else {
            pat = t == E_MED ? S_medkit : t == E_AMMO ? S_ammo : S_portal;
            draw_block(vis_x[k], CENTER_Y + (h >> 1), vis_f[k], 16, pat, PAL_ITEM);
        }
    }
    k_hide_sprites(oam_n);
    /* the weapon, bobbing while walking */
    {
        unsigned char gp = fire_t > 8 ? S_gunfire_0 : S_gunidle_0;
        int gy = 160 + (moving ? ((bob & 16) ? (bob & 15) >> 2 : 4 - ((bob & 15) >> 2)) : 0) + (fire_t > 8 ? 2 : 0);
        for (j = 0; j < 2; ++j)
            for (i = 0; i < 3; ++i)
                k_sprite(OAM_GUN + j * 3 + i, 136 + (i << 4), gy + (j << 4), gp + j * 3 + i, PAL_GUN);
    }
}

/* ---------------------------------------------------------------- game logic */
static unsigned char solid_at(unsigned x, unsigned y) {
    unsigned char *m = &map[((y >> 8) << 5) | (x >> 8)];
    if (*m == 4) {                      /* a door opens when something walks into it */
        *m = 0;
        k_sfx(SFX_TICK);
        return 1;
    }
    return *m;
}

#define R 0x50                          /* body radius, 8.8 */
static unsigned char blocked(unsigned x, unsigned y) {
    return solid_at(x - R, y - R) | solid_at(x + R, y - R) | solid_at(x - R, y + R) | solid_at(x + R, y + R);
}

static unsigned adist(unsigned x0, unsigned y0, unsigned x1, unsigned y1) {
    unsigned dx = x0 > x1 ? x0 - x1 : x1 - x0, dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return dx > dy ? dx + (dy >> 1) : dy + (dx >> 1);
}

/* demons are solid too: no walking into (or through) one */
static unsigned char demon_at(unsigned x, unsigned y) {
    unsigned char i;
    for (i = 0; i < ne; ++i)
        if (etype[i] == E_DEMON && (estate[i] == S_IDLE || estate[i] == S_CHASE)
            && adist(x, y, ex[i], ey[i]) < 0xA0 && adist(x, y, ex[i], ey[i]) < adist(px, py, ex[i], ey[i]))
            return 1;
    return 0;
}

static void move_player(int mvx, int mvy) {
    if (mvx && !blocked(px + mvx, py) && !demon_at(px + mvx, py)) px += mvx;
    if (mvy && !blocked(px, py + mvy) && !demon_at(px, py + mvy)) py += mvy;
}

static void hud_message(const char *s, unsigned char pal) {
    k_fill_tiles(0, 0, 40, 2, ' ', 0);
    k_print((40 - big_len(s)) >> 1, 0, s, pal);
}

static void hurt(unsigned char d) {
    if (dead) return;
    hp = hp > d ? hp - d : 0;
    hurt_t = 12;
    k_sfx(SFX_LOSE);
    if (!hp) dead = 1;
}

static void shoot(void) {
    unsigned char k, i, best = 255;
    int half;
    if (!ammo) { k_sfx(SFX_TICK); hud_message("MERMI BITTI!", 3); return; }
    --ammo;
    fire_t = 14;
    k_sfx(SFX_SHOOT);
    for (k = 0; k < vis_n; ++k) {           /* nearest first: the first demon under the crosshair */
        i = vis_i[k];
        if (etype[i] != E_DEMON || estate[i] == S_DEAD) continue;
        half = vis_h[k] >= 100 ? 24 : vis_h[k] >= 48 ? 13 : 7;
        if (vis_x[k] - half <= 160 && vis_x[k] + half >= 160 && zbuf[19] >= vis_f[k]) { best = i; break; }
    }
    if (best == 255) return;
    spark_t = 6;
    estate[best] = S_CHASE;
    if (ehp[best] > 1 + (k_rand() & 1)) { ehp[best] -= 1 + (k_rand() & 1); epain[best] = 10; }
    else {
        ehp[best] = 0; estate[best] = S_DEAD; eatk[best] = 0; ++kills;
        k_sfx(SFX_BOOM);
    }
}

static void enemies(void) {
    unsigned char i;
    unsigned d, nx, ny;
    for (i = 0; i < ne; ++i) {
        if (estate[i] == S_GONE || estate[i] == S_DEAD) continue;
        d = adist(ex[i], ey[i], px, py);
        if (etype[i] != E_DEMON) {                       /* items */
            if (d < 0x90) {
                if (etype[i] == E_MED && hp < 100) { hp = hp > 75 ? 100 : hp + 25; estate[i] = S_GONE; k_sfx(SFX_PICKUP); }
                else if (etype[i] == E_AMMO) { ammo = ammo > 87 ? 99 : ammo + 12; estate[i] = S_GONE; k_sfx(SFX_PICKUP); }
                else if (etype[i] == E_EXIT) won = 1;
            }
            continue;
        }
        if (epain[i]) --epain[i];
        if (eatk[i]) --eatk[i];
        ++etimer[i];
        if (estate[i] == S_IDLE) {
            if (d < 0x0600) estate[i] = S_CHASE;
            continue;
        }
        if (d > 0x01A0) {                                /* walk towards the player */
            if (epain[i]) continue;
            nx = ex[i]; ny = ey[i];
            if (px > ex[i] + 0x20) nx += 9; else if (px + 0x20 < ex[i]) nx -= 9;
            if (py > ey[i] + 0x20) ny += 9; else if (py + 0x20 < ey[i]) ny -= 9;
            if (!map[((ey[i] >> 8) << 5) | ((nx + (nx > ex[i] ? 0x50 : -0x50)) >> 8)]) ex[i] = nx;
            if (!map[(((ny + (ny > ey[i] ? 0x50 : -0x50)) >> 8) << 5) | (ex[i] >> 8)]) ey[i] = ny;
        } else if (!(etimer[i] % 50)) {                  /* claws */
            eatk[i] = 16;
            hurt(6 + (k_rand() & 7));
        }
    }
}

static void tick(void) {
    unsigned char p = pad[0] | pad[1];
    int cs, sn;
    ++ticks_run;
    if (fire_t) --fire_t;
    if (spark_t) --spark_t;
    if (hurt_t) --hurt_t;
    moving = 0;
    if (dead || won) return;
    cs = COS(ang); sn = SIN(ang);
    if (p & BTN_B) {                                     /* Ctrl: sidestep */
        if (p & BTN_LEFT) { move_player((sn * 20) >> 8, -(cs * 20) >> 8); moving = 1; }
        if (p & BTN_RIGHT) { move_player(-(sn * 20) >> 8, (cs * 20) >> 8); moving = 1; }
    } else {
        if (p & BTN_LEFT) ang = (ang - 10) & 1023;
        if (p & BTN_RIGHT) ang = (ang + 10) & 1023;
    }
    if (p & BTN_UP) { move_player((cs * 22) >> 8, (sn * 22) >> 8); moving = 1; }
    if (p & BTN_DOWN) { move_player(-(cs * 16) >> 8, -(sn * 16) >> 8); moving = 1; }
    if (moving) ++bob;
    if ((p & BTN_A) && !fire_t) shoot();
    enemies();
}

/* ---------------------------------------------------------------- screen */
static void flash_palettes(unsigned char red) {
    static const unsigned ceil_c[2] = { 0x223, 0x611 }, floor_c[2] = { 0x322, 0x611 };
    unsigned char s;
    unsigned c;
    for (s = 0; s < 4; ++s) {
        k_vaddr(0x5000 + ((PAL_W + 2 * s) << 5) + 2);
        c = ceil_c[red]; PPU_VDATA = (unsigned char)c; PPU_VDATA = c >> 8;
        k_vaddr(0x5000 + ((PAL_W + 2 * s + 1) << 5) + 2);
        c = floor_c[red]; PPU_VDATA = (unsigned char)c; PPU_VDATA = c >> 8;
    }
}

static void hud_static(void) {
    unsigned char x;
    k_fill_tiles(0, 24, 40, 6, ' ', 0);
    k_vaddr(0x4000 + (24 << 7));
    for (x = 0; x < 40; ++x) { PPU_VDATA = T_hudbar; PPU_VDATA = PAL_ITEM; }
    k_tile(2, 26, T_hudcross, PAL_ITEM); k_print(4, 26, "CAN", 1);
    k_tile(14, 26, T_hudammo, PAL_ITEM); k_print(16, 26, "MERMI", 1);
    k_tile(27, 26, T_hudkill, PAL_ITEM); k_print(29, 26, "LES", 1);
    k_print(2, 28, "BOLUM", 2); k_print_num(8, 28, level, 1, 0);
    k_print(12, 28, "SHIFT ATES  CTRL+OK YANA", 0);
    hud_hp = hud_ammo = hud_kills = 255;
}

static void hud_update(void) {
    if (hp != hud_hp) { hud_hp = hp; k_print_num(8, 26, hp, 3, hp > 30 ? 0 : 3); }
    if (ammo != hud_ammo) { hud_ammo = ammo; k_print_num(22, 26, ammo, 2, ammo ? 0 : 3); }
    if (kills != hud_kills) {
        hud_kills = kills;
        k_print_num(33, 26, kills, 2, 0); k_print(35, 26, "/", 0); k_print_num(36, 26, nkill, 2, 0);
    }
}

static void make_wall_tiles(void) {
    unsigned char t, k, y, i;
    const unsigned char *src;
    for (t = 0; t < 16; ++t) {
        src = tiles_data + (t << 5);
        k_vaddr((unsigned)(WALL_T + (t << 3)) << 5);
        for (k = 0; k < 8; ++k)
            for (y = 0; y < 8; ++y)
                for (i = 0; i < 4; ++i)
                    PPU_VDATA = y < k ? 0x11 : (y == k && k) ? 0xFF : src[(y << 2) + i];
    }
}

static void make_big_demons(void) {
    unsigned char f, bx, by, r, b, v;
    const unsigned char *src;
    for (f = 0; f < 2; ++f) {
        k_vaddr(0x2000 + ((unsigned)(f ? S_ea64_0 : S_ew64_0) << 7));
        for (by = 0; by < 4; ++by)
            for (bx = 0; bx < 4; ++bx) {
                src = sprites_data + ((unsigned)((f ? S_ea32_0 : S_ew32_0) + (by >> 1) * 2 + (bx >> 1)) << 7)
                      + ((by & 1) << 6) + ((bx & 1) << 2);
                for (r = 0; r < 16; ++r)
                    for (b = 0; b < 8; ++b) {
                        v = src[((r >> 1) << 3) + (b >> 1)];
                        v = (b & 1) ? (v & 15) : (v >> 4);
                        PPU_VDATA = (v << 4) | v;
                    }
            }
    }
}

static void wait_start(void) {
    do k_wait_frame(); while (!((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A)));
}

static void title(void) {
    k_hide_sprites(0);
    k_clear_map();
    big_reset();
    big_center(3, "KORIDOR 64");
    k_print(6, 7, "USSUN KORIDORLARINI GOLGELER", 0);
    k_print(6, 8, "BASTI. HEPSINI TEMIZLE, YESIL", 0);
    k_print(6, 9, "KAPIDAN (PORTAL) CIK.", 0);
    k_print(5, 12, "OKLAR / W S     YURU", 2);
    k_print(5, 13, "OKLAR / A D     DON", 2);
    k_print(5, 14, "CTRL + SOL/SAG  YANA KAY", 2);
    k_print(5, 15, "SHIFT           ATES ET", 2);
    k_print(5, 16, "KAPIYA YURU     KAPI ACILIR", 2);
    k_print(5, 17, "SPACE           DURAKLAT", 2);
    k_print(5, 18, "ESC             MENU", 2);
    k_print(5, 21, "KIRMIZI ARTI: CAN   SARI: MERMI", 0);
    k_print(9, 25, "SPACE / ENTER: BASLA", 1);
    k_sprite(0, 24, 12, S_ew32_0, PAL_EN); k_sprite(1, 40, 12, S_ew32_1, PAL_EN);
    k_sprite(2, 24, 28, S_ew32_2, PAL_EN); k_sprite(3, 40, 28, S_ew32_3, PAL_EN);
    k_sprite(4, 264, 12, S_ea32_0, PAL_EN); k_sprite(5, 280, 12, S_ea32_1, PAL_EN);
    k_sprite(6, 264, 28, S_ea32_2, PAL_EN); k_sprite(7, 280, 28, S_ea32_3, PAL_EN);
    k_hide_sprites(8);
    wait_start();
}

static void banner(const char *a, const char *b) {
    k_hide_sprites(0);
    k_fill_tiles(0, 0, 40, 30, ' ', 0);
    big_reset();
    big_center(9, a);
    k_print((40 - big_len(b)) >> 1, 14, b, 0);
    k_print(9, 20, "SPACE / ENTER: DEVAM", 1);
}

/* plays one level; returns 1 when the exit is reached, 0 when the player dies */
static unsigned char play(void) {
    unsigned char last, n, paused = 0;
    load_level();
    k_hide_sprites(0);
    dead = 0; won = 0; fire_t = 0; spark_t = 0; hurt_t = 0;
    k_clear_map();
    hud_static();
    hud_message(level == 1 ? "BOLUM 1: DEPO" : "BOLUM 2: REAKTOR", 1);
    last = frame_count;
    for (;;) {
        n = frame_count - last;
        last = frame_count;
        if (n > 4) n = 4;
        if (!paused) while (n--) tick();
        render_view();
        place_sprites();
        k_wait_frame();
        blit_view();
        flash_palettes(hurt_t ? 1 : 0);
        hud_update();
        ++frames_drawn;
        if ((pad_new[0] | pad_new[1]) & BTN_START) {
            paused = !paused;
            hud_message(paused ? "DURAKLADI" : "", 1);
        }
        if (dead) { flash_palettes(1); k_sfx(SFX_LOSE); return 0; }
        if (won) { k_sfx(SFX_WIN); return 1; }
    }
}

void main(void) {
    k_init();
    big_init(slots, 14, PAL_BIG);
    k_palette(PAL_W + 0, pal_w0t, 16); k_palette(PAL_W + 1, pal_w0b, 16);
    k_palette(PAL_W + 2, pal_w1t, 16); k_palette(PAL_W + 3, pal_w1b, 16);
    k_palette(PAL_W + 4, pal_w2t, 16); k_palette(PAL_W + 5, pal_w2b, 16);
    k_palette(PAL_W + 6, pal_w3t, 16); k_palette(PAL_W + 7, pal_w3b, 16);
    k_palette(PAL_EN, pal_enemy, 16); k_palette(PAL_GUN, pal_weapon, 16); k_palette(PAL_ITEM, pal_items, 16);
    k_vcopy((unsigned)T_fill << 5, tiles_data + 16 * 32, sizeof(tiles_data) - 16 * 32);
    make_wall_tiles();
    k_vcopy(0x2000, sprites_data, sizeof(sprites_data));
    make_big_demons();
    k_palette(PAL_BIG, pal_big, 16);
    for (;;) {
        title();
        level = 1; hp = 100; ammo = 30;
        for (;;) {
            if (play()) {
                if (level == 2) {
                    banner("ZAFER!", "USSU GOLGELERDEN TEMIZLEDIN.");
                    wait_start();
                    break;
                }
                banner("BOLUM BITTI", "SIRADAKI KAT: REAKTOR");
                wait_start();
                ++level;
                if (hp < 50) hp = 50;
                if (ammo < 20) ammo = 20;
            } else {
                unsigned char t;
                for (t = 0; t < 60; ++t) k_wait_frame();
                banner("OLDUN", "BOLUM BASTAN BASLAYACAK.");
                wait_start();
                hp = 100; if (ammo < 30) ammo = 30;
            }
        }
    }
}
