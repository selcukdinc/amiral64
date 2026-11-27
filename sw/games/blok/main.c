/* Blok Yagmuru: falling blocks. Seven four-cell pieces fall into a 10 x 20 well; full rows disappear.
 * Pad: Left/Right move, Down drops faster, Up drops at once, A turns right, B turns left, Select holds the
 * piece for later (when it is let go, so Select + Start can go back to the menu). Start pauses.
 * Keyboard extras: X / Z turn right / left, C holds, Esc goes back to the menu.
 * Pieces come in shuffled bags of seven; the ghost shows where the piece will land. Every 10 rows the
 * level goes up and pieces fall faster. Rows: 1 = 100, 2 = 300, 3 = 500, 4 = 800 points x (level + 1). */
#include "konsol.h"
#include "bigtext.h"
#include "art.h"
#include "ses.h"
#include "muzik.h"

#define WELL_X 15          /* tile column of the well's first column */
#define WELL_Y 5           /* tile row of the first visible row */
#define COLS 10
#define ROWS 22            /* 2 hidden rows above the 20 visible ones */
#define HIDDEN 2
#define PAL_PIECE 4        /* palettes 4..10: the seven pieces */
#define PAL_UI 11
#define PAL_SPR 12
#define PAL_BIG 13

#define SAVE ((unsigned char *)0x02E0)   /* 'B', best score (3 bytes): kept while the board is on */

/* 4 rotations of the 7 pieces as 4x4 masks (row 0 = top nibble, bit 3 = left column) */
static const unsigned shapes[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },   /* I */
    { 0x0660, 0x0660, 0x0660, 0x0660 },   /* O */
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },   /* T */
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },   /* S */
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },   /* Z */
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },   /* J */
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },   /* L */
};
/* frames per row at each level */
static const unsigned char gravity[16] = { 48, 42, 37, 32, 27, 22, 18, 14, 10, 8, 6, 5, 4, 3, 2, 1 };
static const unsigned line_points[5] = { 0, 100, 300, 500, 800 };

unsigned char board[ROWS][COLS];               /* 0 = empty, 1..7 = piece + 1 */
static unsigned char shown[20][COLS];          /* what the screen shows (tile code) */
unsigned char cur, rot, next_q[3], hold, can_hold;
signed char px, py;                            /* piece position (4x4 box), py in board rows */
unsigned char level, start_level, game_over;
unsigned lines;
unsigned long score, best;
static unsigned char bag[7], bag_n;
static unsigned char fall_t, lock_t, lock_moves, das_t, das_dir;
static unsigned char clear_rows_[4], nclear, clear_t;
static unsigned char shake;
static unsigned t, msg_t;

/* ---------------------------------------------------------------- helpers */
static void put(unsigned char x, unsigned char y, unsigned char tile, unsigned char attr) {
    k_vaddr(0x4000 + ((unsigned)y << 7) + ((unsigned)x << 1));
    PPU_VDATA = tile; PPU_VDATA = attr;
}

static const unsigned long pow10[7] = { 1000000UL, 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
static void print_long(unsigned char x, unsigned char y, unsigned long v, unsigned char digits, unsigned char pal) {
    /* digits <= 7; repeated subtraction: cc65's 32-bit division would take a good part of a frame */
    char buf[8];
    unsigned char i, k = 0, d;
    for (i = 7 - digits; i < 7; ++i) {
        d = '0';
        while (v >= pow10[i]) { v -= pow10[i]; ++d; }
        buf[k++] = d;
    }
    buf[k] = 0;
    k_print(x, y, buf, pal);
}

static void panel(unsigned char x, unsigned char y, unsigned char w, unsigned char h) {
    /* (wl/hl precomputed: cc65 2.18 -O drops the high byte of "i == w - 1" here and never matches) */
    unsigned char i, j, tile, wl = w - 1, hl = h - 1;
    for (j = 0; j < h; ++j) {
        k_vaddr(0x4000 + ((unsigned)(y + j) << 7) + ((unsigned)x << 1));
        for (i = 0; i < w; ++i) {
            if (j == 0) tile = i == 0 ? T_frTL : i == wl ? T_frTR : T_frT;
            else if (j == hl) tile = i == 0 ? T_frBL : i == wl ? T_frBR : T_frB;
            else tile = i == 0 ? T_frL : i == wl ? T_frR : T_frC;
            PPU_VDATA = tile; PPU_VDATA = PAL_UI;
        }
    }
}

static unsigned char cell_of(unsigned mask, unsigned char x, unsigned char y) {
    return (mask >> (15 - (y * 4 + x))) & 1;
}

/* ---------------------------------------------------------------- sound (games/blok/muzik.ses) */
#define sfx_move()   ses_efekt(sfx_kay)
#define sfx_rotate() ses_efekt(sfx_don)
#define sfx_lock()   ses_efekt(sfx_kilit)
#define sfx_drop()   ses_efekt(sfx_dus)
#define sfx_hold()   ses_efekt(sfx_tut)
static unsigned char danger;           /* the danger tune is on */

/* the music follows the game: faster each level, the danger tune when the stack is near the top */
static void music_for_stack(void) {
    unsigned char y, x, top = ROWS;
    for (y = 0; y < ROWS && top == ROWS; ++y)
        for (x = 0; x < COLS; ++x) if (board[y][x]) { top = y; break; }
    if (!danger && top < HIDDEN + 6) { danger = 1; ses_muzik(mus_tehlike); }
    else if (danger && top >= HIDDEN + 9) { danger = 0; ses_muzik(mus_tema); }
    ses_hizlan(danger ? 0 : level * 3);
}

/* ---------------------------------------------------------------- the bag */
static unsigned char from_bag(void) {
    unsigned char i, j, x;
    if (bag_n == 0) {
        for (i = 0; i < 7; ++i) bag[i] = i;
        for (i = 6; i; --i) { j = k_rand() % (i + 1); x = bag[i]; bag[i] = bag[j]; bag[j] = x; }
        bag_n = 7;
    }
    return bag[--bag_n];
}

/* ---------------------------------------------------------------- the board */
static unsigned char fits(unsigned char p, unsigned char r, signed char x, signed char y) {
    unsigned m = shapes[p][r];
    unsigned char i, j;
    signed char bx, by;
    for (j = 0; j < 4; ++j)
        for (i = 0; i < 4; ++i, m <<= 1)
            if (m & 0x8000) {
                bx = x + i; by = y + j;
                if (bx < 0 || bx >= COLS || by >= ROWS) return 0;
                if (by >= 0 && board[by][bx]) return 0;
            }
    return 1;
}

static signed char ghost_y(void) {
    signed char y = py;
    while (fits(cur, rot, px, y + 1)) ++y;
    return y;
}

/* screen code of a cell: 0 well, 1..7 block of piece, 8+p ghost of piece p, 16 flash */
static void draw_cell(unsigned char x, unsigned char y, unsigned char code) {
    unsigned char tile, attr;
    if (shown[y][x] == code) return;
    shown[y][x] = code;
    if (code == 0) { tile = T_well; attr = PAL_UI; }
    else if (code < 8) { tile = T_block; attr = PAL_PIECE + code - 1; }
    else if (code < 16) { tile = T_ghost; attr = PAL_PIECE + code - 8; }
    else { tile = T_flash; attr = PAL_UI; }
    put(WELL_X + x, WELL_Y + y, tile, attr);
}

static unsigned char is_clearing(unsigned char row) {
    unsigned char i;
    for (i = 0; i < nclear; ++i) if (clear_rows_[i] == row) return 1;
    return 0;
}

static unsigned char dirty;             /* something on the board changed: redraw it */

static void overlay(signed char oy, unsigned char code, unsigned char only_empty) {
    unsigned m = shapes[cur][rot];
    unsigned char i, j;
    signed char bx, by;
    for (j = 0; j < 4; ++j)
        for (i = 0; i < 4; ++i, m <<= 1)
            if (m & 0x8000) {
                bx = px + i; by = oy + j - HIDDEN;
                if (by < 0 || by >= 20) continue;
                if (only_empty && board[by + HIDDEN][bx]) continue;
                draw_cell(bx, by, code);
            }
}

static void draw_board(void) {
    unsigned char x, y, flashing;
    unsigned char *row;
    if (!dirty) return;
    dirty = 0;
    for (y = 0; y < 20; ++y) {
        row = board[y + HIDDEN];
        flashing = clear_t && (clear_t & 4) && is_clearing(y + HIDDEN);
        for (x = 0; x < COLS; ++x) draw_cell(x, y, flashing ? 16 : row[x]);
    }
    if (!clear_t && !game_over) {
        overlay(ghost_y(), 8 + cur, 1);
        overlay(py, cur + 1, 0);
    }
}

/* a piece shown in a 4x2 box (next / hold) */
static void draw_mini(unsigned char x, unsigned char y, unsigned char p) {
    unsigned m;
    unsigned char i, j;
    for (j = 0; j < 2; ++j)
        for (i = 0; i < 4; ++i) put(x + i, y + j, T_frC, PAL_UI);
    if (p > 6) return;
    m = shapes[p][0];
    for (j = 0; j < 2; ++j)
        for (i = 0; i < 4; ++i)
            if (cell_of(m, i, j + (p <= 1 ? 1 : 0))) put(x + i, y + j, T_block, PAL_PIECE + p);
}

static unsigned char side_dirty;
static void draw_score(void) {
    print_long(4, 13, score, 7, 0);
    k_seg_number((unsigned)(score > 9999 ? 9999 : score));
}

static void draw_side(void) {
    unsigned char i;
    for (i = 0; i < 3; ++i) draw_mini(30, 7 + i * 4, next_q[i]);
    draw_mini(5, 7, hold);
    print_long(4, 13, score, 7, 0);
    k_print_num(4, 16, level, 2, 0);
    k_print_num(4, 19, lines, 4, 0);
    print_long(4, 22, best > score ? best : score, 7, 2);
    k_seg_number((unsigned)(score > 9999 ? 9999 : score));
}

/* ---------------------------------------------------------------- particles */
#define NP 24
static int pxs[NP], pys[NP];
static signed char pvx[NP], pvy[NP];
static unsigned char plife[NP], live;
static void burst(unsigned char row, unsigned char n) {
    unsigned char i, k = 0;
    for (i = 0; i < NP && k < n; ++i) {
        if (plife[i]) continue;
        pxs[i] = ((WELL_X + (k_rand() % COLS)) * 8) << 2;
        pys[i] = ((WELL_Y + row - HIDDEN) * 8) << 2;
        pvx[i] = (signed char)(k_rand() & 15) - 8;
        pvy[i] = -(signed char)(8 + (k_rand() & 15));
        plife[i] = 30 + (k_rand() & 15);
        ++k; live = 1;
    }
}
static void particles(void) {
    unsigned char i, n = 0;
    if (!live) return;
    for (i = 0; i < NP; ++i) {
        if (!plife[i]) { oam[i].y = 0xF0; continue; }
        ++n;
        --plife[i];
        pxs[i] += pvx[i]; pys[i] += pvy[i];
        if ((t & 1) && pvy[i] < 16) ++pvy[i];
        k_sprite(i, (pxs[i] >> 2) - 4, (pys[i] >> 2) - 4, plife[i] & 4 ? S_spark : S_chip, PAL_SPR);
    }
    live = n;
}

/* ---------------------------------------------------------------- game flow */
static void new_piece(unsigned char p) {
    /* appears in the top visible rows (the I piece lies in row 1 of its box, the others start in row 0) */
    cur = p; rot = 0; px = 3; py = p == 0 ? HIDDEN - 1 : HIDDEN; dirty = 1;
    lock_t = 0; lock_moves = 0; fall_t = 0;
    if (!fits(cur, rot, px, py)) {
        game_over = 1;
        ses_muzik(mus_bitti);
    }
}

static void spawn(void) {
    unsigned char p = next_q[0];
    next_q[0] = next_q[1]; next_q[1] = next_q[2]; next_q[2] = from_bag();
    new_piece(p);
    can_hold = 1;
    draw_side();
}

static void find_full(void) {
    unsigned char y, x;
    nclear = 0;
    for (y = 0; y < ROWS; ++y) {
        for (x = 0; x < COLS && board[y][x]; ++x) ;
        if (x == COLS) clear_rows_[nclear++] = y;
    }
}

static void collapse(void) {
    unsigned char i, y, x, r;
    for (i = 0; i < nclear; ++i) {
        r = clear_rows_[i];
        for (y = r; y > 0; --y)
            for (x = 0; x < COLS; ++x) board[y][x] = board[y - 1][x];
        for (x = 0; x < COLS; ++x) board[0][x] = 0;
    }
}

static void lock_piece(void) {
    unsigned m = shapes[cur][rot];
    unsigned char i, j;
    for (j = 0; j < 4; ++j)
        for (i = 0; i < 4; ++i)
            if (cell_of(m, i, j) && py + j >= 0) board[py + j][px + i] = cur + 1;
    sfx_lock();
    dirty = 1;
    find_full();
    if (nclear) {
        unsigned char old = level;
        clear_t = 24;
        score += (unsigned long)line_points[nclear] * (level + 1);
        lines += nclear;
        if (start_level + lines / 10 > level) level = start_level + lines / 10;
        if (level > 15) level = 15;
        for (i = 0; i < nclear; ++i) burst(clear_rows_[i], nclear == 4 ? 6 : 4);
        msg_t = t;
        if (nclear == 4) { shake = 12; ses_efekt(sfx_dortlu); k_print(WELL_X + 2, 27, "DORTLU!", 1); }
        else { ses_perde(nclear * 3 - 3); ses_efekt(sfx_sil); }
        if (level != old) { ses_efekt(sfx_seviye); k_print(WELL_X + 1, 28, "SEVIYE UP", 2); }
    } else { spawn(); music_for_stack(); }
    draw_side();
}

static void try_move(signed char dx) {
    if (fits(cur, rot, px + dx, py)) {
        px += dx; dirty = 1;
        sfx_move();
        if (lock_t && lock_moves < 15) { lock_t = 0; ++lock_moves; }
    }
}

static void try_rotate(signed char dir) {
    static const signed char kicks[6][2] = { {0,0}, {-1,0}, {1,0}, {-2,0}, {2,0}, {0,-1} };
    unsigned char r = (rot + (dir > 0 ? 1 : 3)) & 3, k;
    for (k = 0; k < 6; ++k)
        if (fits(cur, r, px + kicks[k][0], py + kicks[k][1])) {
            rot = r; px += kicks[k][0]; py += kicks[k][1]; dirty = 1;
            sfx_rotate();
            if (lock_t && lock_moves < 15) { lock_t = 0; ++lock_moves; }
            return;
        }
}

static void do_hold(void) {
    unsigned char h;
    if (!can_hold) return;
    sfx_hold();
    h = hold;
    hold = cur;
    if (h > 6) spawn(); else new_piece(h);
    can_hold = 0;
    draw_side();
}

static void hard_drop(void) {
    signed char y = ghost_y();
    score += (unsigned)(y - py) * 2;
    py = y;
    sfx_drop();
    burst(py + 3, 2);
    lock_piece();
}

/* ---------------------------------------------------------------- screens */
static void background(void) {
    unsigned char y, x;
    for (y = 0; y < 30; ++y) {
        k_vaddr(0x4000 + ((unsigned)y << 7));
        for (x = 0; x < 64; ++x) { PPU_VDATA = T_bg; PPU_VDATA = PAL_UI; }
    }
}

/* the well's rim hugs the playfield: no panel-coloured strip that looks like one more column */
static void well_frame(void) {
    unsigned char i;
    put(WELL_X - 1, WELL_Y - 1, T_wTL, PAL_UI); put(WELL_X + COLS, WELL_Y - 1, T_wTR, PAL_UI);
    put(WELL_X - 1, WELL_Y + 20, T_wBL, PAL_UI); put(WELL_X + COLS, WELL_Y + 20, T_wBR, PAL_UI);
    for (i = 0; i < COLS; ++i) { put(WELL_X + i, WELL_Y - 1, T_wT, PAL_UI); put(WELL_X + i, WELL_Y + 20, T_wB, PAL_UI); }
    for (i = 0; i < 20; ++i) { put(WELL_X - 1, WELL_Y + i, T_wL, PAL_UI); put(WELL_X + COLS, WELL_Y + i, T_wR, PAL_UI); }
}

static void draw_playfield(void) {
    unsigned char x, y;
    background();
    well_frame();
    for (y = 0; y < 20; ++y) for (x = 0; x < COLS; ++x) { shown[y][x] = 255; draw_cell(x, y, 0); }
    panel(3, 4, 8, 6);   k_print(4, 5, "TUT", 2);
    panel(3, 11, 10, 14);
    k_print(4, 12, "PUAN", 1);
    k_print(4, 15, "SEVIYE", 1);
    k_print(4, 18, "SIRA", 1);
    k_print(4, 21, "EN IYI", 1);
    panel(28, 4, 8, 15); k_print(29, 5, "SIRADAKI", 2);
    k_print(28, 20, "OKLAR  YURU", 2);
    k_print(28, 21, "X/SHIFT SAGA", 0);
    k_print(28, 22, "Z/CTRL  SOLA", 0);
    k_print(28, 23, "YUKARI BIRAK", 0);
    k_print(28, 24, "C      TUT", 0);
    k_print(28, 25, "SPACE  DUR", 0);
    k_print(28, 26, "ESC    MENU", 0);
    big_reset();
    big_text(WELL_X - 1, 1, "BLOK");
    k_print(WELL_X + 8, 2, "YAGMURU", 2);
}

static void palettes(void) {
    k_palette(PAL_PIECE + 0, pal_cyan, 16); k_palette(PAL_PIECE + 1, pal_yel, 16); k_palette(PAL_PIECE + 2, pal_pur, 16);
    k_palette(PAL_PIECE + 3, pal_grn, 16); k_palette(PAL_PIECE + 4, pal_red, 16); k_palette(PAL_PIECE + 5, pal_blu, 16);
    k_palette(PAL_PIECE + 6, pal_org, 16);
    k_palette(PAL_UI, pal_ui, 16);
    k_palette(PAL_SPR, pal_spr, 16);
    k_palette(0, pal_ui + 3, 1);          /* backdrop = panel fill, so text sits cleanly on the panels */
    big_palette(8);
}

/* title: falling pieces behind the logo; Left/Right pick the start level */
static void title(void) {
    unsigned char i, p;
    unsigned char fx[8], fy[8], fp[8];
    background();
    ses_muzik(mus_tema);
    ses_hizlan(0);
    ses_ses(15);
    panel(8, 4, 24, 7);
    big_reset();
    big_center(6, "BLOK YAGMURU");
    panel(10, 14, 20, 9);
    k_print(12, 15, "BASLANGIC SEVIYESI", 1);
    k_print(11, 19, "SPACE/ENTER BASLA", 0);
    k_print(11, 27, "ESC: MENUYE DON", 0);
    k_print(14, 25, "EN IYI", 1);
    print_long(21, 25, best, 7, 0);
    for (i = 0; i < 8; ++i) { fx[i] = k_rand() % 38; fy[i] = k_rand() % 30; fp[i] = k_rand() % 7; }
    for (;;) {
        k_wait_frame(); ++t;
        k_print(18, 17, "<", start_level ? 2 : 3);
        k_print_num(19, 17, start_level, 2, 0);
        k_print(21, 17, ">", start_level < 9 ? 2 : 3);
        /* falling chips (sprites) */
        for (i = 0; i < 8; ++i) {
            if ((t & 1) == 0 && ++fy[i] > 240) { fy[i] = 0; fx[i] = k_rand() % 38; fp[i] = k_rand() % 7; }
            k_sprite(i, fx[i] * 8 + 4, fy[i], S_chip, PAL_SPR);
        }
        p = pad_new[0] | pad_new[1];
        if ((pad[0] | pad[1]) & BTN_SELECT && p & BTN_START) k_menu();
        if (p & BTN_LEFT && start_level) { --start_level; sfx_move(); }
        if (p & BTN_RIGHT && start_level < 9) { ++start_level; sfx_move(); }
        if (p & (BTN_A | BTN_START)) break;
    }
    k_seed(t * 251u + 17);
    k_hide_sprites(0);
    ses_efekt(sfx_basla);
}

static void play(void) {
    unsigned char p, held, i, keys, keys_new, keys_old = 0, sel_armed = 0;
    unsigned char paused = 0;
    for (i = 0; i < ROWS; ++i) { unsigned char x; for (x = 0; x < COLS; ++x) board[i][x] = 0; }
    score = 0; lines = 0; level = start_level; game_over = 0; nclear = 0; clear_t = 0;
    bag_n = 0; hold = 255; shake = 0;
    for (i = 0; i < NP; ++i) plife[i] = 0;
    draw_playfield();
    danger = 0;
    ses_muzik(mus_tema);
    ses_hizlan(level * 3);
    next_q[0] = from_bag(); next_q[1] = from_bag(); next_q[2] = from_bag();
    spawn();
    das_t = 0; das_dir = 0;
    while (!game_over) {
        k_wait_frame(); ++t;
        if (t - msg_t == 120) { k_fill_tiles(WELL_X, 27, 10, 2, T_bg, PAL_UI); }
        p = pad_new[0] | pad_new[1];
        held = pad[0] | pad[1];
        /* keyboard extras: X / Z turn, C holds */
        keys = (k_key(KEY_X) ? 1 : 0) | (k_key(KEY_Z) ? 2 : 0) | (k_key(KEY_C) ? 4 : 0);
        keys_new = keys & ~keys_old; keys_old = keys;
        /* Select: hold the piece when it is let go, unless Start came in between (= menu) */
        if (p & BTN_SELECT) sel_armed = 1;
        if ((held & BTN_SELECT) && (p & BTN_START)) k_menu();
        if (p & BTN_START) {
            paused = !paused;
            k_print(WELL_X + 1, WELL_Y + 9, paused ? "DURAKLADI" : "         ", 1);
            k_print(WELL_X, WELL_Y + 11, paused ? " ESC MENU  " : "           ", 0);
            if (!paused) {
                unsigned char x;
                for (x = 0; x < COLS; ++x) { shown[9][x] = 255; shown[11][x] = 255; }
                dirty = 1;
            }
            ses_ses(paused ? 4 : 15);
            ses_efekt(sfx_duraklat);
        }
        if (paused) {
            if (p & BTN_SELECT) k_menu();
            continue;
        }
        particles();
        if (shake) { --shake; k_scroll((shake & 2) ? 2 : 0, (shake & 1) ? 1 : 0); }
        else k_scroll(0, 0);
        if (clear_t) {                         /* rows flash, then fall */
            if ((clear_t & 3) == 0) dirty = 1;
            if (--clear_t == 0) {
                dirty = 1;
                collapse();
                nclear = 0;
                spawn();
                music_for_stack();
            }
            draw_board();
            continue;
        }
        /* input: DAS for left/right */
        if (p & BTN_LEFT) { try_move(-1); das_dir = BTN_LEFT; das_t = 0; }
        else if (p & BTN_RIGHT) { try_move(1); das_dir = BTN_RIGHT; das_t = 0; }
        else if (held & das_dir) {
            if (++das_t >= 10 && ((das_t - 10) & 1) == 0) try_move(das_dir == BTN_LEFT ? -1 : 1);
        } else das_dir = 0;
        if ((p & BTN_A) || (keys_new & 1)) try_rotate(1);
        if ((p & BTN_B) || (keys_new & 2)) try_rotate(-1);
        if (sel_armed && !(held & BTN_SELECT)) { sel_armed = 0; do_hold(); }
        if (keys_new & 4) do_hold();
        if (p & BTN_UP) { hard_drop(); draw_board(); continue; }
        /* gravity (soft drop: every frame) */
        if (++fall_t >= ((held & BTN_DOWN) ? 1 : gravity[level])) {
            fall_t = 0;
            if (fits(cur, rot, px, py + 1)) {
                ++py; dirty = 1;
                if (held & BTN_DOWN) { ++score; side_dirty = 1; }
            }
        }
        if (!fits(cur, rot, px, py + 1)) {
            if (++lock_t >= 30 || (held & BTN_DOWN && lock_t >= 4)) { lock_piece(); }
        } else lock_t = 0;
        draw_board();
        if (side_dirty && (t & 7) == 0) { side_dirty = 0; draw_score(); }
    }
    /* top out: the stack greys from the bottom up */
    k_scroll(0, 0);
    for (i = 0; i < 20; ++i) {
        unsigned char x;
        for (x = 0; x < COLS; ++x) if (board[ROWS - 1 - i][x]) draw_cell(x, 19 - i, 16);
        k_wait_frame(); k_wait_frame();
    }
    if (score > best) {
        best = score;
        SAVE[0] = 'B'; SAVE[1] = (unsigned char)best; SAVE[2] = (unsigned char)(best >> 8); SAVE[3] = (unsigned char)(best >> 16);
    }
    panel(WELL_X - 2, 10, 14, 6);
    k_print(WELL_X + 0, 11, "OYUN BITTI", 3);
    print_long(WELL_X + 1, 13, score, 7, 0);
    k_print(WELL_X - 1, 14, best == score && score ? "  YENI REKOR!" : " SPACE TEKRAR", 1);
    for (i = 0; i < NP; ++i) plife[i] = 0;
    for (i = 0; i < 60; ++i) { k_wait_frame(); particles(); }
    do k_wait_frame(); while (!((pad_new[0] | pad_new[1]) & (BTN_A | BTN_START)));
}

void main(void) {
    static const unsigned char slots[10] = { 0, 4, 8, 12, 16, 20, 24, 28, 240, 244 };
    k_init();
    PPU_CTRL = 0;
    k_vcopy(TILE_BASE * 32, tiles_data, sizeof(tiles_data));
    k_vcopy(0x2000, sprites_data, sizeof(sprites_data));
    big_init(slots, 10, PAL_BIG);
    palettes();
    PPU_CTRL = CTRL_NMI | CTRL_BG | CTRL_SPR;
    if (SAVE[0] == 'B') best = SAVE[1] | (unsigned)SAVE[2] << 8 | (unsigned long)SAVE[3] << 16;
    SYS_TEST = 0xB0;
    for (;;) {
        title();
        SYS_TEST = 0xB1;
        play();
    }
}
