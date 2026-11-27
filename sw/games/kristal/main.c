/* Kristal Ocak: dig through the mine, collect the crystals, reach the exit before time runs out.
 * Rocks and crystals fall and roll; a rock wobbles once before it falls, and a falling rock is deadly.
 * The miner walks smoothly from cell to cell; dust flies where rocks land, crystals sparkle, the exit
 * door slides open. Three mines with their own colours.
 * Pad: directions move, Start pauses, Select + Start goes back to the menu. */
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
static const unsigned char need[3] = { 10, 14, 18 };
static const unsigned *const pal_mine[3][3] = {
    { pal_terrain, pal_crystal, pal_special },
    { pal_terrain_buz, pal_crystal_buz, pal_special_buz },
    { pal_terrain_lav, pal_crystal_lav, pal_special_lav },
};

unsigned char map[H][W];
unsigned char hero_x, hero_y, gems, level, lives, alive, exit_open, won;
unsigned score, time_left;
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
    case CRACK: t = T_crack; break;
    case DYN: t = T_dyn; p = PAL_S; break;
    case LIT: t = anim & 4 ? T_dynlit1 : T_dynlit0; p = PAL_S; break;
    case GAS: t = T_gas; p = PAL_S; break;
    case BOOM: t = anim & 2 ? T_boom1 : T_boom0; p = PAL_S; break;
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
    k_print(1, 1, "OCAK", 2);
}

static void hud(void) {
    k_print_num(3, 0, gems, 2, gems >= need[level] ? 1 : 0);
    k_print_num(6, 0, need[level], 2, 0);
    k_print_num(12, 0, time_left, 3, time_left <= 20 && (frames60 & 32) ? 3 : 0);
    k_print_num(18, 0, lives, 1, 0);
    k_print_num(26, 0, score, 5, 0);
    k_print_num(6, 1, level + 1, 1, 2);
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
        if (act == 1 && moving) pat = tick < 3 ? S_digup : S_digdn;
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
    gems = 0; exit_open = 0; exit_stage = 0; alive = 1; won = 0; moving = 0; face = 0; act = 0;
    time_left = 150; frames60 = 0; tick = 0; shake = 0;
    for (x = 0; x < NFX; ++x) fx_t[x] = 0;
    set_mine_palettes(level);
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            c = levels[level][y][x];
            switch (c) {
            case 'W': map[y][x] = WALL; break;
            case '.': map[y][x] = DIRT; break;
            case 'O': map[y][x] = ROCK; break;
            case '*': map[y][x] = GEM; break;
            case '+': map[y][x] = GEM2; break;
            case 'E': map[y][x] = EXIT; exit_x = x; exit_y = y; break;
            case 'P': map[y][x] = HERO; hero_x = x; hero_y = y; break;
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

/* ---------------------------------------------------------------- the miner moves */
static void move_hero(signed char dx, signed char dy) {
    unsigned char nx = hero_x + dx, ny = hero_y + dy, t = KIND(map[ny][nx]);
    face = dy > 0 ? 0 : dy < 0 ? 1 : dx > 0 ? 2 : 3;
    act = 0;
    if (t == WALL || t == CRACK) return;
    if (t == ROCK) {
        /* push sideways into an empty cell */
        if (dy || KIND(map[ny][nx + dx]) != EMPTY) return;
        map[ny][nx + dx] = ROCK;
        draw_cell(nx + dx, ny);
        act = 2;
    } else if (t == GEM || t == GEM2) {
        gems += t == GEM2 ? 5 : 1;
        score += t == GEM2 ? 50 : 10;
        k_sfx(SFX_PICKUP);
        fx_add(0, nx, ny);
        if (gems >= need[level] && !exit_open) { exit_open = 1; k_sfx(SFX_WIN); }
        hud();
    } else if (t == EXIT) {
        if (exit_stage < 3) return;
        won = 1;
    } else if (t == DIRT) {
        k_sfx(SFX_TICK);
        act = 1;
    }
    map[hero_y][hero_x] = EMPTY;
    from_x = hero_x; from_y = hero_y;
    redraw(hero_x, hero_y);
    hero_x = nx; hero_y = ny;
    map[ny][nx] = HERO;
    redraw(nx, ny);
    moving = 1;
}

/* ---------------------------------------------------------------- screens */
static unsigned char slots[18] = { 0, 4, 8, 12, 16, 20, 24, 28, 216, 220, 224, 228, 232, 236, 240, 244, 248, 252 };

static void wait_start(void) {
    /* both keyboard players (Space or Enter) and pads 1-2 can start */
    do { k_wait_frame(); } while (!((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A)));
}

static void title(void) {
    unsigned char i, f = 0;
    static unsigned char gx[8], gy[8], gs[8];
    k_clear_map();
    k_hide_sprites(0);
    k_split(0, 0, 0);
    cam = 0; k_scroll(0, 0);
    set_mine_palettes(0);
    big_reset();
    big_center(4, "KRISTAL");
    big_center(7, "OCAK");
    k_print(7, 12, "KAZ, KRISTALLERI TOPLA,", 0);
    k_print(7, 14, "SALLANAN KAYADAN KAC VE", 0);
    k_print(7, 16, "ACILAN KAPIDAN CIK.", 0);
    k_print(7, 20, "OKLAR / WASD: KAZ, YURU", 2);
    k_print(9, 24, "SPACE/ENTER: BASLA ", 1);
    for (i = 0; i < 8; ++i) { gx[i] = 20 + i * 36; gy[i] = k_rand(); gs[i] = 1 + (i & 1); }
    for (;;) {
        k_wait_frame(); ++f;
        big_palette(f < 32 ? f >> 2 : 8);
        for (i = 0; i < 8; ++i) {               /* crystals fall behind the title */
            gy[i] += gs[i];
            k_sprite(8 + i, gx[i], gy[i] < 232 ? gy[i] : 300, S_fgem, PAL_C | SPR_BEHIND);
        }
        k_sprite(SPR_HERO, 152, 196, (f >> 4) & 1 ? S_digup : S_digdn, PAL_HERO);
        if ((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A)) break;
    }
    k_hide_sprites(0);
}

static void message(const char *s) {
    unsigned char t;
    k_fill_tiles(0, 1, 40, 1, ' ', 0);
    k_print(12, 1, s, 3);
    for (t = 0; t < 90; ++t) { k_wait_frame(); ++anim; draw_hero(); fx_draw(); }
}

void main(void) {
    unsigned char p;
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
    for (;;) {
        title();
        level = 0; lives = 3; score = 0;
        k_clear_map();
        load_level();
        while (lives) {
            k_wait_frame();
            ++anim;
            p = pad[0] | pad[1];
            if ((p & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) k_menu();
            if ((pad_new[0] | pad_new[1]) & BTN_START) {
                k_print(30, 1, "DURAKLADI", 1);
                do k_wait_frame(); while (!((pad_new[0] | pad_new[1]) & BTN_START));
                k_print(30, 1, "         ", 0);
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
                moving = 0;
                from_x = hero_x; from_y = hero_y;
                if (p & BTN_UP) move_hero(0, -1);
                else if (p & BTN_DOWN) move_hero(0, 1);
                else if (p & BTN_LEFT) move_hero(-1, 0);
                else if (p & BTN_RIGHT) move_hero(1, 0);
                physics();
            }
            draw_hero();
            fx_draw();
            if (won) {
                score += time_left;
                k_sfx(SFX_WIN);
                message("BOLUM TAMAM!");
                level = (level + 1) % 3;
                load_level();
            } else if (!alive) {
                k_sfx(SFX_LOSE);
                --lives;
                message(lives ? "EZILDIN!" : "OYUN BITTI");
                if (lives) load_level();
            }
        }
        k_print(12, 1, "SPACE: TEKRAR", 0);
        wait_start();
    }
}
