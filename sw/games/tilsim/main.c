/* Tilsim: a card roguelike. Every run has a seed (four letters): the same seed deals the same cards, shops
 * and guardians. Each of the 8 stops has three gates: small, big and a guardian with a curse. Reach the
 * gate's target with at most 4 played hands (up to 5 cards each) and 3 discards. A hand scores
 * CHIPS x MULT: the poker hand gives a base (raised by scrolls), every scoring card adds its chips, and
 * the charms (up to 5) add chips, add mult or multiply it. Gold from the gates buys charms and scrolls.
 * Before each gate a map shows the stop's three gates, their targets and the hand levels; the small and
 * the big gate can be skipped for a tag (gold, a scroll, cheaper shop, an extra hand, a charm). On the title
 * the deck is chosen (classic, ember, short, gold, twin) and the seed can be typed in.
 * Pad: Left/Right choose a card, A selects it, Start plays the selected cards, Select discards them (when
 * it is let go), B sorts by rank or suit, Up goes to the charms (read them; in the shop A sells). Hold A to
 * speed up the scoring. Esc, or Select + Start, goes back to the menu. */
#include "konsol.h"
#include "bigtext.h"
#include "art.h"

#define PAL_CRED 4          /* 4 red card, 5 black card; +2 selected; +4 glowing */
#define PAL_BACK 10
#define PAL_ICON 11
#define PAL_CHIPS 12
#define PAL_MULT 13
#define PAL_BIG 14          /* big title; also white floating numbers */
#define PAL_UI 15

#define SAVE ((unsigned char *)0x02E4)   /* 'T', best stop reached */

typedef unsigned char u8;
typedef unsigned long u32;

/* ---------------------------------------------------------------- tables */
enum { HIGH, PAIR, TWOPAIR, THREE, STRAIGHT, FLUSH, FULL, FOUR, SFLUSH };
static const char *const hand_name[9] = { "YUKSEK KART", "CIFT", "IKI CIFT", "UCLU", "KENT", "RENK", "FUL", "KARE", "KENT RENK" };
static const u8 base_chips[9] = { 5, 12, 22, 30, 35, 40, 45, 70, 110 };
static const u8 base_mult[9]  = { 1, 2, 2, 3, 4, 4, 5, 7, 9 };
static const u8 lv_chips[9]   = { 10, 15, 20, 20, 30, 15, 25, 30, 40 };
static const u8 lv_mult[9]    = { 1, 1, 1, 2, 3, 2, 2, 3, 4 };
static const u32 stop_target[8] = { 300, 700, 1600, 3600, 8000, 15000, 26000, 42000UL };
static const char *const suit_name[4] = { "KUPA", "KARO", "SINEK", "MACA" };
static const char *const gate_name[3] = { "KUCUK KAPI", "BUYUK KAPI", "BEKCI" };
static const char seed_letters[17] = "BCDFGHKLMNPRSTVZ";

#define NDECK 5
enum { D_KLASIK, D_KOR, D_KISA, D_ALTIN, D_IKIZ };
static const char *const deck_name[NDECK] = { "KLASIK", "KOR", "KISA", "ALTIN", "IKIZ" };
static const char *const deck_d1[NDECK] = { "52 KART, EKSTRA YOK", "ATES TILSIMIYLA BASLA", "2 3 4 YOK: 40 KART",
                                            "+6 ALTIN ILE BASLA", "YALNIZ KUPA VE MACA" };
static const char *const deck_d2[NDECK] = { "DENGELI BIR BASLANGIC", "ILK ELLER DAHA GUCLU", "KENT VE FUL KOLAY",
                                            "FAIZ SINIRI 10 ALTIN", "IKISER: RENK KOLAY" };
static const unsigned deck_back[NDECK][3] = { { 0x339, 0x55C, 0x99F }, { 0x822, 0xC44, 0xF96 }, { 0x263, 0x494, 0x9E9 },
                                              { 0x850, 0xCA2, 0xFE8 }, { 0x528, 0x84B, 0xC9F } };

#define NTAG 6
static const char *const tag_d1[NTAG] = { "+10 ALTIN", "PARSOMEN", "3 BEDAVA", "PAZARDA", "SONRAKI", "HEDIYE" };
static const char *const tag_d2[NTAG] = { "", "", "YENILEME", "YARI FIYAT", "KAPI +1 EL", "TILSIM" };

#define NCHARM 24
enum { C_ATES, C_KUPA, C_KARO, C_SINEK, C_MACA, C_IKIZ, C_UCLU, C_YOL, C_RENK, C_SARAY, C_AS, C_SABIR, C_FIDAN,
       C_ZAR, C_AYNA, C_SONSOZ, C_HAZINE, C_KESE, C_UCLEM, C_KARTAL, C_RUZGAR, C_CEP, C_YANKI, C_KOR };
static const char *const ch_name[NCHARM] = { "ATES", "KUPA", "KARO", "SINEK", "MACA", "IKIZ", "UCLU", "YOL", "RENK",
    "SARAY", "AS", "SABIR", "FIDAN", "ZAR", "AYNA", "SONSOZ", "HAZINE", "KESE", "UCLEM", "KARTAL", "RUZGAR", "CEP", "YANKI", "KOR" };
static const char *const ch_d1[NCHARM] = { "+4 CARPAN", "KUPA KART", "KARO KART", "SINEK KART", "MACA KART",
    "CIFTLI EL", "UCLULU EL", "KENT", "RENK", "J Q K KART", "AS KART", "KALAN ATIS", "CIFTLI EL", "1/4 SANSLA",
    "TEK RENK EL", "SON ELDE", "ALTIN/2", "KAPI SONU", "3 KART EL", "+1 EL", "+1 ATIS", "+1 KART", "HER KAPI", "+20 CARPAN" };
static const char *const ch_d2[NCHARM] = { "", "+3 CARPAN", "+3 CARPAN", "+3 CARPAN", "+3 CARPAN", "+8 CARPAN",
    "+12 CARPAN", "+12 CARPAN", "+10 CARPAN", "+30 CIP", "+20C +4X", "BASI +15C", "+1 KALICI", "X4 CARPAN", "X2 CARPAN",
    "X3 CARPAN", "+CARPAN", "+4 ALTIN", "X2 CARPAN", "HER KAPIDA", "HER KAPIDA", "ELDE", "+X0.5 BUYUR", "ATIS YOK" };
static const u8 ch_cost[NCHARM] = { 4, 5, 5, 5, 5, 4, 5, 5, 5, 5, 5, 4, 6, 6, 7, 7, 6, 5, 6, 6, 4, 7, 8, 7 };
static const u8 ch_icon[NCHARM] = { T_i_flame, T_i_ih, T_i_id, T_i_ic, T_i_is, T_i_cards, T_i_cards, T_i_cards, T_i_cards,
    T_i_chip, T_i_chip, T_i_clock, T_i_sprout, T_i_dice, T_i_eye, T_i_eye, T_i_coin, T_i_coin, T_i_eye, T_i_cards,
    T_i_clock, T_i_cards, T_i_sprout, T_i_flame };

#define NBOSS 8
static const char *const boss_name[NBOSS] = { "DAR KAPI", "SESSIZ", "SOLUK", "YUZSUZ", "KISA EL", "TEKRARSIZ", "YARIM", "AGIR" };
static const char *const boss_desc[NBOSS] = { "EL SAYISI -1", "ATIS YOK", "CIP VERMEZ", "J Q K SAYILMAZ",
    "ELDE 2 KART AZ", "AYNI EL 0 PUAN", "CARPAN YARIYA", "HEDEF X2.5" };

/* ---------------------------------------------------------------- state */
unsigned seed;
static unsigned rs_deck, rs_shop, rs_luck;
u8 stop_, gate, boss[8], boss_suit;
u8 deck[52], deck_pos, deck_n, deck_kind;
u8 gate_state[3];                 /* 0 ahead, 1 opened, 2 skipped */
u8 tag_kind[2], tag_arg[2];       /* the skip tags of the small and the big gate */
static u8 free_rerolls, half_price, extra_hand;
u8 hand[10], hand_n, hand_size, selmask;
u8 charm[5], charm_val[5], ncharm;
u8 hlevel[9], played_types;
u8 gold, hands_left, discards_left;
u32 score, target;
static u8 sort_suit, cursor, in_charms, t8;
static u32 chips, multq;          /* mult in quarters */
static u32 best_hand;
static u8 fast, flash_k = 255;

/* ---------------------------------------------------------------- rng (xorshift16, one state per stream) */
static u8 rnd(unsigned *s) {
    unsigned x = *s;
    x ^= x << 7; x ^= x >> 9; x ^= x << 8;
    *s = x ? x : 0x1234;
    return (u8)(x ^ (x >> 8));
}
static u8 rnd_n(unsigned *s, u8 n) { return rnd(s) % n; }

/* ---------------------------------------------------------------- screen helpers */
static void put(u8 x, u8 y, u8 tile, u8 attr) {
    k_vaddr(0x4000 + ((unsigned)y << 7) + ((unsigned)x << 1));
    PPU_VDATA = tile; PPU_VDATA = attr;
}
static void blank(u8 x, u8 y, u8 w, u8 h) { k_fill_tiles(x, y, w, h, ' ', 0); }
static void text(u8 x, u8 y, const char *s, u8 pal) { k_print(x, y, s, pal); }

static void num(u8 x, u8 y, u32 v, u8 pal) {       /* left aligned, two spaces after (erases a longer old value) */
    char b[11], o[13];
    u8 i = 10, k = 0;
    b[10] = 0;
    do { b[--i] = '0' + (u8)(v % 10); v /= 10; } while (v && i);
    while (b[i]) o[k++] = b[i++];
    o[k++] = ' '; o[k++] = ' '; o[k] = 0;
    k_print(x, y, o, pal);
}

static void panel(u8 x, u8 y, u8 w, u8 h) {
    u8 i, j, tile, wl = w - 1, hl = h - 1;     /* (see blok: cc65 compare bug with w - 1) */
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

static void big_icon(u8 x, u8 y, u8 tile, u8 pal) {
    put(x, y, tile, pal); put(x + 1, y, tile + 1, pal);
    put(x, y + 1, tile + 2, pal); put(x + 1, y + 1, tile + 3, pal);
}

/* a card: 3x4 tiles. look 0 normal, 1 selected, 2 glowing */
#define RANK(c) ((c) & 15)
#define SUIT(c) ((c) >> 4)
static void draw_card(u8 x, u8 y, u8 c, u8 look) {
    u8 s = SUIT(c), r = RANK(c), p = PAL_CRED + (s >= 2) + look * 2, f;
    u8 a1 = T_eC, a2 = T_eR, b1 = T_pbh + s, b2 = T_eR;
    if (r >= 9 && r <= 11) {             /* J Q K: a portrait over the centre and right columns */
        f = T_fJ0 + (r - 9) * 4;
        a1 = f; a2 = f + 1; b1 = f + 2; b2 = f + 3;
    }
    k_vaddr(0x4000 + ((unsigned)y << 7) + ((unsigned)x << 1));
    PPU_VDATA = T_r2 + r; PPU_VDATA = p; PPU_VDATA = T_eT; PPU_VDATA = p; PPU_VDATA = T_eTR; PPU_VDATA = p;
    k_vaddr(0x4000 + ((unsigned)(y + 1) << 7) + ((unsigned)x << 1));
    PPU_VDATA = T_psh + s; PPU_VDATA = p; PPU_VDATA = a1; PPU_VDATA = p; PPU_VDATA = a2; PPU_VDATA = p;
    k_vaddr(0x4000 + ((unsigned)(y + 2) << 7) + ((unsigned)x << 1));
    PPU_VDATA = T_eL; PPU_VDATA = p; PPU_VDATA = b1; PPU_VDATA = p; PPU_VDATA = b2; PPU_VDATA = p;
    k_vaddr(0x4000 + ((unsigned)(y + 3) << 7) + ((unsigned)x << 1));
    PPU_VDATA = T_eBL; PPU_VDATA = p; PPU_VDATA = T_eB; PPU_VDATA = p; PPU_VDATA = T_eBR; PPU_VDATA = p;
}

static void draw_back(u8 x, u8 y) {
    u8 i, j;
    for (j = 0; j < 4; ++j) for (i = 0; i < 3; ++i) put(x + i, y + j, T_back, PAL_BACK);
}

static void set_back(u8 kind) {           /* card back colours of a deck (palette entries 1, 2, 5) */
    k_vaddr(0x5000 + PAL_BACK * 32 + 2);
    PPU_VDATA = (u8)deck_back[kind][0]; PPU_VDATA = deck_back[kind][0] >> 8;
    PPU_VDATA = (u8)deck_back[kind][1]; PPU_VDATA = deck_back[kind][1] >> 8;
    k_vaddr(0x5000 + PAL_BACK * 32 + 10);
    PPU_VDATA = (u8)deck_back[kind][2]; PPU_VDATA = deck_back[kind][2] >> 8;
}

/* ---------------------------------------------------------------- sound */
static void tone(u8 ch, unsigned p, u8 ctl, u8 env) { k_tone(ch, p, ctl, env); }
static void blip(u8 step) { tone(0, 0x0C0 - step * 10, 0xA8, 0x01); }
static void clink(void) { tone(1, 0x050, 0x6A, 0x01); }
static void thud(void) { k_tone(3, 0x030, 0x8A, 0x02); }

/* ---------------------------------------------------------------- floating numbers (sprites) */
static void charm_name(u8 k, u8 pal);
static void float_num(int x, int y, char prefix, u32 v, u8 pal) {
    u8 n = 0, i, f, frames = fast ? 8 : 16;
    u8 d[7];
    if (flash_k != 255) charm_name(flash_k, 1);          /* the charm doing it lights up */
    do { d[n++] = (u8)(v % 10); v /= 10; } while (v && n < 6);
    for (f = 0; f < frames; ++f) {
        u8 sp = 40;
        int yy = y - f;
        if (prefix) k_sprite(sp++, x, yy, prefix == '+' ? S_dplus : prefix == 'x' ? S_dx : S_dgold, pal);
        for (i = n; i; --i) k_sprite(sp++, x + (prefix ? 9 : 0) + (n - i) * 9, yy, S_d0 + d[i - 1], pal);
        k_hide_sprites(sp);
        k_wait_frame();
        if (pad[0] & BTN_A) fast = 1;
    }
    k_hide_sprites(40);
    if (flash_k != 255) charm_name(flash_k, 0);
}

/* ---------------------------------------------------------------- deck and hand */
static void shuffle(void) {
    u8 i, j, x, r, s;
    deck_n = 0;
    for (i = 0; i < 52; ++i) {
        r = i % 13; s = i / 13;
        if (deck_kind == D_KISA && r < 3) continue;          /* no 2 3 4 */
        if (deck_kind == D_IKIZ) s = (s & 1) ? 3 : 0;        /* hearts and spades, twice each */
        deck[deck_n++] = r | s << 4;
    }
    rs_deck = seed ^ ((unsigned)(stop_ * 3 + gate + 1) * 0x9E37u);
    for (i = deck_n - 1; i; --i) { j = rnd_n(&rs_deck, i + 1); x = deck[i]; deck[i] = deck[j]; deck[j] = x; }
    deck_pos = 0;
}

static void sort_hand(void) {
    u8 i, j, a, b, ka, kb, sa, sb;
    for (i = 1; i < hand_n; ++i)
        for (j = i; j; --j) {
            a = hand[j - 1]; b = hand[j];
            ka = sort_suit ? SUIT(a) * 16 + RANK(a) : RANK(a) * 4 + SUIT(a);
            kb = sort_suit ? SUIT(b) * 16 + RANK(b) : RANK(b) * 4 + SUIT(b);
            if (ka >= kb) break;
            hand[j - 1] = b; hand[j] = a;
            sa = (selmask >> (j - 1)) & 1; sb = (selmask >> j) & 1;
            selmask &= ~(3 << (j - 1));
            selmask |= sb << (j - 1) | sa << j;
        }
}

static void draw_hand(void);
static void refill(void) {
    while (hand_n < hand_size && deck_pos < deck_n) hand[hand_n++] = deck[deck_pos++];
    sort_hand();
}

static u8 count_sel(void) {
    u8 i, n = 0;
    for (i = 0; i < hand_n; ++i) if (selmask >> i & 1) ++n;
    return n;
}

static void remove_selected(void) {
    u8 i, k = 0;
    for (i = 0; i < hand_n; ++i) if (!(selmask >> i & 1)) hand[k++] = hand[i];
    hand_n = k; selmask = 0;
}

/* ---------------------------------------------------------------- charms */
static u8 has(u8 id) {
    u8 i;
    for (i = 0; i < ncharm; ++i) if (charm[i] == id) return 1;
    return 0;
}

/* ---------------------------------------------------------------- hand evaluation */
static u8 play_cards[5], play_n, scoring;      /* scoring: bitmask over play_cards */

static u8 evaluate(void) {
    u8 cnt[13], sc[4], i, r, pairs = 0, three = 0, four = 0, flush = 0, straight = 0, distinct = 0, lo = 13, hi = 0, type;
    for (i = 0; i < 13; ++i) cnt[i] = 0;
    for (i = 0; i < 4; ++i) sc[i] = 0;
    for (i = 0; i < play_n; ++i) { ++cnt[RANK(play_cards[i])]; ++sc[SUIT(play_cards[i])]; }
    for (r = 0; r < 13; ++r) {
        if (!cnt[r]) continue;
        ++distinct;
        if (r < lo) lo = r;
        if (r > hi) hi = r;
        if (cnt[r] == 2) ++pairs;
        if (cnt[r] == 3) three = 1;
        if (cnt[r] == 4) four = 1;
    }
    if (play_n == 5) {
        for (i = 0; i < 4; ++i) if (sc[i] == 5) flush = 1;
        if (distinct == 5 && (hi - lo == 4 || (cnt[12] && cnt[0] && cnt[1] && cnt[2] && cnt[3]))) straight = 1;
    }
    if (straight && flush) type = SFLUSH;
    else if (four) type = FOUR;
    else if (three && pairs) type = FULL;
    else if (flush) type = FLUSH;
    else if (straight) type = STRAIGHT;
    else if (three) type = THREE;
    else if (pairs == 2) type = TWOPAIR;
    else if (pairs) type = PAIR;
    else type = HIGH;
    scoring = 0;
    for (i = 0; i < play_n; ++i) {
        r = RANK(play_cards[i]);
        if (type == SFLUSH || type == FLUSH || type == STRAIGHT || type == FULL) scoring |= 1 << i;
        else if (type != HIGH && cnt[r] >= 2) scoring |= 1 << i;
        else if (type == HIGH && r == hi) { scoring = 1 << i; }
    }
    if (type == HIGH) {            /* only one card: the first of the highest rank */
        for (i = 0; i < play_n; ++i) if (scoring >> i & 1) { scoring = 1 << i; break; }
    }
    return type;
}

static void collect_selected(void) {
    u8 i;
    play_n = 0;
    for (i = 0; i < hand_n && play_n < 5; ++i) if (selmask >> i & 1) play_cards[play_n++] = hand[i];
}

static u8 card_chips(u8 c) {
    u8 r = RANK(c);
    return r < 9 ? r + 2 : r == 12 ? 11 : 10;
}

/* ---------------------------------------------------------------- panels */
static void draw_top(void) {
    u8 b = boss[stop_ & 7];
    char sd[5];
    blank(0, 0, 40, 2);
    text(1, 0, "DURAK", 1);
    k_print_num(7, 0, stop_ + 1, 1, 0);
    text(8, 0, "/8", 0);
    text(12, 0, gate_name[gate], gate == 2 ? 3 : 2);
    sd[0] = seed_letters[seed >> 12]; sd[1] = seed_letters[(seed >> 8) & 15];
    sd[2] = seed_letters[(seed >> 4) & 15]; sd[3] = seed_letters[seed & 15]; sd[4] = 0;
    text(29, 0, "TOHUM", 0); text(35, 0, sd, 1);
    if (gate == 2) {
        text(1, 1, boss_name[b], 3);
        if (b == 2) { text(12, 1, suit_name[boss_suit], 3); text(18, 1, boss_desc[b], 0); }
        else text(12, 1, boss_desc[b], 0);
    }
}

static void draw_charms(void) {
    u8 i, x;
    panel(0, 2, 40, 5);
    for (i = 0; i < 5; ++i) {
        x = 2 + i * 8;
        if (i < ncharm) {
            big_icon(x, 3, ch_icon[charm[i]], PAL_ICON);
            text(x - 1, 5, ch_name[charm[i]], in_charms && cursor == i ? 1 : 0);
            if (charm[i] == C_FIDAN || charm[i] == C_YANKI) {
                put(x + 2, 3, '+', PAL_UI); k_print_num(x + 2, 4, charm_val[i], 1, 2);
            }
        } else text(x, 4, "--", 0);
    }
}

static void charm_name(u8 k, u8 pal) { text(1 + k * 8, 5, ch_name[charm[k]], pal); }

static void charm_desc(void) {
    u8 id;
    blank(0, 7, 40, 1);
    if (!in_charms || cursor >= ncharm) return;
    id = charm[cursor];
    text(2, 7, ch_d1[id], 2);
    text(3 + big_len(ch_d1[id]), 7, ch_d2[id], 1);
}

static void draw_info(void) {
    panel(0, 8, 14, 8);
    text(1, 9, "HEDEF", 0); num(7, 9, target, 3);
    text(1, 10, "SKOR", 0); num(7, 10, score, 1);
    text(1, 12, "EL", 2); k_print_num(4, 12, hands_left, 1, 0);
    text(6, 12, "ATIS", 3); k_print_num(11, 12, discards_left, 1, 0);
    text(1, 14, "ALTIN", 1); num(7, 14, gold, 0);
}

static void show_calc(u8 type, u8 lv) {
    blank(16, 9, 22, 5);
    if (type == 255) { text(16, 10, "KART SEC: EN COK 5", 0); return; }
    text(16, 9, hand_name[type], 1);
    text(31, 9, "SV", 0); k_print_num(33, 9, lv, 1, 0);
    text(16, 11, "CIP", 2); num(20, 11, chips, 2);
    text(26, 11, "X", 0);
    text(28, 11, "CARP", 3); num(33, 11, multq >> 2, 3);
}

static void nums(void) {
    num(20, 11, chips, 2);
    num(33, 11, multq >> 2, 3);
}

static void preview(void) {
    u8 type;
    collect_selected();
    if (!play_n) { show_calc(255, 0); return; }
    type = evaluate();
    chips = base_chips[type] + (unsigned)(hlevel[type] - 1) * lv_chips[type];
    multq = (u32)(base_mult[type] + (hlevel[type] - 1) * lv_mult[type]) << 2;
    show_calc(type, hlevel[type]);
}

static void draw_hand(void) {
    u8 i, x, up;
    blank(2, 17, 36, 6);
    for (i = 0; i < hand_n; ++i) {
        x = 3 + i * 4 + (hand_n < 8 ? (8 - hand_n) * 2 : 0);
        up = selmask >> i & 1;
        draw_card(x, up ? 17 : 18, hand[i], up);
    }
    text(1, 24, "DESTE", 0); k_print_num(7, 24, deck_n - deck_pos, 2, 0);
}

static int card_x(u8 i) { return (3 + i * 4 + (hand_n < 8 ? (8 - hand_n) * 2 : 0)) * 8; }

static void hints(void) {
    blank(0, 26, 40, 2);
    text(1, 26, "SHIFT SEC  SPACE OYNA  TAB AT", 0);
    text(1, 27, "CTRL SIRALA  YUKARI TILSIMLAR  ESC MENU", 0);
}

static void draw_all(void) {
    draw_top(); draw_charms(); charm_desc(); draw_info(); preview(); draw_hand(); hints();
}

/* ---------------------------------------------------------------- scoring */
static void add_mult(u8 n, int x, int y) { multq += (u32)n << 2; nums(); float_num(x, y, '+', n, PAL_MULT); }
static void times_mult(u8 num4, int x, int y) {      /* multiply by num4/4 */
    multq = multq * num4 >> 2;
    nums();
    float_num(x, y, 'x', num4 >> 2, PAL_MULT);
}
static void add_chips(unsigned n, int x, int y) { chips += n; nums(); float_num(x, y, '+', n, PAL_CHIPS); }

static u32 score_hand(void) {
    u8 type, i, c, r, s, b = gate == 2 ? boss[stop_ & 7] : 255, step = 0, k, id, red = 0, black = 0;
    int cx, cy = 17 * 8 - 14, chx;
    u32 total;
    type = evaluate();
    chips = base_chips[type] + (unsigned)(hlevel[type] - 1) * lv_chips[type];
    multq = (u32)(base_mult[type] + (hlevel[type] - 1) * lv_mult[type]) << 2;
    show_calc(type, hlevel[type]);
    /* the played cards go to the middle row, face up */
    blank(2, 17, 36, 6);
    for (i = 0; i < play_n; ++i) draw_card(10 + i * 4, 18, play_cards[i], scoring >> i & 1 ? 2 : 0);
    for (i = 0; i < 20; ++i) k_wait_frame();
    for (i = 0; i < play_n; ++i) {
        c = play_cards[i]; r = RANK(c); s = SUIT(c);
        if (s < 2) ++red; else ++black;
        if (!(scoring >> i & 1)) continue;
        cx = (10 + i * 4) * 8 + 2;
        if (b == 2 && s == boss_suit) { thud(); continue; }
        if (b == 3 && r >= 9 && r <= 11) { thud(); continue; }
        blip(step++);
        add_chips(card_chips(c), cx, cy);
        for (k = 0; k < ncharm; ++k) {
            id = charm[k]; flash_k = k;
            if (id >= C_KUPA && id <= C_MACA && s == id - C_KUPA) { blip(step++); add_mult(3, cx, cy); }
            if (id == C_SARAY && r >= 9 && r <= 11) { blip(step++); add_chips(30, cx, cy); }
            if (id == C_AS && r == 12) { blip(step++); add_chips(20, cx, cy); add_mult(4, cx, cy); }
        }
        flash_k = 255;
    }
    /* charms that look at the whole hand, left to right */
    for (k = 0; k < ncharm; ++k) {
        id = charm[k]; flash_k = k;
        chx = (2 + k * 8) * 8 - 4; cy = 4 * 8;
        switch (id) {
        case C_ATES: add_mult(4, chx, cy); break;
        case C_KOR: add_mult(20, chx, cy); break;
        case C_IKIZ: if (type == PAIR || type == TWOPAIR || type == THREE || type == FULL || type == FOUR) add_mult(8, chx, cy); break;
        case C_UCLU: if (type == THREE || type == FULL || type == FOUR) add_mult(12, chx, cy); break;
        case C_YOL: if (type == STRAIGHT || type == SFLUSH) add_mult(12, chx, cy); break;
        case C_RENK: if (type == FLUSH || type == SFLUSH) add_mult(10, chx, cy); break;
        case C_SABIR: if (discards_left) add_chips((unsigned)discards_left * 15, chx, cy); break;
        case C_FIDAN:
            if (type == PAIR || type == TWOPAIR || type == THREE || type == FULL || type == FOUR) { ++charm_val[k]; draw_charms(); }
            if (charm_val[k]) add_mult(charm_val[k], chx, cy);
            break;
        case C_ZAR: if (rnd_n(&rs_luck, 4) == 0) times_mult(16, chx, cy); break;
        case C_AYNA: if (!red || !black) times_mult(8, chx, cy); break;
        case C_SONSOZ: if (hands_left == 1) times_mult(12, chx, cy); break;
        case C_HAZINE: if (gold >= 2) add_mult(gold >> 1, chx, cy); break;
        case C_UCLEM: if (play_n == 3) times_mult(8, chx, cy); break;
        case C_YANKI: if (charm_val[k]) times_mult(4 + charm_val[k] * 2, chx, cy); break;
        }
    }
    flash_k = 255;
    if (b == 6) { multq >>= 1; thud(); nums(); }
    if (b == 5 && (played_types >> type & 1)) { chips = 0; thud(); nums(); }
    played_types |= 1 << type;
    if (multq < 4) multq = 4;
    total = chips * multq >> 2;
    if (total > best_hand) best_hand = total;
    blank(16, 13, 22, 1);
    text(16, 13, "=", 0); num(18, 13, total, 1);
    tone(0, 0x060, 0xAF, 0x03);
    for (i = 0; i < (fast ? 15 : 40); ++i) k_wait_frame();
    return total;
}

/* score counts up to its new value */
static void count_up(u32 add) {
    u32 goal = score + add, step = add / 24 + 1;
    while (score < goal) {
        score = score + step > goal ? goal : score + step;
        num(7, 10, score, 1);
        if ((score & 3) == 0) clink();
        k_wait_frame();
    }
    k_seg_number(score > 9999 ? 9999 : (unsigned)score);
}

/* ---------------------------------------------------------------- gate */
static void sparks(u8 frames) {
    u8 i, f;
    for (f = 0; f < frames; ++f) {
        for (i = 0; i < 16; ++i) k_sprite(i, 40 + rnd_n(&rs_luck, 240), 40 + rnd_n(&rs_luck, 150), f & 2 ? S_spark : S_dplus, PAL_BIG);
        k_wait_frame();
    }
    k_hide_sprites(0);
}

/* returns 1 when the gate is opened */
static u32 gate_target(u8 g) {
    u32 v = stop_target[stop_ & 7];
    if (stop_ >= 8) v <<= (stop_ - 7);
    if (g == 1) v = v * 3 / 2;
    if (g == 2) v = boss[stop_ & 7] == 7 ? v * 5 / 2 : v * 2;
    return v;
}

static u8 play_gate(void) {
    u8 p, held, b = gate == 2 ? boss[stop_ & 7] : 255, i, sel_armed = 0;
    target = gate_target(gate);
    hands_left = 4 + has(C_KARTAL) - (b == 0) + extra_hand;
    extra_hand = 0;
    discards_left = (b == 1 || has(C_KOR)) ? 0 : 3 + has(C_RUZGAR);
    hand_size = 8 + has(C_CEP) - (b == 4 ? 2 : 0);
    if (hand_size > 10) hand_size = 10;
    score = 0; selmask = 0; hand_n = 0; played_types = 0; cursor = 0; in_charms = 0;
    shuffle();
    refill();
    k_clear_map();
    draw_all();
    /* deal: the deck slides in */
    for (i = 0; i < 8; ++i) { blip(i); k_wait_frame(); k_wait_frame(); }
    for (;;) {
        k_wait_frame(); ++t8;
        p = pad_new[0] | pad_new[1];
        held = pad[0] | pad[1];
        fast = 0;
        /* cursor sprite */
        if (in_charms) k_sprite(0, (2 + cursor * 8) * 8, 6 * 8 + 2 + (t8 >> 3 & 1), S_arrow, PAL_BIG);
        else k_sprite(0, card_x(cursor) + 4, 22 * 8 + 2 + (t8 >> 3 & 1), S_arrow, PAL_BIG);
        if (p & BTN_LEFT && cursor) { --cursor; blip(0); if (in_charms) { draw_charms(); charm_desc(); } }
        if (p & BTN_RIGHT) {
            if (cursor + 1 < (in_charms ? ncharm : hand_n)) { ++cursor; blip(0); if (in_charms) { draw_charms(); charm_desc(); } }
        }
        if (p & BTN_UP && !in_charms && ncharm) { in_charms = 1; cursor = 0; draw_charms(); charm_desc(); }
        if (p & BTN_DOWN && in_charms) { in_charms = 0; cursor = 0; draw_charms(); charm_desc(); }
        if (in_charms) continue;
        if (p & BTN_B) { sort_suit ^= 1; sort_hand(); draw_hand(); }
        if (p & BTN_A && cursor < hand_n) {
            if (selmask >> cursor & 1) selmask &= ~(1 << cursor);
            else if (count_sel() < 5) selmask |= 1 << cursor;
            blip(count_sel());
            draw_hand(); preview();
        }
        /* Select discards when it is let go, so Select + Start can mean "menu" */
        if (p & BTN_SELECT) sel_armed = 1;
        if ((held & BTN_SELECT) && (p & BTN_START)) k_menu();
        if (sel_armed && !(held & BTN_SELECT)) {
            sel_armed = 0;
            if (!selmask || !discards_left) { thud(); continue; }
            --discards_left;
            thud();
            remove_selected(); refill();
            draw_hand(); draw_info(); preview();
            if (cursor >= hand_n) cursor = hand_n - 1;
        }
        if (p & BTN_START && selmask && !(held & BTN_SELECT)) {
            u32 got;
            k_hide_sprites(0);
            collect_selected();
            got = score_hand();
            count_up(got);
            --hands_left;
            remove_selected(); refill();
            draw_info();
            if (score >= target) return 1;
            if (!hands_left || !hand_n) return 0;
            draw_hand(); preview();
            if (cursor >= hand_n) cursor = hand_n - 1;
        }
    }
}

/* ---------------------------------------------------------------- shop */
static u8 offer[3], offer_type[3], sold[3];      /* type 0 charm, 1 scroll (hand type in offer) */
static void roll_offers(void) {
    u8 i, id, tries;
    for (i = 0; i < 3; ++i) {
        sold[i] = 0;
        if (i == 2) { offer_type[i] = 1; offer[i] = rnd_n(&rs_shop, 9); continue; }
        offer_type[i] = 0;
        for (tries = 0; tries < 20; ++tries) {
            id = rnd_n(&rs_shop, NCHARM);
            if (!has(id) && (i == 0 || offer[0] != id)) break;
        }
        offer[i] = id;
    }
}
static u8 price(u8 i) {
    u8 v = offer_type[i] ? 3 : ch_cost[offer[i]];
    return half_price ? (v + 1) >> 1 : v;
}

static void draw_offer(u8 i, u8 hi) {
    u8 x = 1 + i * 13;
    panel(x, 9, 12, 12);
    if (sold[i]) { text(x + 3, 14, "SATILDI", 0); return; }
    if (offer_type[i]) {
        big_icon(x + 5, 10, T_i_cards, PAL_ICON);
        text(x + 1, 13, "PARSOMEN", 2);
        text(x + 1, 15, hand_name[offer[i]], 1);
        text(x + 1, 16, "SEVIYE", 0);
        k_print_num(x + 8, 16, hlevel[offer[i]] + 1, 1, 1);
    } else {
        big_icon(x + 5, 10, ch_icon[offer[i]], PAL_ICON);
        text(x + 1, 13, ch_name[offer[i]], 1);
        text(x + 1, 15, ch_d1[offer[i]], 2);
        text(x + 1, 16, ch_d2[offer[i]], 0);
    }
    text(x + 4, 19, "$", 1); k_print_num(x + 5, 19, price(i), 1, hi ? 1 : 0);
}

static void shop(void) {
    u8 p, i, sel = 0, mode = 0, reroll = 2;      /* mode 0 offers, 1 my charms (sell) */
    if (free_rerolls) reroll = 0;
    roll_offers();
    k_clear_map();
    draw_top();
    text(17, 1, "PAZAR", 1);
    draw_charms();
    for (i = 0; i < 3; ++i) draw_offer(i, 0);
    text(1, 23, "ALTIN", 1);
    blank(0, 25, 40, 3);
    text(1, 25, "SHIFT AL TAB YENILE $", 0);
    text(1, 26, "YUKARI SAT  SPACE DEVAM", 0);
    for (;;) {
        num(7, 23, gold, 0);
        k_print_num(22, 25, reroll, 1, 1);
        k_wait_frame(); ++t8;
        if (mode) k_sprite(0, (2 + sel * 8) * 8, 6 * 8 + 2 + (t8 >> 3 & 1), S_arrow, PAL_BIG);
        else k_sprite(0, (1 + sel * 13) * 8 + 44, 21 * 8 + 1 + (t8 >> 3 & 1), S_arrow, PAL_BIG);
        p = pad_new[0] | pad_new[1];
        if (p & BTN_LEFT && sel) { --sel; blip(0); }
        if (p & BTN_RIGHT && sel + 1 < (mode ? ncharm : 3)) { ++sel; blip(0); }
        if (p & BTN_UP && ncharm && !mode) { mode = 1; sel = 0; }
        if (p & BTN_DOWN && mode) { mode = 0; sel = 0; in_charms = 0; draw_charms(); charm_desc(); }
        if (mode) { in_charms = 1; cursor = sel; if (p & (BTN_LEFT | BTN_RIGHT | BTN_UP)) { draw_charms(); charm_desc(); } }
        if (p & BTN_A) {
            if (mode && sel < ncharm) {          /* sell */
                u8 v = ch_cost[charm[sel]] >> 1;
                gold += v ? v : 1;
                for (i = sel; i + 1 < ncharm; ++i) { charm[i] = charm[i + 1]; charm_val[i] = charm_val[i + 1]; }
                --ncharm;
                if (sel && sel >= ncharm) --sel;
                if (!ncharm) mode = 0;
                clink(); draw_charms(); charm_desc();
            } else if (!mode && !sold[sel] && gold >= price(sel) && (offer_type[sel] || ncharm < 5)) {
                gold -= price(sel);
                if (offer_type[sel]) ++hlevel[offer[sel]];
                else { charm[ncharm] = offer[sel]; charm_val[ncharm] = 0; ++ncharm; }
                sold[sel] = 1;
                k_sfx(SFX_PICKUP);
                draw_charms(); draw_offer(sel, 0);
            } else thud();
        }
        if (p & BTN_SELECT && gold >= reroll) {
            gold -= reroll;
            if (free_rerolls) { if (--free_rerolls == 0) reroll = 2; } else ++reroll;
            roll_offers();
            for (i = 0; i < 3; ++i) draw_offer(i, 0);
            clink();
        }
        if (p & BTN_START) break;
    }
    k_hide_sprites(0);
    in_charms = 0;
    half_price = 0; free_rerolls = 0;
}

/* ---------------------------------------------------------------- the stop's map: play or skip a gate */
static void roll_tags(void) {
    unsigned r = seed ^ ((unsigned)(stop_ + 1) * 0x7F4Bu);
    u8 g;
    for (g = 0; g < 2; ++g) { tag_kind[g] = rnd_n(&r, NTAG); tag_arg[g] = rnd_n(&r, 9); }
}

static void apply_tag(u8 g) {
    u8 k = tag_kind[g], a = tag_arg[g];
    switch (k) {
    case 0: gold += 10; break;
    case 1: ++hlevel[a]; break;
    case 2: free_rerolls = 3; break;
    case 3: half_price = 1; break;
    case 4: ++extra_hand; break;
    case 5:
        if (ncharm < 5) {
            unsigned r = seed ^ ((unsigned)stop_ * 0x3D1u + g);
            u8 id, tries;
            for (tries = 0; tries < 30; ++tries) { id = rnd_n(&r, NCHARM); if (!has(id)) break; }
            charm[ncharm] = id; charm_val[ncharm] = 0; ++ncharm;
        } else gold += 6;
        break;
    }
}

static void hand_table(void) {
    u8 i, y;
    panel(0, 18, 40, 11);
    text(2, 18, "EL SEVIYELERI", 2);
    for (i = 0; i < 9; ++i) {
        y = 19 + i;
        text(2, y, hand_name[i], 0);
        text(14, y, "SV", 0); k_print_num(16, y, hlevel[i], 1, hlevel[i] > 1 ? 1 : 0);
        num(20, y, base_chips[i] + (unsigned)(hlevel[i] - 1) * lv_chips[i], 2);
        text(26, y, "X", 0);
        num(28, y, base_mult[i] + (hlevel[i] - 1) * lv_mult[i], 3);
    }
}

/* returns 1 to play the gate, 0 when it was skipped */
static u8 gate_map(void) {
    u8 g, x, p, b = boss[stop_ & 7];
    k_clear_map();
    k_hide_sprites(0);
    draw_top();
    blank(0, 1, 40, 1);
    text(1, 1, "SIRADAKI KAPI", 1);
    for (g = 0; g < 3; ++g) {
        x = 1 + g * 13;
        panel(x, 2, 12, 13);
        text(x + 1, 3, g == 2 ? "BEKCI" : g ? "BUYUK" : "KUCUK", g == 2 ? 3 : g == gate ? 1 : 0);
        text(x + 1, 5, "HEDEF", 0); num(x + 1, 6, gate_target(g), g == gate ? 1 : 0);
        text(x + 1, 8, "ODUL", 0); text(x + 6, 8, "$", 1); k_print_num(x + 7, 8, 3 + g, 1, 1);
        if (gate_state[g] == 1) text(x + 1, 11, "GECILDI", 2);
        else if (gate_state[g] == 2) text(x + 1, 11, "ATLANDI", 0);
        else if (g == 2) {
            text(x + 1, 10, boss_name[b], 3);
            if (b == 2) text(x + 1, 11, suit_name[boss_suit], 3);
            text(x + 1, b == 2 ? 12 : 11, boss_desc[b], 0);
        } else {
            text(x + 1, 10, "ATLARSAN:", 0);
            text(x + 1, 11, tag_d1[tag_kind[g]], 2);
            if (tag_kind[g] == 1) text(x + 1, 12, hand_name[tag_arg[g]], 2);
            else text(x + 1, 12, tag_d2[tag_kind[g]], 2);
        }
    }
    text(1, 16, "SHIFT GIR", 1);
    if (gate < 2) text(11, 16, "CTRL ATLA+ETIKET", 0);
    text(29, 16, "ALTIN", 1); num(35, 16, gold, 0);
    hand_table();
    for (;;) {
        k_wait_frame(); ++t8;
        k_sprite(0, (1 + gate * 13) * 8 + 40, 14 * 8 + 3 + (t8 >> 3 & 1), S_arrow, PAL_BIG);
        p = pad_new[0] | pad_new[1];
        if ((pad[0] | pad[1]) & BTN_SELECT && p & BTN_START) k_menu();
        if (p & (BTN_A | BTN_START)) { k_hide_sprites(0); return 1; }
        if (p & BTN_B && gate < 2) {
            apply_tag(gate);
            k_sfx(SFX_PICKUP);
            k_hide_sprites(0);
            return 0;
        }
    }
}

/* ---------------------------------------------------------------- run */
static u8 cash_out(void) {
    u8 g = 3 + gate, interest = gold / 5, i;
    if (interest > (deck_kind == D_ALTIN ? 10 : 5)) interest = deck_kind == D_ALTIN ? 10 : 5;
    g += hands_left + interest + (has(C_KESE) ? 4 : 0);
    gold += g;
    for (i = 0; i < ncharm; ++i) if (charm[i] == C_YANKI) ++charm_val[i];
    return g;
}

static void banner(const char *s, u8 pal) {
    panel(6, 11, 28, 6);
    text((40 - big_len(s)) / 2, 13, s, pal);
}

static u8 best_stop;
static void run(void) {
    u8 i, g;
    rs_shop = seed * 31u + 7; rs_luck = seed ^ 0x5A5Au;
    for (i = 0; i < 8; ++i) boss[i] = rnd_n(&rs_shop, NBOSS);
    boss_suit = rnd_n(&rs_shop, 4);
    for (i = 0; i < 9; ++i) hlevel[i] = 1;
    ncharm = 0; gold = 4; stop_ = 0; gate = 0; best_hand = 0;
    free_rerolls = 0; half_price = 0; extra_hand = 0;
    gate_state[0] = gate_state[1] = gate_state[2] = 0;
    if (deck_kind == D_KOR) { charm[0] = C_ATES; charm_val[0] = 0; ncharm = 1; }
    if (deck_kind == D_ALTIN) gold += 6;
    set_back(deck_kind);
    roll_tags();
    for (;;) {
        SYS_TEST = 0xE3;
        if (!gate_map()) {               /* skipped: on to the next gate, no gold, no shop */
            gate_state[gate] = 2;
            ++gate;
            continue;
        }
        SYS_TEST = 0xE1;
        if (!play_gate()) {
            k_hide_sprites(0);
            if (stop_ + 1 > best_stop) { best_stop = stop_ + 1; SAVE[0] = 'T'; SAVE[1] = best_stop; }
            banner("TILSIM BOZULDU", 3);
            text(8, 14, "DURAK", 0); k_print_num(14, 14, stop_ + 1, 1, 1);
            text(17, 14, "EN IYI EL", 0); num(27, 14, best_hand, 1);
            text(13, 15, "SHIFT: BASLIGA", 0);
            k_sfx(SFX_LOSE);
            do k_wait_frame(); while (!((pad_new[0] | pad_new[1]) & (BTN_A | BTN_START)));
            return;
        }
        SYS_TEST = 0xE2;
        k_hide_sprites(0);
        banner("KAPI ACILDI!", 1);
        k_sfx(SFX_WIN);
        g = cash_out();
        text(12, 15, "+", 1); k_print_num(13, 15, g, 2, 1); text(16, 15, "ALTIN", 1);
        sparks(50);
        if (gate == 2 && stop_ == 7) {
            banner("TILSIM KIRILDI!", 1);
            text(9, 15, "SONSUZ KIPE DEVAM", 0);
            sparks(90);
        }
        shop();
        gate_state[gate] = 1;
        if (++gate == 3) {
            gate = 0; ++stop_;
            gate_state[0] = gate_state[1] = gate_state[2] = 0;
            roll_tags();
            if (stop_ + 1 > best_stop) { best_stop = stop_ + 1; SAVE[0] = 'T'; SAVE[1] = best_stop; }
        }
    }
}

/* ---------------------------------------------------------------- title: deck, seed, start */
static void title(void) {
    u8 p, row = 0, pos = 0, i, f = 0, redraw = 1;
    static const u8 fan[5] = { 0x3C, 0x0B, 0x1A, 0x29, 0x38 };   /* A spades, K hearts, Q diamonds, J clubs, 10 spades */
    static const u8 row_y[4] = { 13, 18, 20, 21 };
    char sd[2];
    k_clear_map();
    k_hide_sprites(0);
    big_reset();
    big_center(1, "TILSIM");
    text(12, 4, "KADERIN KARTLARI", 2);
    for (i = 0; i < 5; ++i) draw_card(10 + i * 4, 6, fan[i], 0);
    panel(2, 11, 36, 15);
    text(5, 13, "DESTE", 0);
    text(5, 18, "TOHUM", 0);
    text(5, 20, "BASLA", 1);
    text(5, 21, "YENI TOHUMLA BASLA", 1);
    text(5, 23, "EN IYI DURAK", 0); k_print_num(18, 23, best_stop, 1, 1);
    text(3, 27, "YUKARI ASAGI SATIR   SOL SAG DEGISTIR", 0);
    text(3, 28, "SHIFT/SPACE SEC   ESC: MENU", 0);
    for (;;) {
        if (redraw) {
            redraw = 0;
            blank(11, 13, 16, 1);
            text(11, 13, "<", 2); text(13, 13, deck_name[deck_kind], 1); text(20, 13, ">", 2);
            blank(5, 14, 28, 2);
            text(5, 14, deck_d1[deck_kind], 0); text(5, 15, deck_d2[deck_kind], 0);
            set_back(deck_kind);
            draw_back(32, 12);
        }
        k_wait_frame(); ++f;
        for (i = 0; i < 4; ++i) {
            sd[0] = seed_letters[(seed >> (12 - i * 4)) & 15]; sd[1] = 0;
            text(12 + i * 2, 18, sd, row == 1 && i == pos && (f & 16) ? 0 : 1);
        }
        k_sprite(0, 2 * 8 + 4 + (f >> 3 & 1), row_y[row] * 8 - 4, S_arrow, PAL_BIG);
        p = pad_new[0] | pad_new[1];
        if ((pad[0] | pad[1]) & BTN_SELECT && p & BTN_START) k_menu();
        if (p & BTN_UP && row) { --row; blip(0); }
        if (p & BTN_DOWN && row < 3) { ++row; blip(0); }
        if (row == 0 && p & (BTN_LEFT | BTN_RIGHT)) {
            deck_kind = p & BTN_LEFT ? (deck_kind ? deck_kind - 1 : NDECK - 1) : (deck_kind + 1 < NDECK ? deck_kind + 1 : 0);
            redraw = 1; blip(deck_kind);
        }
        if (row == 1) {
            if (p & BTN_LEFT && pos) --pos;
            if (p & BTN_RIGHT && pos < 3) ++pos;
            if (p & (BTN_A | BTN_B)) {
                u8 sh = 12 - pos * 4, v = (seed >> sh) & 15;
                v = (v + (p & BTN_A ? 1 : 15)) & 15;
                seed = (seed & ~(15u << sh)) | (unsigned)v << sh;
                blip(v);
            }
        }
        if (row == 3 && p & (BTN_A | BTN_START)) {
            seed ^= (unsigned)f * 40503u + frame_count * 257u;
            if (!seed) seed = 1;
            break;
        }
        if (row != 1 && row != 3 && p & (BTN_A | BTN_START)) break;
        if (row == 1 && p & BTN_START) break;
    }
    k_hide_sprites(0);
    k_sfx(SFX_WIN);
}

void main(void) {
    static const u8 slots[8] = { 0, 4, 8, 12, 16, 20, 24, 28 };
    k_init();
    PPU_CTRL = 0;
    k_vunpack(TILE_BASE * 32, tiles_data);
    k_vunpack(0x2000, sprites_data);
    k_palette(PAL_CRED, pal_cred, 16); k_palette(PAL_CRED + 1, pal_cblack, 16);
    k_palette(PAL_CRED + 2, pal_cred_s, 16); k_palette(PAL_CRED + 3, pal_cblk_s, 16);
    k_palette(PAL_CRED + 4, pal_cred_g, 16); k_palette(PAL_CRED + 5, pal_cblk_g, 16);
    k_palette(PAL_BACK, pal_back, 16); k_palette(PAL_ICON, pal_icons, 16);
    k_palette(PAL_CHIPS, pal_chips, 16); k_palette(PAL_MULT, pal_mult, 16);
    k_palette(PAL_UI, pal_ui, 16);
    k_palette(0, pal_ui + 3, 1);           /* backdrop: the panel colour (deep purple felt) */
    big_init(slots, 8, PAL_BIG);
    big_palette(8);
    PPU_CTRL = CTRL_NMI | CTRL_BG | CTRL_SPR;
    if (SAVE[0] == 'T') best_stop = SAVE[1];
    seed = 0x4B2D;
    SYS_TEST = 0xE0;
    for (;;) {
        title();
        run();
    }
}
