/* Yildiz Muhafizi: a side-scrolling space shooter. Fly with the pad, A fires.
 * Saucers weave and shoot back, darts dash straight through, mines take three hits.
 * The score line at the top stays still while the stars scroll (raster split). */
#include "konsol.h"
#include "art.h"
#include "ses.h"
#include "muzik.h"

#define MAX_SHOTS 6
#define MAX_EN 8
#define MAX_EB 4
enum { EN_NONE, EN_SAUCER, EN_DART, EN_MINE, EN_BOOM };

/* sprite slots */
#define SPR_SHIP 0
#define SPR_SHOT 1
#define SPR_EN   (SPR_SHOT + MAX_SHOTS)
#define SPR_EB   (SPR_EN + MAX_EN)

static const signed char sine[32] = {
    0, 2, 4, 6, 7, 8, 9, 10, 10, 10, 9, 8, 7, 6, 4, 2,
    0, -2, -4, -6, -7, -8, -9, -10, -10, -10, -9, -8, -7, -6, -4, -2 };

int ship_x, ship_y;
unsigned char lives, invuln, cooldown, alive;
unsigned score, wave;
unsigned char shot_on[MAX_SHOTS];
int shot_x[MAX_SHOTS], shot_y[MAX_SHOTS];
unsigned char en_type[MAX_EN], en_hp[MAX_EN], en_t[MAX_EN];
int en_x[MAX_EN], en_y[MAX_EN], en_base[MAX_EN];
unsigned char eb_on[MAX_EB];
int eb_x[MAX_EB], eb_y[MAX_EB];
unsigned char spawn_timer, starx;
static unsigned bgx;

static void hud(void) {
    k_print(1, 0, "PUAN", 1);
    k_print_num(6, 0, score, 5, 0);
    k_print(14, 0, "DALGA", 1);
    k_print_num(20, 0, wave, 2, 0);
    k_print(25, 0, "GEMI", 1);
    k_print_num(30, 0, lives, 1, 0);
    k_seg_number(score);
}

static void stars(void) {
    unsigned char x, y, r;
    for (y = 2; y < 30; ++y)
        for (x = 0; x < 64; ++x) {
            r = k_rand();
            k_tile(x, y, r < 10 ? T_star3 : r < 40 ? T_star2 : r < 90 ? T_star1 : ' ', 5);
        }
}

static unsigned char hit(int ax, int ay, int bx, int by, int r) {
    int dx = ax - bx, dy = ay - by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx < r && dy < r;
}

static void spawn(void) {
    unsigned char i, r;
    for (i = 0; i < MAX_EN; ++i) if (en_type[i] == EN_NONE) break;
    if (i == MAX_EN) return;
    r = k_rand();
    en_x[i] = 320;
    en_y[i] = 32 + (k_rand() % 176);
    en_base[i] = en_y[i];
    en_t[i] = 0;
    if (r < 120) { en_type[i] = EN_SAUCER; en_hp[i] = 1; }
    else if (r < 220) { en_type[i] = EN_DART; en_hp[i] = 1; }
    else { en_type[i] = EN_MINE; en_hp[i] = 3; }
}

static void enemy_fire(unsigned char i) {
    unsigned char j;
    for (j = 0; j < MAX_EB; ++j)
        if (!eb_on[j]) { eb_on[j] = 1; eb_x[j] = en_x[i]; eb_y[j] = en_y[i]; return; }
}

static void ship_hit(void) {
    if (invuln) return;
    ses_efekt(sfx_vuruldu);
    if (lives) --lives;
    invuln = 120;
    hud();
    if (!lives) alive = 0;
}

static void update(void) {
    unsigned char i, j, p = pad[0] | pad[1], fire = (pad_new[0] | pad_new[1]) & BTN_A;
    /* ship */
    if (p & BTN_UP && ship_y > 20) ship_y -= 2;
    if (p & BTN_DOWN && ship_y < 220) ship_y += 2;
    if (p & BTN_LEFT && ship_x > 4) ship_x -= 2;
    if (p & BTN_RIGHT && ship_x < 200) ship_x += 2;
    if (cooldown) --cooldown;
    if ((fire || (p & BTN_A && !cooldown)) && !cooldown) {
        for (i = 0; i < MAX_SHOTS; ++i)
            if (!shot_on[i]) {
                shot_on[i] = 1; shot_x[i] = ship_x + 10; shot_y[i] = ship_y;
                cooldown = 8; ses_efekt(sfx_ates); break;
            }
    }
    if (invuln) --invuln;
    /* shots */
    for (i = 0; i < MAX_SHOTS; ++i)
        if (shot_on[i]) {
            shot_x[i] += 6;
            if (shot_x[i] > 320) shot_on[i] = 0;
        }
    /* enemies */
    for (i = 0; i < MAX_EN; ++i) {
        if (en_type[i] == EN_NONE) continue;
        ++en_t[i];
        switch (en_type[i]) {
        case EN_SAUCER:
            en_x[i] -= 1 + (wave > 3);
            en_y[i] = en_base[i] + sine[(en_t[i] >> 1) & 31] * 2;
            if ((en_t[i] & 63) == 40 && en_x[i] < 300) enemy_fire(i);
            break;
        case EN_DART: en_x[i] -= 3 + (wave > 5); break;
        case EN_MINE: en_x[i] -= 1; break;
        case EN_BOOM: if (en_t[i] > 16) en_type[i] = EN_NONE; continue;
        }
        if (en_x[i] < -16) { en_type[i] = EN_NONE; continue; }
        for (j = 0; j < MAX_SHOTS; ++j)
            if (shot_on[j] && hit(shot_x[j], shot_y[j], en_x[i], en_y[i], 12)) {
                shot_on[j] = 0;
                if (--en_hp[i] == 0) {
                    score += en_type[i] == EN_MINE ? 50 : en_type[i] == EN_DART ? 30 : 20;
                    en_type[i] = EN_BOOM; en_t[i] = 0;
                    ses_efekt(sfx_patlama);
                    hud();
                } else {
                    ses_efekt(sfx_isabet);
                }
                break;
            }
        if (en_type[i] != EN_BOOM && hit(ship_x, ship_y, en_x[i], en_y[i], 12)) {
            ship_hit();
            en_type[i] = EN_BOOM; en_t[i] = 0;
        }
    }
    for (j = 0; j < MAX_EB; ++j)
        if (eb_on[j]) {
            eb_x[j] -= 3;
            if (eb_x[j] < -8) eb_on[j] = 0;
            else if (hit(ship_x, ship_y, eb_x[j], eb_y[j], 8)) { eb_on[j] = 0; ship_hit(); }
        }
    /* waves: faster spawning as the score grows */
    if (--spawn_timer == 0) {
        spawn();
        spawn_timer = wave < 8 ? 70 - wave * 6 : 22;
    }
    if (score >= wave * 200 && wave < 20) {
        ++wave; hud();
        if (wave == 5) ses_muzik(mus_dalga2);      /* the second theme from wave 5 on */
        ses_efekt(sfx_dalga);
    }
}

static void draw(void) {
    unsigned char i, t;
    k_sprite(SPR_SHIP, (invuln & 4) ? -32 : ship_x, ship_y, S_ship, 8);
    for (i = 0; i < MAX_SHOTS; ++i) k_sprite(SPR_SHOT + i, shot_on[i] ? shot_x[i] : -32, shot_y[i], S_bullet, 8);
    for (i = 0; i < MAX_EN; ++i) {
        switch (en_type[i]) {
        case EN_SAUCER: t = S_saucer; break;
        case EN_DART: t = S_dart; break;
        case EN_MINE: t = S_mine; break;
        case EN_BOOM: t = en_t[i] < 8 ? S_boom1 : S_boom2; break;
        default: k_sprite(SPR_EN + i, -32, 0, 0, 0); continue;
        }
        k_sprite(SPR_EN + i, en_x[i], en_y[i], t, 8);
    }
    for (i = 0; i < MAX_EB; ++i) k_sprite(SPR_EB + i, eb_on[i] ? eb_x[i] : -32, eb_y[i], S_ebullet, 8);
}

static void title(void) {
    k_hide_sprites(0);
    k_split(0, 0, 0);
    k_scroll(0, 0);
    k_clear_map();
    k_print(12, 6, "YILDIZ MUHAFIZI", 1);
    k_print(7, 10, "GEMIYI YON TUSLARIYLA SUR,", 0);
    k_print(7, 12, "SHIFT ILE ATES ET.", 0);
    k_print(7, 15, "DAIRE   20   OK   30", 2);
    k_print(7, 17, "MAYIN 50 (UC ISABET)", 2);
    k_print(9, 22, "SPACE/ENTER: BASLA ", 1);
    k_sprite(1, 64, 110, S_saucer, 8);
    k_sprite(2, 120, 110, S_dart, 8);
    k_sprite(3, 64, 126, S_mine, 8);
    ses_muzik(mus_baslik);
    do k_wait_frame(); while (!((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A)));
}

void main(void) {
    unsigned char i;
    k_init();
    k_palette(5, pal_uzay, 16);
    k_palette(8, pal_uzay, 16);
    k_palette(0, pal_uzay, 1);
    k_vcopy(TILE_BASE * 32, tiles_data, sizeof(tiles_data));
    k_vcopy(0x2000, sprites_data, sizeof(sprites_data));
    k_seed(0xBEEF);
    for (;;) {
        title();
        k_hide_sprites(0);
        k_clear_map();
        stars();
        ship_x = 40; ship_y = 120; lives = 3; score = 0; wave = 1; invuln = 60; alive = 1;
        spawn_timer = 60; cooldown = 0;
        for (i = 0; i < MAX_EN; ++i) en_type[i] = EN_NONE;
        for (i = 0; i < MAX_SHOTS; ++i) shot_on[i] = 0;
        for (i = 0; i < MAX_EB; ++i) eb_on[i] = 0;
        hud();
        ses_muzik(mus_dalga1);
        ses_efekt(sfx_basla);
        while (alive) {
            k_wait_frame();
            if (((pad[0] | pad[1]) & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) k_menu();
            if ((pad_new[0] | pad_new[1]) & BTN_START) {
                k_print(16, 1, "DURAKLADI", 1);
                ses_ses(4);
                do k_wait_frame(); while (!((pad_new[0] | pad_new[1]) & BTN_START));
                ses_ses(15);
                k_print(16, 1, "         ", 0);
            }
            bgx = (bgx + 1) & 511;
            k_split(14, bgx, 0);
            update();
            draw();
            ++starx;
        }
        ses_muzik(mus_bitti);
        k_print(15, 1, "OYUN BITTI", 3);
        for (i = 0; i < 120; ++i) k_wait_frame();
    }
}
