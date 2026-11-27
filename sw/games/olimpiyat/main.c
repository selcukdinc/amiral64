/* Mahalle Olimpiyati: a 1-4 player party game in turns. Three events:
 *   KOSU      press A and B in turn as fast as you can (100 m)
 *   UZUN ATLAMA  run with A/B, jump with UP before the red line: the closer, the further
 *   REFLEKS   wait for the signal, then press A; pressing early is a foul
 * Every event gives 4/3/2/1 points by place; the most points wins. Each player may use their own pad
 * or pass pad 1 around. */
#include "konsol.h"
#include "art.h"
#include "ses.h"
#include "muzik.h"

#define TRACK_ROW 16
unsigned char nplayers, event_no;
unsigned result[4];                /* this event: lower is better for KOSU/REFLEKS, higher for UZUN ATLAMA */
unsigned char points[4];
unsigned dist, speed;
unsigned char last_btn, cur;
int runner_x;
static const unsigned signal_col = 0xC00;   /* the backdrop flashes red on the signal */

static unsigned char pressed(void) { return pad_new[0] | pad_new[1] | pad_new[cur]; }

static void wait_frames(unsigned char n) { while (n--) k_wait_frame(); }

static void wait_a(void) {
    do k_wait_frame(); while (!(pressed() & (BTN_A | BTN_START)));
}

static void field(void) {
    unsigned char y;
    k_hide_sprites(0);
    k_clear_map();
    for (y = 4; y < 30; ++y) k_fill_tiles(0, y, 40, 1, T_grass, 4);
    k_fill_tiles(0, TRACK_ROW - 1, 40, 1, T_lane, 4);
    k_fill_tiles(0, TRACK_ROW, 40, 3, T_track, 4);
    k_fill_tiles(0, TRACK_ROW + 3, 40, 1, T_lane, 4);
}

static void show_time(unsigned char x, unsigned char y, unsigned frames) {
    /* frames at 60 Hz as seconds.hundredths */
    unsigned s = frames / 60, h = (frames % 60) * 5 / 3;
    k_print_num(x, y, s, 2, 0);
    k_print(x + 2, y, ".", 0);
    k_print_num(x + 3, y, h, 2, 0);
}

static void runner(unsigned char frame_ix) {
    k_sprite(0, runner_x, TRACK_ROW * 8 + 2, frame_ix, 8 + cur);
}

/* alternate A and B: each correct press speeds up, speed fades */
static void mash(void) {
    unsigned char p = pressed();
    if ((p & BTN_A) && last_btn != BTN_A) { speed += 10; last_btn = BTN_A; }
    else if ((p & BTN_B) && last_btn != BTN_B) { speed += 10; last_btn = BTN_B; }
    if (speed > 120) speed = 120;
    if (speed > 1) speed -= 2; else speed = 0;
}

static void turn_title(const char *what) {
    k_fill_tiles(0, 0, 40, 4, ' ', 0);
    k_print(2, 0, what, 1);
    k_print(2, 2, "OYUNCU", 2);
    k_print_num(9, 2, cur + 1, 1, 2);
    k_print(11, 2, "HAZIR: SHIFT", 0);
    wait_a();
    k_fill_tiles(11, 2, 20, 1, ' ', 0);
}

static void event_run(void) {
    unsigned t = 0;
    field();
    /* finish line */
    k_fill_tiles(36, TRACK_ROW, 1, 3, T_line, 4);
    for (cur = 0; cur < nplayers; ++cur) {
        turn_title("KOSU 100 M  SHIFT-CTRL SIRAYLA!");
        dist = 0; speed = 0; t = 0; last_btn = 0; runner_x = 8;
        runner(S_run1);
        ses_dur();
        k_print(18, 2, "3", 3); ses_efekt(sfx_geri); wait_frames(40);
        k_print(18, 2, "2", 3); ses_efekt(sfx_geri); wait_frames(40);
        k_print(18, 2, "1", 3); ses_efekt(sfx_geri); wait_frames(40);
        k_print(18, 2, "KOS!", 1); ses_efekt(sfx_kos); ses_muzik(mus_kosu);
        while (dist < 10000 && t < 3600) {
            k_wait_frame();
            ++t;
            mash();
            dist += speed;
            runner_x = 8 + (int)(dist / 36);
            runner((t >> 3) & 1 ? S_run1 : S_run2);
            if (!(t & 3)) show_time(24, 2, t);
        }
        result[cur] = t;
        show_time(24, 2, t);
        ses_dur();
        ses_efekt(sfx_bitis);
        wait_frames(90);
    }
}

static void event_jump(void) {
    unsigned t, best;
    unsigned char tries, jumped;
    int line_x = 200, d;
    field();
    k_fill_tiles(25, TRACK_ROW, 1, 3, T_line, 4);
    k_fill_tiles(26, TRACK_ROW, 14, 3, T_sand, 4);
    for (cur = 0; cur < nplayers; ++cur) {
        best = 0;
        for (tries = 0; tries < 2; ++tries) {
            turn_title("UZUN ATLAMA SHIFT-CTRL KOS,YUKARI ATLA");
            k_print(30, 2, "DENEME", 0);
            k_print_num(37, 2, tries + 1, 1, 0);
            speed = 0; last_btn = 0; runner_x = 8; jumped = 0; t = 0;
            ses_muzik(mus_atlama);
            while (!jumped && runner_x < line_x + 8) {
                k_wait_frame();
                ++t;
                mash();
                runner_x += speed >> 5;
                runner((t >> 3) & 1 ? S_run1 : S_run2);
                if (pressed() & BTN_UP) { jumped = 1; ses_efekt(sfx_atla); }
            }
            ses_dur();
            d = 0;
            if (!jumped || runner_x > line_x) {
                k_print(18, 3, "FAUL!  ", 3);
                ses_efekt(sfx_faul);
            } else {
                /* jump length: speed, minus what was lost before the line */
                d = (int)speed * 7 - (line_x - runner_x) * 4;
                if (d < 50) d = 50;
                runner(S_jump);
                for (t = 0; t < 30; ++t) { k_wait_frame(); runner_x += d / 120 + 1; runner(S_jump); }
                k_print_num(18, 3, d, 4, 1);
                k_print(22, 3, " CM", 1);
                ses_efekt(sfx_olcum);
            }
            if (d > best) best = d;
            wait_frames(80);
            k_fill_tiles(18, 3, 10, 1, ' ', 0);
        }
        result[cur] = best;
    }
}

static void event_reflex(void) {
    unsigned char t, wait, early;
    field();
    for (cur = 0; cur < nplayers; ++cur) {
        turn_title("REFLEKS  ISARETI BEKLE, SHIFT'E BAS");
        k_print(14, 8, "HAZIR...", 0);
        wait = 60 + (k_rand() & 127) + (k_rand() & 63);
        early = 0;
        ses_muzik(mus_refleks);
        while (wait--) {
            k_wait_frame();
            if (pressed() & BTN_A) { early = 1; break; }
        }
        if (early) {
            k_print(14, 8, "ERKEN! FAUL", 3);
            ses_dur();
            ses_efekt(sfx_faul);
            result[cur] = 999;
        } else {
            k_print(14, 8, "SIMDI!     ", 1);
            k_palette(0, &signal_col, 1);
            ses_dur();
            ses_efekt(sfx_sinyal);
            t = 0;
            do { k_wait_frame(); ++t; } while (!(pressed() & BTN_A) && t < 250);
            k_palette(0, pal_saha, 1);
            result[cur] = (unsigned)t * 50 / 3;     /* milliseconds */
            k_print(14, 10, "MS", 0);
            k_print_num(10, 10, result[cur], 3, 1);
        }
        wait_frames(90);
        k_fill_tiles(0, 8, 40, 3, ' ', 0);
    }
}

/* places: 4, 3, 2, 1 points (ties share the better place) */
static void award(unsigned char higher_better) {
    unsigned char i, j, place;
    for (i = 0; i < nplayers; ++i) {
        place = 0;
        for (j = 0; j < nplayers; ++j)
            if (higher_better ? result[j] > result[i] : result[j] < result[i]) ++place;
        points[i] += 4 - place;
    }
}

static void scoreboard(const char *title) {
    unsigned char i;
    k_hide_sprites(0);
    k_clear_map();
    ses_muzik(mus_baslik);
    k_print(12, 4, title, 1);
    for (i = 0; i < nplayers; ++i) {
        k_print(10, 8 + 2 * i, "OYUNCU", 2);
        k_print_num(17, 8 + 2 * i, i + 1, 1, 2);
        k_print_num(22, 8 + 2 * i, points[i], 2, 0);
        k_print(25, 8 + 2 * i, "PUAN", 0);
    }
    k_print(12, 22, "DEVAM: SHIFT", 0);
    cur = 0;
    wait_a();
}

void main(void) {
    unsigned char i, best;
    k_init();
    k_palette(4, pal_saha, 16);
    k_palette(8, pal_r1, 16); k_palette(9, pal_r2, 16); k_palette(10, pal_r3, 16); k_palette(11, pal_r4, 16);
    k_vcopy(TILE_BASE * 32, tiles_data, sizeof(tiles_data));
    k_vcopy(0x2000, sprites_data, sizeof(sprites_data));
    nplayers = 2;
    for (;;) {
        k_palette(0, pal_saha, 1);
        k_hide_sprites(0);
        k_clear_map();
        k_print(10, 5, "MAHALLE OLIMPIYATI", 1);
        k_print(6, 9, "KOSU, UZUN ATLAMA VE REFLEKS:", 0);
        k_print(6, 11, "SIRAYLA OYNANAN UC YARISMA.", 0);
        k_print(10, 16, "OYUNCU SAYISI:", 2);
        k_print(6, 21, "YUKARI/ASAGI SEC  SPACE/ENTER BASLA", 0);
        ses_muzik(mus_baslik);
        cur = 0;
        for (;;) {
            k_print_num(25, 16, nplayers, 1, 1);
            k_wait_frame();
            if (((pad[0] | pad[1]) & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) k_menu();
            if ((pad_new[0] | pad_new[1]) & BTN_UP && nplayers < 4) { ++nplayers; ses_efekt(sfx_geri); }
            if ((pad_new[0] | pad_new[1]) & BTN_DOWN && nplayers > 1) { --nplayers; ses_efekt(sfx_geri); }
            if ((pad_new[0] | pad_new[1]) & (BTN_START | BTN_A)) break;
        }
        k_seed(0x0A11 + frame_count);
        for (i = 0; i < 4; ++i) points[i] = 0;
        event_no = 1; event_run(); award(0); scoreboard("KOSU SONRASI");
        event_no = 2; event_jump(); award(1); scoreboard("UZUN ATLAMA SONRASI");
        event_no = 3; event_reflex(); award(0); scoreboard("REFLEKS SONRASI");
        best = 0;
        for (i = 1; i < nplayers; ++i) if (points[i] > points[best]) best = i;
        k_clear_map();
        k_print(11, 10, "SAMPIYON: OYUNCU", 1);
        k_print_num(28, 10, best + 1, 1, 1);
        ses_muzik(mus_sampiyon);
        event_no = 4;
        cur = 0;
        wait_a();
    }
}
