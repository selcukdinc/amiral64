/* Kristal Ocak: dig through the mine, collect the crystals, reach the exit before time runs out.
 * Rocks and crystals fall and roll; a falling rock is deadly. 3 levels, 3 lives.
 * Pad: directions move, Start pauses, Select + Start goes back to the menu. */
#include "konsol.h"
#include "art.h"

#define W 32
#define H 13
#define FIELD_ROW 2          /* tile row of the first field row (rows 0-1: status line) */
#define TICK 6               /* frames per game step */

enum { EMPTY, DIRT, WALL, ROCK, GEM, EXIT, HERO };
#define MOVED 0x80
#define FALLING 0x40
#define KIND(c) ((c) & 0x0F)

static const char *const levels[3][H] = {
  { "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    "WP.....O...*.....O.....*....O..W",
    "W.O.O....O...O.*....O....*.....W",
    "W....*..WWWWW......O.....WWW...W",
    "W..O.....*.....O.....*.........W",
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
    "W.......*.......**.......*.....W",
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
    "WWWWWW..WWW...***...WWW..WWWWWWW",
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

unsigned char map[H][W];
unsigned char hero_x, hero_y, gems, level, lives, alive, exit_open, won;
unsigned score, time_left;
static unsigned cam, cam_target;
static unsigned char exit_x, exit_y, tick, frames60;

static const unsigned char tiles_of[7] = { ' ', T_dirt, T_wall, T_rock, T_gem, T_exitc, T_hero };

static void draw_cell(unsigned char x, unsigned char y) {
    unsigned char t, k = KIND(map[y][x]);
    unsigned a;
    if (k == EXIT && exit_open) t = T_exito; else t = tiles_of[k];
    a = 0x4000 + ((unsigned)(FIELD_ROW + 2 * y) << 7) + ((unsigned)x << 2);
    if (k == EMPTY) {
        k_vaddr(a); PPU_VDATA = ' '; PPU_VDATA = 0; PPU_VDATA = ' '; PPU_VDATA = 0;
        k_vaddr(a + 128); PPU_VDATA = ' '; PPU_VDATA = 0; PPU_VDATA = ' '; PPU_VDATA = 0;
    } else {
        k_vaddr(a); PPU_VDATA = t; PPU_VDATA = 4; PPU_VDATA = t + 1; PPU_VDATA = 4;
        k_vaddr(a + 128); PPU_VDATA = t + 2; PPU_VDATA = 4; PPU_VDATA = t + 3; PPU_VDATA = 4;
    }
}

static void status(void) {
    k_print(1, 0, "KRISTAL", 1);
    k_print_num(9, 0, gems, 2, 0);
    k_print(11, 0, "/", 0);
    k_print_num(12, 0, need[level], 2, 0);
    k_print(16, 0, "SURE", 1);
    k_print_num(21, 0, time_left, 3, 0);
    k_print(26, 0, "CAN", 1);
    k_print_num(30, 0, lives, 1, 0);
    k_print(1, 1, "PUAN", 1);
    k_print_num(6, 1, score, 5, 0);
    k_seg_number(score);
}

static void load_level(void) {
    unsigned char x, y;
    char c;
    gems = 0; exit_open = 0; alive = 1; won = 0;
    time_left = 150; frames60 = 0; tick = 0;
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            c = levels[level][y][x];
            switch (c) {
            case 'W': map[y][x] = WALL; break;
            case '.': map[y][x] = DIRT; break;
            case 'O': map[y][x] = ROCK; break;
            case '*': map[y][x] = GEM; break;
            case 'E': map[y][x] = EXIT; exit_x = x; exit_y = y; break;
            case 'P': map[y][x] = HERO; hero_x = x; hero_y = y; break;
            default: map[y][x] = EMPTY;
            }
            draw_cell(x, y);
        }
    k_fill_tiles(0, 0, 64, 2, ' ', 0);
    status();
}

/* Rocks and crystals: fall into empty cells, roll off round things, crush the hero when falling. */
static void physics(void) {
    signed char y;
    unsigned char x, c, k, below;
    for (y = H - 2; y >= 0; --y)
        for (x = 1; x < W - 1; ++x) {
            c = map[y][x];
            k = KIND(c);
            if ((k != ROCK && k != GEM) || (c & MOVED)) continue;
            below = map[y + 1][x];
            if (KIND(below) == EMPTY) {
                map[y + 1][x] = k | MOVED | FALLING;
                map[y][x] = EMPTY;
                draw_cell(x, y); draw_cell(x, y + 1);
            } else if (KIND(below) == HERO && (c & FALLING)) {
                alive = 0;
            } else if (KIND(below) == ROCK || KIND(below) == GEM || KIND(below) == WALL) {
                if (KIND(below) != WALL && map[y][x - 1] == EMPTY && map[y + 1][x - 1] == EMPTY) {
                    map[y][x - 1] = k | MOVED | FALLING; map[y][x] = EMPTY;
                    draw_cell(x, y); draw_cell(x - 1, y);
                } else if (KIND(below) != WALL && map[y][x + 1] == EMPTY && map[y + 1][x + 1] == EMPTY) {
                    map[y][x + 1] = k | MOVED | FALLING; map[y][x] = EMPTY;
                    draw_cell(x, y); draw_cell(x + 1, y);
                } else {
                    if (c & FALLING) k_sfx(SFX_FALL);
                    map[y][x] = k;
                }
            } else {
                if (c & FALLING) k_sfx(SFX_FALL);
                map[y][x] = k;
            }
        }
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) map[y][x] &= ~MOVED;
}

static void move_hero(signed char dx, signed char dy) {
    unsigned char nx = hero_x + dx, ny = hero_y + dy, t = KIND(map[ny][nx]);
    if (t == WALL) return;
    if (t == ROCK) {
        /* push sideways into an empty cell */
        if (dy || KIND(map[ny][nx + dx]) != EMPTY) return;
        map[ny][nx + dx] = ROCK;
        draw_cell(nx + dx, ny);
    } else if (t == GEM) {
        ++gems; score += 10; k_sfx(SFX_PICKUP);
        if (gems >= need[level] && !exit_open) { exit_open = 1; draw_cell(exit_x, exit_y); k_sfx(SFX_WIN); }
        status();
    } else if (t == EXIT) {
        if (!exit_open) return;
        won = 1;
    } else if (t == DIRT) {
        k_sfx(SFX_TICK);
    }
    map[hero_y][hero_x] = EMPTY;
    draw_cell(hero_x, hero_y);
    hero_x = nx; hero_y = ny;
    map[ny][nx] = HERO;
    draw_cell(nx, ny);
}

static void wait_start(void) {
    /* both keyboard players (Space or Enter) and pads 1-2 can start */
    do { k_wait_frame(); } while (!((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A)));
}

static void title(void) {
    k_clear_map();
    k_split(0, 0, 0);
    cam = 0; k_scroll(0, 0);
    k_print(14, 6, "KRISTAL OCAK", 1);
    k_print(6, 10, "KAZ, KRISTALLERI TOPLA,", 0);
    k_print(6, 12, "KAYALARA DIKKAT ET VE", 0);
    k_print(6, 14, "KAPIDAN CIK.", 0);
    k_print(9, 20, "A / START ILE BASLA", 2);
    wait_start();
}

static void message(const char *s) {
    k_fill_tiles(0, 0, 40, 2, ' ', 0);
    k_print(12, 0, s, 3);
    for (tick = 0; tick < 90; ++tick) k_wait_frame();
}

void main(void) {
    unsigned char p;
    k_init();
    k_palette(4, pal_ocak, 16);
    k_palette(0, pal_ocak, 1);          /* black backdrop */
    k_vcopy(TILE_BASE * 32, tiles_data, sizeof(tiles_data));
    k_seed(0x1234);
    for (;;) {
        title();
        level = 0; lives = 3; score = 0;
        k_clear_map();
        load_level();
        while (lives) {
            k_wait_frame();
            p = pad[0] | pad[1];
            if ((p & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) k_menu();
            if ((pad_new[0] | pad_new[1]) & BTN_START) {
                k_print(16, 1, "DURAKLADI", 1);
                do k_wait_frame(); while (!((pad_new[0] | pad_new[1]) & BTN_START));
                k_print(16, 1, "         ", 0);
            }
            /* camera follows the hero; the status line stays put (raster split) */
            cam_target = hero_x > 10 ? (hero_x - 10) * 16 : 0;
            if (cam_target > 192) cam_target = 192;
            if (cam < cam_target) cam += 4; else if (cam > cam_target) cam -= 4;
            k_split(14, cam, 0);
            if (++frames60 == 60) {
                frames60 = 0;
                if (time_left) { --time_left; k_print_num(21, 0, time_left, 3, 0); }
                if (!time_left) alive = 0;
            }
            if (++tick < TICK) continue;
            tick = 0;
            if (p & BTN_UP) move_hero(0, -1);
            else if (p & BTN_DOWN) move_hero(0, 1);
            else if (p & BTN_LEFT) move_hero(-1, 0);
            else if (p & BTN_RIGHT) move_hero(1, 0);
            physics();
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
        k_print(12, 1, "START: TEKRAR", 0);
        wait_start();
    }
}
