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

# UART monitor: write a byte, dump it
k = run(Konsol(rom("boot.bin"), cart), 5)
k.uart_in += list(b"W 1234 5A\r") + list(b"D 1230\r")
k = run(k, 20)
out = bytes(k.uart_out)
check("monitor W/D", k.ram[0x1234] == 0x5A and b"1230: 00 00 00 00 5A" in out, out[-120:])

# a cartridge in the flash wins over the BRAM one
k = run(Konsol(rom("boot.bin"), cart, flash=cart.replace(b"TEST OYUN A", b"FLASH OYUN A")), 150)
txt = screen_text(k)
check("flash cartridge preferred", "FLASH OYUN A" in txt and "FLASH" in txt, txt)
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
hold(k, 0x80, 2)
run(k, 10, shot="kristal_level1.png")
check("kristal: level 1 drawn", "KRISTAL 00/10" in screen_text(k), screen_text(k))
W = 32
cell = lambda x, y: peek(k, S, "map", y * W + x) & 15
check("kristal: hero starts at (1,1)", peek(k, S, "hero_x") == 1 and peek(k, S, "hero_y") == 1)
hold(k, 0x01, 6 * 5)                     # right x5
check("kristal: walks right through dirt", peek(k, S, "hero_x") == 6 and cell(5, 1) == 0,
      f"x={peek(k, S, 'hero_x')}")
hold(k, 0x04, 6)                          # down
hold(k, 0x01, 6)                          # right: under the rock at (7,1)
hold(k, 0x01, 6)                          # right again: the rock falls into (7,2)
run(k, 12)
check("kristal: rock falls into the dug cell", cell(7, 2) == 3 and cell(7, 1) == 0, f"(7,1)={cell(7,1)} (7,2)={cell(7,2)}")
hold(k, 0x08, 6)                          # up to (8,1)
hold(k, 0x01, 6 * 3)                      # right to the crystal at (11,1)
check("kristal: crystal collected", peek(k, S, "gems") == 1 and peek(k, S, "score", size=2) == 10,
      f"gems={peek(k, S, 'gems')} hero=({peek(k, S, 'hero_x')},{peek(k, S, 'hero_y')})")
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

# no cartridge
k = run(Konsol(rom("boot.bin"), b""), 150)
check("missing cartridge message", "KARTUS BULUNAMADI" in screen_text(k), screen_text(k))

print(f"{'all software tests passed' if not failed else str(failed) + ' software test(s) failed'}")
sys.exit(1 if failed else 0)
