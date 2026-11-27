/* Kristal Ocak: dig through the mine, collect the crystals, reach the exit before time runs out.
 * Rocks and crystals fall and roll; a rock wobbles once before it falls, and a falling rock is deadly.
 * A rock that falls on a cracked wall breaks it. Dynamite is picked up, placed with B + direction on an
 * empty cell, burns for three steps and blows up the 3x3 round it: dirt, rocks and cracked walls go,
 * steel walls and crystals stay, other dynamite goes off too, and so does the miner.
 * The miner walks smoothly from cell to cell; dust flies where rocks land, crystals sparkle, the exit
 * door slides open. Three mines with their own colours.
 * Screens: title (with a demo when left alone), mine select, level card, play, pause menu, death,
 * level end (time left into points, 1-3 stars), game over. Unlocked levels, stars and the best score are
 * kept in RAM at $02E8 while the board is on.
 * Pad: directions move (an early press is kept for the next step), B + direction digs or collects without
 * moving, A held restarts the level (costs a life), Start pauses, Select + Start goes back to the menu. */
#include "konsol.h"
#include "bigtext.h"
#include "art.h"

#define W 32
#define H 13
#define FIELD_ROW 2          /* tile row of the first field row (rows 0-1: the HUD) */
#define TICK 6               /* frames per game step */

enum { EMPTY, DIRT, WALL, ROCK, GEM, GEM2, EXIT, HERO, CRACK, DYN, LIT, GAS, BOOM, RAIL, CART, OIL };
#define FALLING 0x40
#define WOBBLE  0x20
#define KIND(c) ((c) & 0x0F)

#define PAL_T    4           /* field: terrain, crystals and the door, specials */
#define PAL_C    5
#define PAL_S    6
#define PAL_HERO 8
#define PAL_ICON 13
#define PAL_FX   14
#define PAL_BIG  15

/* sprites: 0 the miner, 1-4 sparkles and dust, 8-15 title crystals */
#define SPR_HERO 0
#define SPR_FX   1
#define NFX      4

static const char *const levels[3][H] = {
  { "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    "WP.....O...*.....O.....*....O..W",
    "W.O.O....O...O.*....O....*.....W",
    "W....*..WWWWW......O.....WWW...W",
    "W..O.....*.....O.....+.........W",
    "W.....O.....O.....*......O..*..W",
    "W.*......WWWWWWW.....O.........W",
    "W....O.....*........*.....O....W",
    "W..*....O.......O.......*......W",
    "W.......*...O.....WWWWW....O.*.W",
    "W.O..*......*...O......*......EW",
    "W.....O...O.....*...O.....O....W",
    "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW" },
  { "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    "W*.O.O.O.O.O.O.O.O.O.O.O.O.O..*W",
    "W.............................PW",
    "W.WWWWWWWWWWWWWW..WWWWWWWWWWWW.W",
    "W.*..O..*..O..*....*..O..*..O..W",
    "W..O..O..O..O.....O..O..O..O...W",
    "W.......*.......+*.......*.....W",
    "WWWWWWWWWWWW...OO...WWWWWWWWWWWW",
    "W*..O....O..*......*..O....O..*W",
    "W..O..*....O........O....*..O..W",
    "W.......O.......E.......O......W",
    "W*...O.....*...O...*.....O....*W",
    "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW" },
  { "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    "WP..OOOO.....*.*.*.....OOOO....W",
    "W...OOOO..W.........W..OOOO..*.W",
    "W.*.......W...OOO...W.......*..W",
    "WWWWWW..WWW...*+*...WWW..WWWWWWW",
    "W.....*...O....O....O...*......W",
    "W..O.....O*O..O*O..O*O.....O...W",
    "W..*..O...O....O....O...O..*...W",
    "W.WWWWWWWWWWW.....WWWWWWWWWWWW.W",
    "W..*...O...*...O...*...O...*...W",
    "W.O.*.O.*.O.*.O.*.O.*.O.*.O.*.OW",
    "W.......................E......W",
    "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW" },
};
#define NLEVEL 3
static const unsigned char need[NLEVEL] = { 10, 14, 18 };
static const unsigned char lv_mine[NLEVEL] = { 0, 1, 2 };     /* which mine (colours, select screen row) */
static const unsigned char lv_num[NLEVEL] = { 1, 1, 1 };      /* its number in the mine */
static const unsigned char lv_time[NLEVEL] = { 150, 150, 150 };
static const char *const lv_name[NLEVEL] = { "ILK KAZI", "BUZ KOPRUSU", "KAYA YAGMURU" };
static const char *const lv_intro[NLEVEL] = {
    "KAZ, TOPLA, SALLANAN KAYADAN KAC",
    "CTRL + YON: YERINDEN KIPIRDAMADAN KAZ",
    "TAKILIRSAN SHIFT BASILI: YENIDEN",
};
static const char *const mine_name[3] = { "TOPRAK OCAK", "BUZ MAGARASI", "LAV DERINLIGI" };
static const unsigned *const pal_mine[3][3] = {
    { pal_terrain, pal_crystal, pal_special },
    { pal_terrain_buz, pal_crystal_buz, pal_special_buz },
    { pal_terrain_lav, pal_crystal_lav, pal_special_lav },
};

unsigned char map[H][W];
unsigned char hero_x, hero_y, gems, level, lives, alive, exit_open, won;
unsigned score, time_left;
unsigned char gems_total, died_here, snap;              /* crystals the level has; a life lost in it; B dig */
unsigned char dyns, blasted;                            /* dynamite carried; the miner was in a blast */
#define NFUSE 6
unsigned char fuse_x[NFUSE], fuse_y[NFUSE], fuse_t[NFUSE];   /* lit dynamite: steps left (0 = free slot) */
static unsigned cam, cam_target;
static unsigned char exit_x, exit_y, exit_stage, tick, frames60, anim;
/* the miner on screen: where he came from, which way he faces, what he does */
static unsigned char from_x, from_y, face, act;       /* face: 0 down 1 up 2 right 3 left; act: 0 walk 1 dig 2 push */
static unsigned char moving;
static unsigned char shake;
static unsigned char fx_t[NFX], fx_kind[NFX];
static int fx_x[NFX], fx_y[NFX];
static unsigned crystal_cols[4];
static const unsigned char slide[TICK] = { 0, 3, 5, 8, 11, 13 };   /* pixels moved within a step */

/* ---------------------------------------------------------------- drawing */
static void put_big(unsigned char x, unsigned char y, unsigned char t, unsigned char pal) {
    unsigned a = 0x4000 + ((unsigned)(FIELD_ROW + 2 * y) << 7) + ((unsigned)x << 2);
    k_vaddr(a); PPU_VDATA = t; PPU_VDATA = pal; PPU_VDATA = t + 1; PPU_VDATA = pal;
    k_vaddr(a + 128); PPU_VDATA = t + 2; PPU_VDATA = pal; PPU_VDATA = t + 3; PPU_VDATA = pal;
}

static unsigned char open_cell(unsigned char k) { return k == EMPTY || k == HERO || k == BOOM; }

static void draw_cell(unsigned char x, unsigned char y) {
    unsigned char c = map[y][x], k = KIND(c), t, p = PAL_T, h = (x * 7 + y * 13 + (x >> 2)) & 3;
    switch (k) {
    case DIRT:
        t = (y && open_cell(KIND(map[y - 1][x]))) ? (h & 1 ? T_dirtlit1 : T_dirtlit0) : T_dirt0 + h * 4;
        break;
    case WALL: t = T_wall; break;
    case ROCK: t = c & WOBBLE ? T_rockw : T_rock; break;
    case GEM: t = T_gem; p = PAL_C; break;
    case GEM2: t = T_gem2; p = PAL_C; break;
    case EXIT: t = T_exit0 + exit_stage * 4; p = PAL_C; break;
    case CRACK: t = c & WOBBLE ? T_crack2 : T_crack; break;     /* hit: it breaks next step */
    case DYN: t = T_dyn; p = PAL_S; break;
    case LIT: t = anim & 8 ? T_dynlit1 : T_dynlit0; p = PAL_S; break;
    case GAS: t = T_gas; p = PAL_S; break;
    case BOOM: t = c & WOBBLE ? T_boom1 : T_boom0; p = PAL_S; break;
    case RAIL: t = T_rail; p = PAL_S; break;
    case CART: t = T_cart; p = PAL_S; break;
    case OIL: t = T_oil; p = PAL_S; break;
    default: t = h & 1 ? T_back1 : T_back0;       /* empty, and the miner's cell (he is a sprite) */
    }
    put_big(x, y, t, p);
}

/* a cell changed: it and the dirt below it (lit from above or not) */
static void redraw(unsigned char x, unsigned char y) {
    draw_cell(x, y);
    if (y + 1 < H && KIND(map[y + 1][x]) == DIRT) draw_cell(x, y + 1);
}

static void hud_static(void) {
    k_fill_tiles(0, 0, 64, 2, ' ', 0);
    k_tile(1, 0, T_igem, PAL_ICON);
    k_print(5, 0, "/", 0);
    k_tile(10, 0, T_iclock, PAL_ICON);
    k_tile(16, 0, T_iheart, PAL_ICON);
    k_print(21, 0, "PUAN", 1);
    k_tile(33, 0, T_idyn, PAL_ICON);
    k_print(1, 1, "OCAK  -", 2);
}

static void hud(void) {
    k_print_num(3, 0, gems, 2, gems >= need[level] ? 1 : 0);
    k_print_num(6, 0, need[level], 2, 0);
    k_print_num(12, 0, time_left, 3, time_left <= 20 && (frames60 & 32) ? 3 : 0);
    k_print_num(18, 0, lives, 1, 0);
    k_print_num(26, 0, score, 5, 0);
    k_print_num(6, 1, lv_mine[level] + 1, 1, 2);
    k_print_num(8, 1, lv_num[level], 1, 2);
    k_print_num(35, 0, dyns, 1, dyns ? 1 : 0);
    k_seg_number(score);
}

/* ---------------------------------------------------------------- effects: sparkles, dust, shake */
static void fx_add(unsigned char kind, unsigned char x, unsigned char y) {
    unsigned char i;
    for (i = 0; i < NFX; ++i)
        if (!fx_t[i]) {
            fx_kind[i] = kind; fx_t[i] = kind ? 12 : 10;
            fx_x[i] = x * 16; fx_y[i] = FIELD_ROW * 8 + y * 16;
            return;
        }
}

static void fx_draw(void) {
    unsigned char i, pat;
    for (i = 0; i < NFX; ++i) {
        if (!fx_t[i]) { k_sprite(SPR_FX + i, -16, 0, 0, 0); continue; }
        --fx_t[i];
        if (fx_kind[i] == 0) pat = fx_t[i] > 3 && fx_t[i] < 8 ? S_spark1 : S_spark0;
        else pat = S_dust0 + (fx_t[i] < 4 ? 2 : fx_t[i] < 8 ? 1 : 0);
        k_sprite(SPR_FX + i, fx_x[i] - (int)cam, fx_y[i] + (fx_kind[i] ? 2 : -2), pat, PAL_FX);
    }
}

/* the crystals' shades turn round; the open door blinks */
static void cycle_colours(void) {
    unsigned char i;
    unsigned c0;
    if ((anim & 7) == 0) {
        c0 = crystal_cols[0];
        for (i = 0; i < 3; ++i) crystal_cols[i] = crystal_cols[i + 1];
        crystal_cols[3] = c0;
        k_vaddr(0x5000 + (PAL_C << 5) + 5 * 2);
        for (i = 0; i < 4; ++i) { PPU_VDATA = (unsigned char)crystal_cols[i]; PPU_VDATA = crystal_cols[i] >> 8; }
    }
    if (exit_stage == 3) {
        k_vaddr(0x5000 + (PAL_C << 5) + 15 * 2);
        c0 = anim & 16 ? 0xFE6 : 0xFFF;
        PPU_VDATA = (unsigned char)c0; PPU_VDATA = c0 >> 8;
    }
}

/* a few random cells a frame: a crystal in view sparkles now and then */
static void sparkle(void) {
    unsigned char x = (cam >> 4) + k_rand() % 20, y = k_rand() % H;
    if (x < W && (KIND(map[y][x]) == GEM || KIND(map[y][x]) == GEM2) && (k_rand() & 3) == 0) fx_add(0, x, y);
}

/* ---------------------------------------------------------------- the miner */
static void draw_hero(void) {
    int px = hero_x * 16, py = hero_y * 16;
    unsigned char pat, attr = PAL_HERO, step = (anim >> 3) & 1;
    if (moving) {
        unsigned char s = 16 - slide[tick];
        px -= ((signed char)hero_x - (signed char)from_x) * s;
        py -= ((signed char)hero_y - (signed char)from_y) * s;
    }
    if (!alive) pat = S_crushed;
    else if (won) pat = S_cheer;
    else if (face >= 2) {
        if (snap || (act == 1 && moving)) pat = tick < 3 ? S_digup : S_digdn;
        else if (act == 2 && moving) pat = S_push;
        else pat = moving && step ? S_side1 : S_side0;
        if (face == 3) attr |= ATTR_FLIPX;
    } else if (face == 1) pat = moving && step ? S_up1 : S_up0;
    else pat = moving && step ? S_down1 : S_down0;
    k_sprite(SPR_HERO, px - (int)cam, FIELD_ROW * 8 + py - 1, pat, attr);
    /* the camera follows the miner (in pixels) */
    cam_target = px > 152 ? px - 152 : 0;
    if (cam_target > 192) cam_target = 192;
}

/* ---------------------------------------------------------------- the level */
static void set_mine_palettes(unsigned char mine) {
    unsigned char i;
    k_palette(PAL_T, pal_mine[mine][0], 16);
    k_palette(PAL_C, pal_mine[mine][1], 16);
    k_palette(PAL_S, pal_mine[mine][2], 16);
    for (i = 0; i < 4; ++i) crystal_cols[i] = pal_mine[mine][1][5 + i];
}

static void load_level(void) {
    unsigned char x, y;
    char c;
    gems = 0; gems_total = 0; exit_open = 0; exit_stage = 0; alive = 1; won = 0; moving = 0; face = 0; act = 0;
    snap = 0; time_left = lv_time[level]; frames60 = 0; tick = 0; shake = 0; dyns = 0; blasted = 0;
    for (x = 0; x < NFUSE; ++x) fuse_t[x] = 0;
    for (x = 0; x < NFX; ++x) fx_t[x] = 0;
    set_mine_palettes(lv_mine[level]);
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            c = levels[level][y][x];
            switch (c) {
            case 'W': map[y][x] = WALL; break;
            case '.': map[y][x] = DIRT; break;
            case 'O': map[y][x] = ROCK; break;
            case '*': map[y][x] = GEM; ++gems_total; break;
            case '+': map[y][x] = GEM2; gems_total += 5; break;
            case 'E': map[y][x] = EXIT; exit_x = x; exit_y = y; break;
            case 'P': map[y][x] = HERO; hero_x = x; hero_y = y; break;
            case 'C': map[y][x] = CRACK; break;
            case 'D': map[y][x] = DYN; break;
            default: map[y][x] = EMPTY;
            }
        }
    from_x = hero_x; from_y = hero_y;
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) draw_cell(x, y);
    hud_static();
    hud();
    cam = hero_x > 9 ? (hero_x - 9) * 16 : 0;
    if (cam > 192) cam = 192;
}

/* ---------------------------------------------------------------- rocks and crystals */
static void landed(unsigned char x, unsigned char y) {
    k_sfx(SFX_FALL);
    fx_add(1, x, y);
    if (x * 16 + 16 > cam && x * 16 < cam + 320) shake = 6;
}

/* fall into empty cells (after one step of wobbling), roll off round things, crush the miner.
 * Bottom-up, so a falling thing is not moved twice; a thing that rolled right is skipped (x + 1). */
static unsigned char *row, ph_x, ph_c, ph_k, ph_b;
static signed char ph_y;
static void physics(void) {
    for (ph_y = H - 2; ph_y > 0; --ph_y) {
        row = map[ph_y];
        for (ph_x = 1; ph_x < W - 1; ++ph_x) {
            ph_c = row[ph_x];
            ph_k = ph_c & 0x0F;
            if (ph_k == BOOM || ph_k == CRACK) {
                /* a blast burns for two steps; a cracked wall that was hit falls apart */
                if (ph_c & WOBBLE) { row[ph_x] = EMPTY; redraw(ph_x, ph_y); if (ph_k == CRACK) fx_add(1, ph_x, ph_y); }
                else if (ph_k == BOOM) { row[ph_x] = BOOM | WOBBLE; draw_cell(ph_x, ph_y); }
                continue;
            }
            if (ph_k != ROCK && ph_k != GEM && ph_k != GEM2) continue;
            ph_b = row[ph_x + W] & 0x0F;
            if (ph_b == EMPTY) {
                if (ph_c & (FALLING | WOBBLE)) {
                    row[ph_x + W] = ph_k | FALLING;
                    row[ph_x] = EMPTY;
                    redraw(ph_x, ph_y); draw_cell(ph_x, ph_y + 1);
                } else {
                    row[ph_x] = ph_k | WOBBLE;           /* it shakes first: a moment to run */
                    draw_cell(ph_x, ph_y);
                }
            } else if (ph_b == HERO && (ph_c & FALLING)) {
                alive = 0;
            } else if (ph_b == CRACK && ph_k == ROCK && (ph_c & FALLING)) {
                row[ph_x + W] = CRACK | WOBBLE;          /* the rock breaks the wall, then falls through */
                draw_cell(ph_x, ph_y + 1);
                k_sfx(SFX_BOOM);
                landed(ph_x, ph_y);
                row[ph_x] = ROCK;
                draw_cell(ph_x, ph_y);
            } else if ((ph_b == ROCK || ph_b == GEM || ph_b == GEM2)
                       && row[ph_x - 1] == EMPTY && row[ph_x - 1 + W] == EMPTY) {
                row[ph_x - 1] = ph_k | FALLING; row[ph_x] = EMPTY;
                redraw(ph_x, ph_y); draw_cell(ph_x - 1, ph_y);
            } else if ((ph_b == ROCK || ph_b == GEM || ph_b == GEM2)
                       && row[ph_x + 1] == EMPTY && row[ph_x + 1 + W] == EMPTY) {
                row[ph_x + 1] = ph_k | FALLING; row[ph_x] = EMPTY;
                redraw(ph_x, ph_y); draw_cell(ph_x + 1, ph_y);
                ++ph_x;
            } else if (ph_c & (FALLING | WOBBLE)) {
                if (ph_c & FALLING) landed(ph_x, ph_y);
                row[ph_x] = ph_k;
                draw_cell(ph_x, ph_y);
            }
        }
    }
}

/* ---------------------------------------------------------------- dynamite */
static void light(unsigned char x, unsigned char y, unsigned char t) {
    unsigned char i;
    for (i = 0; i < NFUSE; ++i)
        if (!fuse_t[i]) {
            fuse_x[i] = x; fuse_y[i] = y; fuse_t[i] = t;
            map[y][x] = LIT; draw_cell(x, y);
            return;
        }
}

/* the 3x3 round (x, y): steel walls, the door and crystals stay; other dynamite goes off next */
static void explode(unsigned char x, unsigned char y) {
    unsigned char cx, cy, k, i;
    k_sfx(SFX_BOOM);
    shake = 10;
    fx_add(1, x, y);
    for (cy = y - 1; cy <= y + 1; ++cy)
        for (cx = x - 1; cx <= x + 1; ++cx) {
            k = KIND(map[cy][cx]);
            if (k == WALL || k == EXIT || k == GEM || k == GEM2) continue;
            if (k == HERO) { alive = 0; blasted = 1; continue; }
            if (k == DYN) { light(cx, cy, 1); continue; }
            if (k == LIT && (cx != x || cy != y)) {
                for (i = 0; i < NFUSE; ++i) if (fuse_t[i] && fuse_x[i] == cx && fuse_y[i] == cy) fuse_t[i] = 1;
                continue;
            }
            map[cy][cx] = BOOM;
            redraw(cx, cy);
        }
}

/* one game step for the fuses */
static void fuses(void) {
    unsigned char i;
    for (i = 0; i < NFUSE; ++i)
        if (fuse_t[i]) {
            if (!--fuse_t[i]) explode(fuse_x[i], fuse_y[i]);
            else draw_cell(fuse_x[i], fuse_y[i]);     /* the fuse flickers */
        }
}

/* ---------------------------------------------------------------- the miner moves */
static void collect(unsigned char x, unsigned char y, unsigned char t) {
    gems += t == GEM2 ? 5 : 1;
    score += t == GEM2 ? 50 : 10;
    k_sfx(SFX_PICKUP);
    fx_add(0, x, y);
    if (gems >= need[level] && !exit_open) { exit_open = 1; k_sfx(SFX_WIN); }
    hud();
}

static void take_dyn(void) { ++dyns; k_sfx(SFX_PICKUP); hud(); }

/* B + direction: dig the dirt or take what is next to the miner and stay put; on an empty cell, place
 * a stick of dynamite (it burns for three steps after this one) */
static void snap_at(signed char dx, signed char dy) {
    unsigned char nx = hero_x + dx, ny = hero_y + dy, t = KIND(map[ny][nx]);
    face = dy > 0 ? 0 : dy < 0 ? 1 : dx > 0 ? 2 : 3;
    act = 0; snap = 1;
    if (t == EMPTY && dyns) { --dyns; hud(); light(nx, ny, 4); k_sfx(SFX_TICK); return; }
    if (t == DIRT) k_sfx(SFX_TICK);
    else if (t == GEM || t == GEM2) collect(nx, ny, t);
    else if (t == DYN) take_dyn();
    else return;
    map[ny][nx] = EMPTY;
    redraw(nx, ny);
}

static void move_hero(signed char dx, signed char dy) {
    unsigned char nx = hero_x + dx, ny = hero_y + dy, t = KIND(map[ny][nx]);
    face = dy > 0 ? 0 : dy < 0 ? 1 : dx > 0 ? 2 : 3;
    act = 0;
    if (t != EMPTY && t != DIRT && t != ROCK && t != GEM && t != GEM2 && t != EXIT && t != DYN) return;
    if (t == ROCK) {
        /* push sideways into an empty cell */
        if (dy || KIND(map[ny][nx + dx]) != EMPTY) return;
        map[ny][nx + dx] = ROCK;
        draw_cell(nx + dx, ny);
        act = 2;
    } else if (t == GEM || t == GEM2) {
        collect(nx, ny, t);
    } else if (t == EXIT) {
        if (exit_stage < 3) return;
        won = 1;
    } else if (t == DIRT) {
        k_sfx(SFX_TICK);
        act = 1;
    } else if (t == DYN) take_dyn();
    map[hero_y][hero_x] = EMPTY;
    from_x = hero_x; from_y = hero_y;
    redraw(hero_x, hero_y);
    hero_x = nx; hero_y = ny;
    map[ny][nx] = HERO;
    redraw(nx, ny);
    moving = 1;
}

/* ---------------------------------------------------------------- save data (RAM, kept while the board is on) */
#define SAVE ((volatile unsigned char *)0x02E8)   /* 'K','O', unlocked levels, stars[9], best score (2), check */
#define SV_OPEN  2
#define SV_STARS 3
#define SV_BEST  12
#define SV_CHECK 14
static unsigned char unlocked, stars[9];
static unsigned best;

static unsigned char save_sum(void) {
    unsigned char i, c = 0x5A;
    for (i = SV_OPEN; i < SV_CHECK; ++i) c = (c << 1 | c >> 7) ^ SAVE[i];
    return c;
}

static void save_read(void) {
    unsigned char i;
    unlocked = 1; best = 0;
    for (i = 0; i < 9; ++i) stars[i] = 0;
    if (SAVE[0] != 'K' || SAVE[1] != 'O' || SAVE[SV_CHECK] != save_sum()) return;
    /* best first: cc65's optimizer reads SAVE[...] after a loop over it through a stale pointer */
    best = SAVE[SV_BEST] | (unsigned)SAVE[SV_BEST + 1] << 8;
    unlocked = SAVE[SV_OPEN];
    if (unlocked < 1 || unlocked > NLEVEL) unlocked = 1;
    for (i = 0; i < 9; ++i) stars[i] = SAVE[SV_STARS + i] > 3 ? 0 : SAVE[SV_STARS + i];
}

static void save_write(void) {
    unsigned char i;
    SAVE[0] = 'K'; SAVE[1] = 'O'; SAVE[SV_OPEN] = unlocked;
    SAVE[SV_BEST] = (unsigned char)best; SAVE[SV_BEST + 1] = best >> 8;
    for (i = 0; i < 9; ++i) SAVE[SV_STARS + i] = stars[i];
    i = save_sum();
    SAVE[SV_CHECK] = i;
}

/* ---------------------------------------------------------------- the state machine */
enum { ST_TITLE, ST_DEMO, ST_SELECT, ST_CARD, ST_PLAY, ST_PAUSE, ST_DEATH, ST_CLEAR, ST_OVER };
unsigned char state;
static unsigned st_t;                     /* frames in the current state */
static unsigned char restart;              /* the level is started again on purpose (A held, pause menu) */
unsigned char demo;                        /* the title is playing the demo */
static unsigned char sel, cursor, demo_i, demo_n, hold_a, a_armed, buf, last_won, new_best;
static unsigned char slots[18] = { 0, 4, 8, 12, 16, 20, 24, 28, 216, 220, 224, 228, 232, 236, 240, 244, 248, 252 };
static unsigned char gx[8], gy[8], gs[8];
static char line[24];

static unsigned char pressed(unsigned char m) { return (pad_new[0] | pad_new[1]) & m; }

static void screen_reset(void) {
    k_clear_map();
    k_hide_sprites(0);
    k_split(0, 0, 0);
    cam = 0; k_scroll(0, 0);
    big_reset();
}

static void center(unsigned char y, const char *s, unsigned char pal) {
    unsigned char n = 0;
    while (s[n]) ++n;
    k_print((40 - n) >> 1, y, s, pal);
}

/* "OCAK m-n" into line */
static void level_label(unsigned char lv) {
    unsigned char i;
    const char *o = "OCAK ";
    for (i = 0; o[i]; ++i) line[i] = o[i];
    line[5] = '1' + lv_mine[lv]; line[6] = '-'; line[7] = '0' + lv_num[lv]; line[8] = 0;
}

static void draw_stars(unsigned char x, unsigned char y, unsigned char n) {
    unsigned char i;
    for (i = 0; i < 3; ++i) k_tile(x + i * 2, y, i < n ? T_istar : T_istar0, PAL_ICON);
}

/* a box over the field (pause menu, level end); the cells under it are drawn again when it closes */
static unsigned char box_x;
static void box_open(void) {
    box_x = (cam >> 3) + 10;
    k_fill_tiles(box_x, 9, 20, 11, ' ', 0);
}
static void box_close(void) {
    unsigned char x, y;
    for (y = 3; y <= 9; ++y)
        for (x = box_x >> 1; x <= (box_x + 20) >> 1 && x < W; ++x) draw_cell(x, y);
}

/* the demo: the first level played by itself (directions and how many steps) */
static const unsigned char demo_path[] = {
    BTN_RIGHT, 5, BTN_DOWN, 1, BTN_RIGHT, 2, BTN_UP, 1, BTN_RIGHT, 6, BTN_DOWN, 1, BTN_RIGHT, 1, BTN_DOWN, 1,
    BTN_RIGHT, 3, BTN_DOWN, 4, BTN_RIGHT, 6, BTN_UP, 3, BTN_LEFT, 3, BTN_RIGHT, 3, BTN_UP, 2, BTN_RIGHT, 1,
    BTN_UP, 1, BTN_LEFT, 2, 0, 40,
};

static void go(unsigned char s);

/* ---- title */
static void title_enter(void) {
    unsigned char i;
    screen_reset();
    set_mine_palettes(0);
    big_center(4, "KRISTAL");
    big_center(7, "OCAK");
    k_print(7, 12, "KAZ, KRISTALLERI TOPLA,", 0);
    k_print(7, 14, "SALLANAN KAYADAN KAC VE", 0);
    k_print(7, 16, "ACILAN KAPIDAN CIK.", 0);
    k_print(7, 20, "OKLAR / WASD: KAZ, YURU", 2);
    k_print(7, 21, "CTRL + YON: YERINDE KAZ", 2);
    k_print(9, 24, "SPACE/ENTER: BASLA ", 1);
    if (best) { k_print(13, 27, "REKOR", 0); k_print_num(19, 27, best, 5, 1); }
    for (i = 0; i < 8; ++i) { gx[i] = 20 + i * 36; gy[i] = k_rand(); gs[i] = 1 + (i & 1); }
}

static void title_frame(void) {
    unsigned char i;
    big_palette(st_t < 32 ? st_t >> 2 : 8);
    for (i = 0; i < 8; ++i) {               /* crystals fall behind the title */
        gy[i] += gs[i];
        k_sprite(8 + i, gx[i], gy[i] < 232 ? gy[i] : 300, S_fgem, PAL_C | SPR_BEHIND);
    }
    k_sprite(SPR_HERO, 152, 196, (st_t >> 4) & 1 ? S_digup : S_digdn, PAL_HERO);
    if (pressed(BTN_START | BTN_A)) go(ST_SELECT);
    else if (st_t >= 600) go(ST_DEMO);
}

/* ---- mine select */
static unsigned char slot_x(unsigned char lv) { return 6 + (lv_num[lv] - 1) * 11; }
static unsigned char slot_y(unsigned char lv) { return 8 + lv_mine[lv] * 6; }

static void select_enter(void) {
    unsigned char i;
    screen_reset();
    set_mine_palettes(0);
    big_center(2, "OCAK SEC");
    for (i = 0; i < 3; ++i) k_print(3, 6 + i * 6, mine_name[i], 1);
    for (i = 0; i < NLEVEL; ++i) {
        level_label(i);
        if (i < unlocked) {
            k_print(slot_x(i), slot_y(i), line + 5, 0);
            draw_stars(slot_x(i) - 1, slot_y(i) + 1, stars[i]);
        } else k_print(slot_x(i) - 1, slot_y(i), "KILITLI", 3);
    }
    k_print(13, 25, "REKOR", 0); k_print_num(19, 25, best, 5, 1);
    k_print(3, 28, "OKLAR: SEC  SPACE: GIR  CTRL: GERI", 2);
    if (sel >= unlocked) sel = unlocked - 1;
}

static void select_frame(void) {
    big_palette(st_t < 32 ? st_t >> 2 : 8);
    if (pressed(BTN_LEFT | BTN_UP) && sel) { --sel; k_sfx(SFX_BLIP); }
    if (pressed(BTN_RIGHT | BTN_DOWN) && sel + 1 < unlocked) { ++sel; k_sfx(SFX_BLIP); }
    k_sprite(SPR_HERO, slot_x(sel) * 8 - 22, slot_y(sel) * 8 - 4, (st_t >> 4) & 1 ? S_side1 : S_side0, PAL_HERO);
    if (pressed(BTN_B)) go(ST_TITLE);
    else if (pressed(BTN_START | BTN_A)) {
        level = sel; lives = 3; score = 0; died_here = 0; new_best = 0;
        k_sfx(SFX_PICKUP);
        go(ST_CARD);
    }
}

/* ---- level card */
static void card_enter(void) {
    screen_reset();
    set_mine_palettes(lv_mine[level]);
    level_label(level);
    big_center(5, line);
    center(9, mine_name[lv_mine[level]], 2);
    center(12, lv_name[level], 1);
    k_print(11, 16, "HEDEF", 0); k_print_num(17, 16, need[level], 2, 1); k_print(20, 16, "KRISTAL", 0);
    k_print(11, 18, "SURE", 0); k_print_num(17, 18, lv_time[level], 3, 1);
    k_print(11, 20, "CAN", 0); k_print_num(17, 20, lives, 1, 1);
    center(24, lv_intro[level], 2);
}

static void card_frame(void) {
    big_palette(st_t < 32 ? st_t >> 2 : 8);
    k_sprite(SPR_HERO, 152, 200, (st_t >> 3) & 1 ? S_down1 : S_down0, PAL_HERO);
    if (st_t >= 150 || (st_t > 20 && pressed(BTN_START | BTN_A))) go(ST_PLAY);
}

/* ---- play (also the demo) */
static void start_level(void) {
    screen_reset();
    load_level();
    buf = 0; hold_a = 0; a_armed = 0;
}

static void hold_bar(void) {
    unsigned char i;
    if (!hold_a) { k_print(26, 1, "             ", 0); return; }
    k_print(26, 1, "YENIDEN", 3);
    for (i = 0; i < 5; ++i) k_tile(34 + i, 1, i < hold_a / 12 ? '#' : '-', 3);
}

static void play_frame(void) {
    unsigned char p, dir;
    if (demo) {
        if (pressed(0xFF) || !alive || won || demo_i >= sizeof demo_path) { go(ST_TITLE); return; }
        p = 0;
        if (demo_i < sizeof demo_path) p = demo_path[demo_i];
    } else {
        p = pad[0] | pad[1];
        buf |= pad_new[0] | pad_new[1];            /* a press between two steps is kept */
        if (pressed(BTN_START)) { go(ST_PAUSE); return; }
        /* A held: restart the level (a press that started on another screen does not count) */
        if (pressed(BTN_A)) a_armed = 1;
        if ((p & BTN_A) && a_armed) {
            if (++hold_a >= 60) { alive = 0; hold_a = 0; hold_bar(); restart = 1; go(ST_DEATH); return; }
            if ((hold_a & 3) == 0) hold_bar();
        } else if (hold_a) { hold_a = 0; hold_bar(); }
    }
    /* camera (the HUD stays put: raster split), with a little shake when rocks land */
    if (cam < cam_target) cam += cam_target - cam > 8 ? 4 : 2;
    else if (cam > cam_target) cam -= cam - cam_target > 8 ? 4 : 2;
    if (shake) --shake;
    k_split(14, cam + (shake & 2 ? 2 : 0), shake & 1);
    cycle_colours();
    sparkle();
    if (++frames60 == 60) {
        frames60 = 0;
        if (time_left) --time_left;
        if (!time_left) alive = 0;
        hud();
    } else if (time_left <= 20 && (frames60 & 31) == 0) hud();
    if (exit_open && exit_stage < 3 && (anim & 7) == 0) { ++exit_stage; draw_cell(exit_x, exit_y); }
    if (++tick >= TICK) {
        tick = 0;
        moving = 0; snap = 0;
        from_x = hero_x; from_y = hero_y;
        if (demo) {
            if (!--demo_n && (demo_i += 2) < sizeof demo_path) demo_n = demo_path[demo_i + 1];
        } else {
            if (!(p & 0x0F)) p |= buf & 0x0F;      /* held direction first, else the one pressed early */
            p |= buf & BTN_B;
            buf = 0;
        }
        dir = p & 0x0F;
        if (p & BTN_B) {
            if (dir & BTN_UP) snap_at(0, -1);
            else if (dir & BTN_DOWN) snap_at(0, 1);
            else if (dir & BTN_LEFT) snap_at(-1, 0);
            else if (dir & BTN_RIGHT) snap_at(1, 0);
        } else if (dir & BTN_UP) move_hero(0, -1);
        else if (dir & BTN_DOWN) move_hero(0, 1);
        else if (dir & BTN_LEFT) move_hero(-1, 0);
        else if (dir & BTN_RIGHT) move_hero(1, 0);
        fuses();
        physics();
    }
    draw_hero();
    fx_draw();
    if (demo) return;
    if (won) go(ST_CLEAR);
    else if (!alive) go(ST_DEATH);
}

static void demo_enter(void) {
    demo = 1; level = 0; lives = 3; score = 0;
    demo_i = 0; demo_n = demo_path[1];
    start_level();
    k_print(26, 1, "DEMO", 1);
}

/* ---- pause menu */
static const char *const pause_items[3] = { "DEVAM", "YENIDEN BASLA", "MENUYE DON" };
static void pause_draw(void) {
    unsigned char i;
    for (i = 0; i < 3; ++i) {
        k_print(box_x + 2, 13 + i * 2, i == cursor ? ">" : " ", 1);
        k_print(box_x + 4, 13 + i * 2, pause_items[i], i == cursor ? 1 : 0);
    }
}

static void pause_enter(void) {
    k_sprite(SPR_HERO, -16, 0, 0, 0);
    k_hide_sprites(SPR_FX);
    box_open();
    k_print(box_x + 5, 10, "DURAKLADI", 2);
    cursor = 0;
    pause_draw();
}

static void pause_frame(void) {
    if (pressed(BTN_UP) && cursor) { --cursor; pause_draw(); k_sfx(SFX_BLIP); }
    if (pressed(BTN_DOWN) && cursor < 2) { ++cursor; pause_draw(); k_sfx(SFX_BLIP); }
    if (pressed(BTN_B)) cursor = 0;
    if (!pressed(BTN_START | BTN_A | BTN_B)) return;
    if (cursor == 0) { box_close(); state = ST_PLAY; a_armed = 0; buf = 0; draw_hero(); }
    else if (cursor == 1) { box_close(); alive = 0; restart = 1; go(ST_DEATH); }
    else go(ST_SELECT);
}

/* ---- death: a dust cloud, a moment, then the level again (or the end) */
static void death_enter(void) {
    unsigned char i;
    k_sfx(SFX_LOSE);
    for (i = 0; i < NFX; ++i) fx_t[i] = 0;
    for (i = 0; i < NFX; ++i) {
        fx_add(1, hero_x, hero_y);
        fx_x[i] += i & 1 ? 6 : -6; fx_y[i] += i & 2 ? 5 : -5; fx_t[i] = 12 + i * 3;
    }
    k_fill_tiles(26, 1, 14, 1, ' ', 0);
    k_print(26, 1, restart ? "YENIDEN" : blasted ? "PATLADIN!" : time_left ? "EZILDIN!" : "SURE BITTI!", 3);
    restart = 0;
}

static void death_frame(void) {
    if (shake) --shake;
    k_split(14, cam + (shake & 2 ? 2 : 0), shake & 1);
    if (st_t < 30) {
        unsigned char i;
        for (i = 0; i < NFX; ++i) if (fx_t[i] && (st_t & 3) == 0) { fx_x[i] += i & 1 ? 1 : -1; fx_y[i] -= 1; }
    }
    draw_hero();
    fx_draw();
    if (st_t < 75) return;
    died_here = 1;
    if (--lives) { start_level(); state = ST_PLAY; }
    else go(ST_OVER);
}

/* ---- level end: the time left counts into the points, then the stars */
static unsigned char got;                 /* stars won */
static void clear_enter(void) {
    k_sfx(SFX_WIN);
    k_hide_sprites(SPR_FX);
    box_open();
    k_print(box_x + 4, 10, "BOLUM TAMAM!", 1);
    k_print(box_x + 2, 12, "SURE", 0);
    k_print(box_x + 2, 13, "PUAN", 0);
    got = 1 + (gems >= gems_total) + !died_here;
    k_print(box_x + 2, 18, gems >= gems_total ? "HEPSI TOPLANDI" : "", 2);
    k_print(box_x + 2, 19, died_here ? "" : "HIC OLMEDIN", 2);
}

static void clear_frame(void) {
    unsigned char i;
    draw_hero();
    if (time_left) {
        if (st_t > 30) {
            i = time_left > 2 ? 2 : time_left;
            time_left -= i; score += i * 2;
            if ((st_t & 3) == 0) k_sfx(SFX_TICK);
        }
        k_print_num(box_x + 8, 12, time_left, 3, 1);
        k_print_num(box_x + 8, 13, score, 5, 1);
        hud();
        st_t = st_t > 31 ? 31 : st_t;
        return;
    }
    k_print_num(box_x + 8, 12, 0, 3, 1);
    k_print_num(box_x + 8, 13, score, 5, 1);
    /* the stars one by one */
    for (i = 0; i < 3; ++i)
        if (st_t == 50 + i * 20) {
            k_tile(box_x + 7 + i * 2, 15, i < got ? T_istar : T_istar0, PAL_ICON);
            if (i < got) k_sfx(SFX_PICKUP);
        }
    if (st_t < 120 || (st_t < 400 && !pressed(BTN_START | BTN_A))) return;
    if (got > stars[level]) stars[level] = got;
    if (level + 2 > unlocked && level + 1 < NLEVEL) unlocked = level + 2;
    save_write();
    if (level + 1 < NLEVEL) { ++level; died_here = 0; go(ST_CARD); }
    else { last_won = 1; go(ST_OVER); }
}

/* ---- game over (or every level done) */
static void over_enter(void) {
    if (score > best) { best = score; new_best = 1; }
    save_write();
    screen_reset();
    set_mine_palettes(lv_mine[level]);
    big_center(6, last_won ? "TEBRIKLER" : "OYUN BITTI");
    if (last_won) center(10, "BUTUN OCAKLAR TAMAM!", 2);
    k_print(13, 14, "PUAN", 0); k_print_num(19, 14, score, 5, 1);
    k_print(13, 16, "REKOR", 0); k_print_num(19, 16, best, 5, 1);
    if (new_best) center(19, "YENI REKOR!", 3);
    center(24, "SPACE: DEVAM", 1);
}

static void over_frame(void) {
    big_palette(st_t < 32 ? st_t >> 2 : 8);
    if (new_best) k_print(14, 19, (st_t & 32) ? "           " : "YENI REKOR!", 3);
    if (st_t > 40 && pressed(BTN_START | BTN_A)) { last_won = 0; go(ST_SELECT); }
}

static void go(unsigned char s) {
    state = s; st_t = 0;
    switch (s) {
    case ST_TITLE: demo = 0; title_enter(); break;
    case ST_DEMO: demo_enter(); state = ST_PLAY; break;
    case ST_SELECT: select_enter(); break;
    case ST_CARD: card_enter(); break;
    case ST_PLAY: start_level(); break;
    case ST_PAUSE: pause_enter(); break;
    case ST_DEATH: death_enter(); break;
    case ST_CLEAR: clear_enter(); break;
    case ST_OVER: over_enter(); break;
    }
}

void main(void) {
    k_init();
    PPU_CTRL = 0;
    k_vunpack(TILE_BASE * 32, tiles_data);
    k_vunpack(0x2000, sprites_data);
    k_palette(0, pal_terrain, 1);           /* black backdrop */
    k_palette(PAL_HERO, pal_miner, 16);
    k_palette(PAL_ICON, pal_icons, 16);
    k_palette(PAL_FX, pal_fx, 16);
    k_palette(PAL_BIG, pal_big, 16);
    big_init(slots, 18, PAL_BIG);
    PPU_CTRL = CTRL_NMI | CTRL_BG | CTRL_SPR;
    k_seed(0x1234);
    save_read();
    go(ST_TITLE);
    for (;;) {
        k_wait_frame();
        ++anim; ++st_t;
        if (((pad[0] | pad[1]) & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) k_menu();
        switch (state) {
        case ST_TITLE: title_frame(); break;
        case ST_SELECT: select_frame(); break;
        case ST_CARD: card_frame(); break;
        case ST_PLAY: play_frame(); break;
        case ST_PAUSE: pause_frame(); break;
        case ST_DEATH: death_frame(); break;
        case ST_CLEAR: clear_frame(); break;
        case ST_OVER: over_frame(); break;
        }
    }
}
