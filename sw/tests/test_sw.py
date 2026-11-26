#!/usr/bin/env python3
"""Software tests on the Python konsol model (tools/emu6502.py): CPU self-tests, boot ROM menu and loader,
UART monitor. Screenshots go to build/shots/."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../../tools"))
from emu6502 import Konsol, write_png

B = os.path.join(HERE, "../build")
SHOTS = os.path.join(B, "shots")
os.makedirs(SHOTS, exist_ok=True)
failed = 0

def check(name, ok, detail=""):
    global failed
    print(("PASS  " if ok else "FAIL  ") + name + (f" ({detail})" if detail and not ok else ""))
    if not ok:
        failed += 1

def rom(name):
    return open(os.path.join(B, name), "rb").read()

def screen_text(k):
    """The 64x32 tile map as text (font tiles are ASCII codes)."""
    rows = []
    for r in range(30):
        rows.append("".join(chr(k.ppu.vram[0x4000 + 2 * (r * 64 + c)]) if 32 <= k.ppu.vram[0x4000 + 2 * (r * 64 + c)] < 96 else "."
                            for c in range(40)))
    return "\n".join(rows)

def run(k, frames, pad=None, shot=None):
    pic = None
    for f in range(frames):
        if pad and f in pad: k.pads[0] = pad[f]
        pic = k.frame(render=shot is not None and f == frames - 1)
    if shot:
        write_png(os.path.join(SHOTS, shot), pic)
    return k

# CPU tests
for t in ("cputest", "ppucpu"):
    k = run(Konsol(rom(t + ".bin")), 10)
    check(f"{t} on the model", k.test == 1, f"test code {k.test}")

# boot ROM: logo, then the menu lists the cartridge's games
cart = rom("carttest.bin")
k = run(Konsol(rom("boot.bin"), cart), 30, shot="logo.png")
check("logo shows the title", "RETRO KONSOL" in screen_text(k), screen_text(k))
check("start-up jingle plays", any(r == 0 and v for _, r, v in k.apu_log))
k = run(k, 100, shot="menu.png")
txt = screen_text(k)
check("menu lists both test games", "TEST OYUN A" in txt and "TEST OYUN B" in txt and "2-4P" in txt, txt)
check("boot greeting on the UART", b"RETRO KONSOL BOOT ROM" in k.uart_out, k.uart_out)
# down, then A: game B is loaded and runs
k = run(k, 30, pad={2: 0x04, 6: 0x00, 10: 0x80, 14: 0x00})
check("game B loaded from the cartridge and running", k.test == 0xB2, f"test {k.test}")
run(k, 2, shot="game_b.png")
check("game B drew its text", "TEST OYUN B CALISIYOR" in screen_text(k), screen_text(k))
# Select + Start returns to the menu
k = run(k, 200, pad={0: 0x30, 4: 0x00})
check("Select+Start returns to the menu", "OYUN SEC" in screen_text(k), screen_text(k))

# keyboard diagnostics in the menu: scan codes on the 7-segment display and the UART
k = run(Konsol(rom("boot.bin"), cart), 140)
k.kbd += [0x1D, 0xF0, 0x1D]
k = run(k, 3)
check("menu shows PS/2 codes", bytes(k.disp[0:3]) == bytes([0x1D, 0xF0, 0x1D]) and b"KB 1D" in bytes(k.uart_out),
      f"{bytes(k.disp[0:4]).hex()} {bytes(k.uart_out)[-30:]}")

# UART monitor: write a byte, dump it
k = run(Konsol(rom("boot.bin"), cart), 5)
k.uart_in += list(b"W 1234 5A\r") + list(b"D 1230\r")
k = run(k, 20)
out = bytes(k.uart_out)
check("monitor W/D", k.ram[0x1234] == 0x5A and b"1230: 00 00 00 00 5A" in out, out[-120:])

# a cartridge in the flash wins over the BRAM one
k = run(Konsol(rom("boot.bin"), cart, flash=cart.replace(b"TEST OYUN A", b"FLSH OYUN A")), 150)
txt = screen_text(k)
check("flash cartridge preferred", "FLSH OYUN A" in txt and "FLASH" in txt, txt)
k = run(Konsol(rom("boot.bin"), cart), 150)
check("BRAM cartridge when the flash is empty", "TEST OYUN A" in screen_text(k) and "BRAM" in screen_text(k))

# ---------------------------------------------------------------- games
def load_game(name):
    """Boot ROM in ROM, the game at $9000, started directly. Returns (konsol, symbols)."""
    k = Konsol(rom("boot.bin"))
    g = rom(f"game_{name}.bin")
    k.game[:len(g)] = g
    k.pc = 0x9000
    syms = {}
    for l in open(os.path.join(B, f"game_{name}.lbl")):
        _, addr, label = l.split()
        syms[label.lstrip(".")] = int(addr, 16)
    return k, syms

def hold(k, mask, frames, player=0):
    k.pads[player] = mask
    run(k, frames)
    k.pads[player] = 0
    run(k, 2)

def peek(k, syms, name, off=0, size=1):
    a = syms["_" + name] + off
    v = 0
    for i in range(size):
        v |= k.rd(a + i) << (8 * i)
    return v

k, S = load_game("kristal")
check("kristal: fits in 20 KB", len(rom("game_kristal.bin")) <= 0x5000)
run(k, 10, shot="kristal_title.png")
check("kristal: title screen", "KRISTAL OCAK" in screen_text(k))
k.pads[1] = 0x10                          # Enter = keyboard player 2 Start
run(k, 3); k.pads[1] = 0; run(k, 2)
run(k, 10, shot="kristal_level1.png")
check("kristal: level 1 drawn", "KRISTAL 00/10" in screen_text(k), screen_text(k))
W = 32
cell = lambda x, y: peek(k, S, "map", y * W + x) & 15
check("kristal: hero starts at (1,1)", peek(k, S, "hero_x") == 1 and peek(k, S, "hero_y") == 1)
def walk(mask, until, limit=120):
    """hold a direction until the hero reaches a cell (the game moves once per 6-frame step)"""
    k.pads[0] = mask
    for _ in range(limit):
        if until(): break
        run(k, 1)
    k.pads[0] = 0
    run(k, 2)
hx = lambda: peek(k, S, "hero_x")
hy = lambda: peek(k, S, "hero_y")
walk(0x01, lambda: hx() == 6)
check("kristal: walks right through dirt", hx() == 6 and cell(5, 1) == 0, f"x={hx()}")
walk(0x04, lambda: hy() == 2)                 # down
walk(0x01, lambda: hx() == 7)                 # right: under the rock at (7,1)
walk(0x01, lambda: hx() == 8)                 # right again: the rock falls into (7,2)
run(k, 12)
check("kristal: rock falls into the dug cell", cell(7, 2) == 3 and cell(7, 1) == 0, f"(7,1)={cell(7,1)} (7,2)={cell(7,2)}")
walk(0x08, lambda: hy() == 1)                 # up to (8,1)
walk(0x01, lambda: hx() == 11)                # right to the crystal at (11,1)
check("kristal: crystal collected", peek(k, S, "gems") == 1 and peek(k, S, "score", size=2) == 10,
      f"gems={peek(k, S, 'gems')} hero=({hx()},{hy()})")
k.wr(S["_time_left"], 1); k.wr(S["_time_left"] + 1, 0)
run(k, 200, shot="kristal_timeout.png")
check("kristal: time out costs a life", peek(k, S, "lives") == 2, f"lives={peek(k, S, 'lives')}")

k, S = load_game("yildiz")
check("yildiz: fits in 20 KB", len(rom("game_yildiz.bin")) <= 0x5000)
run(k, 10, shot="yildiz_title.png")
check("yildiz: title screen", "YILDIZ MUHAFIZI" in screen_text(k))
hold(k, 0x80, 2)
f0 = peek(k, S, "frame_count")
loops0 = k.rd(S["_starx"]) if "_starx" in S else None
# fly up and down firing for 20 seconds
for t in range(20):
    hold(k, 0x80 | (0x08 if t % 2 else 0x04), 28)
run(k, 1, shot="yildiz_play.png")
check("yildiz: enemies shot down", peek(k, S, "score", size=2) >= 40, f"score={peek(k, S, 'score', size=2)}")
k.wr(S["_score"], 450 & 255); k.wr(S["_score"] + 1, 450 >> 8)
run(k, 3)
check("yildiz: waves advance with the score", peek(k, S, "wave", size=2) >= 3, f"wave={peek(k, S, 'wave', size=2)}")
check("yildiz: sprites on screen", sum(1 for i in range(64) if k.ppu.vram[0x5200 + 4 * i] < 240) >= 2)
# the game loop must keep up with 60 frames per second (starx counts main loop passes)
k.pads[0] = 0x80
l0 = peek(k, S, "starx")
run(k, 120)
loops = (peek(k, S, "starx") - l0) & 255
check("yildiz: game loop keeps up with 60 fps", loops >= 115, f"{loops} loops in 120 frames")
k.pads[0] = 0

k, S = load_game("fitil")
check("fitil: fits in 20 KB", len(rom("game_fitil.bin")) <= 0x5000)
run(k, 10, shot="fitil_setup.png")
check("fitil: setup screen", "FITIL ARENASI" in screen_text(k) and "OYUNCU SAYISI" in screen_text(k))
hold(k, 0x08, 2); hold(k, 0x08, 2)        # 4 players
check("fitil: player count", peek(k, S, "nplayers") == 4, f"{peek(k, S, 'nplayers')}")
hold(k, 0x04, 2)                          # back to 3
hold(k, 0x10, 2)
run(k, 5, shot="fitil_arena.png")
check("fitil: 3-player round starts", peek(k, S, "nplayers") == 3 and peek(k, S, "alive_count") == 3,
      f"n={peek(k, S, 'nplayers')} alive={peek(k, S, 'alive_count')}")
G = lambda x, y: peek(k, S, "grid", y * 15 + x)
hold(k, 0x80, 1)                          # P1 drops a bomb at (1,1) ...
check("fitil: bomb placed", G(1, 1) == 3, f"{G(1,1)}")
hold(k, 0x04, 40)                         # ... and walks down to (1,3)
check("fitil: P1 walked to safety", (peek(k, S, "px"), peek(k, S, "py")) == (1, 3), f"({peek(k, S, 'px')},{peek(k, S, 'py')})")
run(k, 130, shot="fitil_boom.png")
check("fitil: bomb exploded, P1 survived", G(1, 1) == 0 and peek(k, S, "palive") == 1)
# P2 and P3 blow themselves up: P1 wins the round
k.pads[1] = 0x80; k.pads[2] = 0x80
run(k, 2); k.pads[1] = 0; k.pads[2] = 0
run(k, 240, shot="fitil_round.png")
check("fitil: last player standing wins the round", peek(k, S, "pwins") == 1, f"wins={[peek(k, S, 'pwins', i) for i in range(4)]}")

k, S = load_game("olimpiyat")
check("olimpiyat: fits in 20 KB", len(rom("game_olimpiyat.bin")) <= 0x5000)
run(k, 10, shot="olimpiyat_title.png")
check("olimpiyat: title screen", "MAHALLE OLIMPIYATI" in screen_text(k))
hold(k, 0x10, 2)                          # 2 players (default)
shots = {1: "olimpiyat_kosu.png", 2: "olimpiyat_atlama.png", 3: "olimpiyat_refleks.png"}
taken = set()
toggle = 0
for f in range(20000):
    ev = peek(k, S, "event_no")
    if ev == 4:
        break
    txt = screen_text(k)
    toggle ^= 1
    if "DEVAM: A" in txt or "HAZIR: A" in txt:
        k.pads[0] = 0x80 if toggle else 0
    elif ev in (1, 2):
        k.pads[0] = 0x80 if (f >> 1) & 1 else 0x40          # A, B, A, B ...
        rx = peek(k, S, "runner_x", size=2)
        if ev == 2 and 180 <= rx <= 199:
            k.pads[0] |= 0x08                               # jump just before the line
    elif ev == 3:
        k.pads[0] = (0x80 if toggle else 0) if "SIMDI!" in txt else 0
    if ev in shots and ev not in taken and f % 200 == 150:
        run(k, 1, shot=shots[ev]); taken.add(ev)
    else:
        run(k, 1)
pts = [peek(k, S, "points", i) for i in range(2)]
check("olimpiyat: all three events played", peek(k, S, "event_no") == 4, f"event {peek(k, S, 'event_no')}")
check("olimpiyat: points awarded (2 players, 3 events)", 21 <= sum(pts) <= 24, f"points={pts}")
check("olimpiyat: champion screen", "SAMPIYON" in screen_text(k))

# ---------------------------------------------------------------- launcher (the real cartridge)
full = rom("cart.bin")
k = Konsol(rom("boot.bin"), full)
run(k, 60, shot="launcher_splash.png")
check("launcher: started by the boot ROM instead of the menu", k.test in (0xD0, 0xD1) and "OYUN SEC" not in screen_text(k),
      f"test {k.test}")
for _ in range(500):
    run(k, 1)
    if k.test == 0xD1: break
run(k, 10, shot="launcher_home.png")
txt = screen_text(k)
check("launcher: home screen after the splash", k.test == 0xD1 and "MADENI KAZ" in txt and "6" in txt and "OYUN" in txt, txt)
k.pads[0] = 0x01; run(k, 3); k.pads[0] = 0; run(k, 40, shot="launcher_blok.png")
check("launcher: Right picks the next game", "DUSEN BLOKLARI" in screen_text(k), screen_text(k))
k.pads[0] = 0x80; run(k, 3); k.pads[0] = 0
for _ in range(120):
    run(k, 1)
    if k.test == 0xB0: break
check("launcher: A loads the game through the boot ROM API", k.test == 0xB0, f"test {k.test}")
check("launcher: remembers the choice for the way back", bytes(k.ram[0x2FC:0x2FF]) == b"KW\x01", bytes(k.ram[0x2FC:0x2FF]))
run(k, 20)
k.pads[0] = 0x30; run(k, 3); k.pads[0] = 0          # Select + Start: back to the menu = the launcher, warm
for _ in range(200):
    run(k, 1)
    if k.test == 0xD1: break
check("launcher: back from a game without the splash, same game chosen",
      k.test == 0xD1 and "DUSEN BLOKLARI" in screen_text(k), screen_text(k))
k = Konsol(rom("boot.bin"), full)
k.pads[0] = 0x20                                    # Select held at power-up: the boot ROM menu
run(k, 140); k.pads[0] = 0; run(k, 5)
txt = screen_text(k)
check("Select at power-up: the boot ROM menu lists the cartridge", "OYUN SEC" in txt and "TILSIM" in txt and "BLOK YAGMURU" in txt, txt)

# ---------------------------------------------------------------- blok yagmuru
k, S = load_game("blok")
check("blok: fits in 20 KB", len(rom("game_blok.bin")) <= 0x5000)
run(k, 40, shot="blok_title.png")
check("blok: title screen", "BASLANGIC SEVIYESI" in screen_text(k), screen_text(k))
hold(k, 0x10, 2)
run(k, 30, shot="blok_play.png")
check("blok: playfield", "SIRADAKI" in screen_text(k) and k.test == 0xB1)
def key(k, code, frames=2):
    k.keys[code >> 3] |= 1 << (code & 7); run(k, frames)
    k.keys[code >> 3] &= ~(1 << (code & 7)); run(k, 2)
k.wr(S["_cur"], 2); k.wr(S["_rot"], 0); k.wr(S["_px"], 3); k.wr(S["_py"], 4)   # a T piece in open space
key(k, 0x22)                                        # X: turn right
r1 = k.rd(S["_rot"])
key(k, 0x1A); key(k, 0x1A)                          # Z twice: turn left twice
check("blok: X / Z on the keyboard turn the piece", r1 == 1 and k.rd(S["_rot"]) == 3, f"rot {r1} -> {k.rd(S['_rot'])}")
hold(k, 0x80, 2)                                    # A: turn right (pad)
check("blok: A turns the piece", k.rd(S["_rot"]) == 0, f"rot {k.rd(S['_rot'])}")
held0 = k.rd(S["_hold"])
k.pads[0] = 0x20; run(k, 3)
check("blok: Select does not hold while it is down", k.rd(S["_hold"]) == held0)
k.pads[0] = 0; run(k, 3)
check("blok: Select holds the piece when let go", k.rd(S["_hold"]) == 2 and held0 == 255, f"hold {held0} -> {k.rd(S['_hold'])}")
for x in range(10):
    for y in range(22): k.wr(S["_board"] + y * 10 + x, 0)
# an I piece over a row with a 4-cell gap: a hard drop clears it
BOARD = S["_board"]
for x in (0, 1, 2, 7, 8, 9): k.wr(BOARD + 21 * 10 + x, 3)
k.wr(S["_cur"], 0); k.wr(S["_rot"], 0); k.wr(S["_px"], 3); k.wr(S["_py"], 1)
hold(k, 0x08, 2)                                    # Up: hard drop
run(k, 40, shot="blok_clear.png")
check("blok: a full row is cleared and scored", peek(k, S, "lines", size=2) == 1 and peek(k, S, "score", size=4) >= 100
      and all(k.rd(BOARD + 21 * 10 + x) == 0 for x in range(10)),
      f"lines {peek(k, S, 'lines', size=2)} score {peek(k, S, 'score', size=4)} row {[k.rd(BOARD + 210 + x) for x in range(10)]}")
# pieces stack up to the top: game over
for _ in range(40):
    hold(k, 0x08, 2)
    run(k, 30)
    if "OYUN BITTI" in screen_text(k): break
run(k, 70, shot="blok_over.png")
check("blok: topping out ends the game", "OYUN BITTI" in screen_text(k), screen_text(k))

# ---------------------------------------------------------------- tilsim
import itertools
from collections import Counter
def tilsim_value(cards):
    """the game's scoring without charms (hand level 1)"""
    BC = [5, 12, 22, 30, 35, 40, 45, 70, 110]; BM = [1, 2, 2, 3, 4, 4, 5, 7, 9]
    r = [c & 15 for c in cards]; s_ = [c >> 4 for c in cards]; cnt = Counter(r)
    fl = len(cards) == 5 and len(set(s_)) == 1
    st = len(cards) == 5 and len(cnt) == 5 and (max(r) - min(r) == 4 or set(r) == {12, 0, 1, 2, 3})
    v = sorted(cnt.values(), reverse=True)
    t = (8 if st and fl else 7 if v[0] == 4 else 6 if v[0] == 3 and len(v) > 1 and v[1] == 2 else 5 if fl else 4 if st
         else 3 if v[0] == 3 else 2 if v[0] == 2 and len(v) > 1 and v[1] == 2 else 1 if v[0] == 2 else 0)
    sc = cards if t in (4, 5, 6, 8) else [max(cards, key=lambda c: c & 15)] if t == 0 else [c for c in cards if cnt[c & 15] >= 2]
    return (BC[t] + sum((c & 15) + 2 if (c & 15) < 9 else 11 if (c & 15) == 12 else 10 for c in sc)) * BM[t]

def load_low(name):
    k = Konsol(rom("boot.bin"))
    g = rom(f"game_{name}.bin")
    k.ram[0x400:0x400 + len(g)] = g
    k.pc = 0x400
    syms = {}
    for l in open(os.path.join(B, f"game_{name}.lbl")):
        _, addr, label = l.split()
        syms[label.lstrip(".")] = int(addr, 16)
    return k, syms

k, S = load_low("tilsim")
check("tilsim: fits below $7C00 (loads at $0400)", len(rom("game_tilsim.bin")) <= 0x7800)
run(k, 70, shot="tilsim_title.png")
check("tilsim: title with the seed", "KADERIN KARTLARI" in screen_text(k) and "TOHUM" in screen_text(k), screen_text(k))
def tilsim_enter(k):
    """from the stop's map into the gate"""
    for _ in range(300):
        run(k, 1)
        if k.test == 0xE3: break
    run(k, 20)
    hold(k, 0x80, 2)
    for _ in range(200):
        run(k, 1)
        if k.test == 0xE1: break
    run(k, 40)
hold(k, 0x10, 2)                                    # Start: play the shown seed with the classic deck
for _ in range(300):
    run(k, 1)
    if k.test == 0xE3: break
run(k, 30, shot="tilsim_map.png")
check("tilsim: the stop's map shows the gates and the hand levels",
      "SIRADAKI KAPI" in screen_text(k) and "EL SEVIYELERI" in screen_text(k), screen_text(k))
hold(k, 0x80, 2)
for _ in range(200):
    run(k, 1)
    if k.test == 0xE1: break
run(k, 40)
hand_of = lambda: [k.rd(S["_hand"] + i) for i in range(k.rd(S["_hand_n"]))]
first = hand_of()
check("tilsim: 8 cards dealt, sorted by rank", len(first) == 8 and [c & 15 for c in first] == sorted([c & 15 for c in first], reverse=True),
      [hex(c) for c in first])
# the same seed deals the same hand
k2, _ = load_low("tilsim"); run(k2, 70); hold(k2, 0x10, 2); tilsim_enter(k2)
check("tilsim: the same seed deals the same cards", [k2.rd(S["_hand"] + i) for i in range(8)] == first)
def best(hand):
    b = (0, 0)
    for n in range(1, 6):
        for comb in itertools.combinations(range(len(hand)), n):
            v = tilsim_value([hand[i] for i in comb])
            if v > b[0]: b = (v, sum(1 << i for i in comb))
    return b
score = lambda: peek(k, S, "score", size=4)
gates = 0
shop_seen = False
for hand_no in range(30):
    v, m = best(hand_of())
    if hand_no and v < 90 and k.rd(S["_discards_left"]):
        # a weak hand: throw away up to 5 low cards that are not in a pair (Select acts when let go)
        h = hand_of(); cnt = Counter(c & 15 for c in h)
        out = sorted([i for i, c in enumerate(h) if cnt[c & 15] < 2], key=lambda i: h[i] & 15)[:5]
        if out:
            k.wr(S["_selmask"], sum(1 << i for i in out))
            hold(k, 0x20, 2); run(k, 20)
            continue
    before = score()
    k.wr(S["_selmask"], m)
    hold(k, 0x10, 2)
    k.pads[0] = 0x80                                # hold A: fast scoring
    for _ in range(900):
        run(k, 1)
        if k.test == 0xE2 or (score() - before == v and "KART SEC" in screen_text(k)): break
    k.pads[0] = 0
    if hand_no == 0:
        run(k, 5, shot="tilsim_scored.png")
        check("tilsim: a hand scores chips x mult like the rules say", score() - before == v, f"got {score() - before}, want {v}")
    if k.test == 0xE2:
        gates += 1
        run(k, 160, shot="tilsim_shop.png")
        if not shop_seen:
            shop_seen = True
            check("tilsim: the shop opens after a gate", "PAZAR" in screen_text(k) and "ALTIN" in screen_text(k), screen_text(k))
            n0 = k.rd(S["_ncharm"]); g0 = k.rd(S["_gold"])
            hold(k, 0x80, 2); run(k, 10)
            check("tilsim: buying a charm costs gold", k.rd(S["_ncharm"]) == n0 + 1 and k.rd(S["_gold"]) < g0,
                  f"charms {k.rd(S['_ncharm'])} gold {g0}->{k.rd(S['_gold'])}")
        hold(k, 0x10, 2)
        if gates == 2: break
        tilsim_enter(k)
    run(k, 30)
check("tilsim: two gates opened by good play", gates == 2, f"gates {gates}")

# skipping a gate gives its tag; the short deck has no 2 3 4
k, S = load_low("tilsim")
run(k, 70)
hold(k, 0x01, 2); hold(k, 0x01, 2)                  # deck: KLASIK -> KOR -> KISA
run(k, 5, shot="tilsim_title.png")
check("tilsim: deck chosen on the title", k.rd(S["_deck_kind"]) == 2 and "KISA" in screen_text(k), screen_text(k))
hold(k, 0x10, 2)
for _ in range(300):
    run(k, 1)
    if k.test == 0xE3: break
run(k, 30)
kind, arg = k.rd(S["_tag_kind"]), k.rd(S["_tag_arg"])
gold0, lv0 = k.rd(S["_gold"]), k.rd(S["_hlevel"] + arg)
hold(k, 0x40, 2)                                    # B: skip the small gate
for _ in range(100):
    run(k, 1)
    if k.rd(S["_gate"]) == 1: break
ok = {0: k.rd(S["_gold"]) == gold0 + 10, 1: k.rd(S["_hlevel"] + arg) == lv0 + 1, 2: k.rd(S["_free_rerolls"]) == 3 if "_free_rerolls" in S else True,
      3: True, 4: True, 5: k.rd(S["_ncharm"]) == 1}[kind]
check("tilsim: skipping the small gate gives its tag and moves on", k.rd(S["_gate"]) == 1 and ok, f"tag {kind} gate {k.rd(S['_gate'])}")
tilsim_enter(k)
check("tilsim: the short deck has 40 cards without 2 3 4", k.rd(S["_deck_n"]) == 40 and all((c & 15) >= 3 for c in hand_of()),
      [hex(c) for c in hand_of()])
k.keys[0x76 >> 3] |= 1 << (0x76 & 7)                # Esc: back to the menu (the boot ROM resets)
run(k, 3)
check("tilsim: Esc leaves the game (the boot ROM runs again)", k.pc >= 0xE000, f"pc {k.pc:04X}")

# no cartridge
k = run(Konsol(rom("boot.bin"), b""), 150)
check("missing cartridge message", "KARTUS BULUNAMADI" in screen_text(k), screen_text(k))

print(f"{'all software tests passed' if not failed else str(failed) + ' software test(s) failed'}")
sys.exit(1 if failed else 0)
