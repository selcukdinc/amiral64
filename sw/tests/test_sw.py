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

def load_low(name):
    k = Konsol(rom("boot.bin"))
    g = rom(f"game_{name}.bin")
    k.ram[0x400:0x400 + len(g)] = g
    if os.path.exists(os.path.join(B, f"game_{name}.bin.ek")):      # extra data: where the loader leaves the port
        k.cart = bytearray(rom(f"game_{name}.bin.ek")); k.cart_addr = 0
    k.pc = 0x400
    syms = {}
    for l in open(os.path.join(B, f"game_{name}.lbl")):
        _, addr, label = l.split()
        syms[label.lstrip(".")] = int(addr, 16)
    return k, syms

k, S = load_low("kristal")
check("kristal: fits below $8000 (loads at $0400)", len(rom("game_kristal.bin")) <= 0x7C00)
run(k, 40, shot="kristal_title.png")
check("kristal: title screen", "KRISTALLERI TOPLA" in screen_text(k), screen_text(k))
check("kristal: crystals fall behind the title", sum(1 for i in range(8, 16) if k.rd(0x300 + 4 * i) < 240) >= 4)
ST_TITLE, ST_DEMO, ST_SELECT, ST_CARD, ST_PLAY, ST_PAUSE, ST_DEATH, ST_CLEAR, ST_OVER = range(9)
st = lambda: peek(k, S, "state")
def tap(mask, player=0, after=4):
    k.pads[player] = mask; run(k, 2); k.pads[player] = 0; run(k, after)
tap(0x10, player=1)                       # Enter = keyboard player 2 Start
run(k, 10, shot="kristal_select.png")
txt = screen_text(k)
check("kristal: Start opens the mine select", st() == ST_SELECT and "TOPRAK OCAK" in txt and txt.count("KILITLI") == 2, txt)
tap(0x10)
run(k, 30, shot="kristal_card.png")
txt = screen_text(k)
check("kristal: level card (name, target, a line about the level)", st() == ST_CARD and "ILK KAZI" in txt and "HEDEF" in txt
      and "KRISTAL" in txt and "KAYADAN KAC" in txt, txt)
tap(0x80)                                 # A: on with the level
run(k, 10, shot="kristal_level1.png")
txt = screen_text(k)
check("kristal: level 1 drawn with its HUD", st() == ST_PLAY and "00/10" in txt and "PUAN" in txt and "OCAK 1-1" in txt, txt)
W = 32
cell = lambda x, y: peek(k, S, "map", y * W + x) & 15
check("kristal: hero starts at (1,1)", peek(k, S, "hero_x") == 1 and peek(k, S, "hero_y") == 1)
check("kristal: the miner is a sprite", k.rd(0x300) < 240 and k.rd(0x302) <= 10)
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
# the sprite slides: while walking right it moves a few pixels a frame, never a whole cell at once
# (the OAM the PPU shows: copied by DMA in the NMI)
xs = []
k.pads[0] = 0x01
for _ in range(14):
    run(k, 1); xs.append(k.ppu.vram[0x5201] | (0x100 if k.ppu.vram[0x5203] & 0x80 else 0))
k.pads[0] = 0; run(k, 2)
steps = [b - a for a, b in zip(xs, xs[1:])]
check("kristal: the miner slides from cell to cell", max(steps) <= 4 and sum(1 for d in steps if d > 0) >= 8, xs)
walk(0x01, lambda: hx() == 6)
check("kristal: walks right through dirt", hx() == 6 and cell(5, 1) == 0, f"x={hx()}")
walk(0x04, lambda: hy() == 2)                 # down
walk(0x01, lambda: hx() == 7)                 # right: under the rock at (7,1)
k.pads[0] = 0x01                              # right again: the rock wobbles once, then falls into (7,2)
wobbled = dusty = False
for _ in range(30):
    run(k, 1)
    if peek(k, S, "map", 1 * W + 7) & 0x20: wobbled = True
    if any(13 <= k.rd(0x300 + 4 * i + 2) <= 15 and k.rd(0x300 + 4 * i) < 240 for i in range(1, 5)): dusty = True
    if hx() == 8: k.pads[0] = 0
k.pads[0] = 0
run(k, 12)
check("kristal: the rock wobbles before it falls", wobbled)
check("kristal: rock falls into the dug cell", cell(7, 2) == 3 and cell(7, 1) == 0, f"(7,1)={cell(7,1)} (7,2)={cell(7,2)}")
check("kristal: dust flies where the rock lands", dusty)
pal5 = lambda: bytes(k.ppu.vram[0x5000 + 5 * 32 + 10:0x5000 + 5 * 32 + 18])
c0 = pal5(); run(k, 9)
check("kristal: the crystals' colours turn round", pal5() != c0)
walk(0x08, lambda: hy() == 1)                 # up to (8,1)
walk(0x01, lambda: hx() == 11)                # right to the crystal at (11,1)
check("kristal: crystal collected", peek(k, S, "gems") == 1 and peek(k, S, "score", size=2) == 10,
      f"gems={peek(k, S, 'gems')} hero=({hx()},{hy()})")
# B + direction: dig the dirt below without moving
k.pads[0] = 0x44; run(k, 8); k.pads[0] = 0; run(k, 2)
check("kristal: B + direction digs without moving", cell(11, 2) == 0 and (hx(), hy()) == (11, 1), f"({hx()},{hy()}) {cell(11, 2)}")
# a press shorter than a step is not lost
k.pads[0] = 0x01; run(k, 1); k.pads[0] = 0; run(k, 9)
check("kristal: a short press still moves one cell", (hx(), hy()) == (12, 1), f"({hx()},{hy()})")
# pause menu: Start, then Devam
tap(0x10)
check("kristal: Start opens the pause menu", st() == ST_PAUSE and "DURAKLADI" in screen_text(k) and "MENUYE DON" in screen_text(k),
      screen_text(k))
run(k, 1, shot="kristal_pause.png")
tap(0x10)
check("kristal: Devam goes back to the level", st() == ST_PLAY and "DURAKLADI" not in screen_text(k) and hx() == 12)
k.wr(S["_time_left"], 1); k.wr(S["_time_left"] + 1, 0)
run(k, 200, shot="kristal_timeout.png")
check("kristal: time out costs a life", peek(k, S, "lives") == 2 and st() == ST_PLAY and (hx(), hy()) == (1, 1),
      f"lives={peek(k, S, 'lives')} state={st()}")
# pause, Yeniden basla: the level again, one life less
tap(0x10); tap(0x04); tap(0x80)
run(k, 90)
check("kristal: pause menu restart costs a life", peek(k, S, "lives") == 1 and st() == ST_PLAY, f"lives={peek(k, S, 'lives')}")
# hold A: the level again; with the last life it is the end
walk(0x01, lambda: hx() == 3)
k.pads[0] = 0x80; run(k, 30)
check("kristal: holding A shows the restart bar", "YENIDEN" in screen_text(k))
run(k, 40); k.pads[0] = 0
run(k, 90, shot="kristal_over.png")
txt = screen_text(k)
check("kristal: A held on the last life: game over", st() == ST_OVER and "PUAN" in txt and "REKOR" in txt, txt)
run(k, 30); tap(0x10); run(k, 10)            # (a screen takes a few frames to draw)
check("kristal: game over goes back to the mine select", st() == ST_SELECT)
# play level 1 again and finish it with every crystal and no death: three stars, level 2 opens
tap(0x10); run(k, 25); tap(0x80); run(k, 5)
k.wr(S["_gems"], peek(k, S, "gems_total"))
k.wr(S["_won"], 1)
run(k, 60)
check("kristal: level end counts the time left into the points", st() == ST_CLEAR and "BOLUM TAMAM" in screen_text(k))
run(k, 140, shot="kristal_clear.png")
T_istar = int(next(l.split()[2] for l in open(os.path.join(B, "kristal/art.h")) if l.startswith("#define T_istar ")))
lit = sum(1 for c in range(64) if k.ppu.vram[0x4000 + 2 * (15 * 64 + c)] == T_istar)
check("kristal: level end gives three stars", peek(k, S, "time_left", size=2) == 0 and peek(k, S, "score", size=2) >= 250
      and lit == 3, f"score={peek(k, S, 'score', size=2)} stars={lit}")
tap(0x10); run(k, 10)
txt = screen_text(k)
check("kristal: then the card of the next level", st() == ST_CARD and "BUZ KOPRUSU" in txt, txt)
save = bytes(k.ram[0x2E8:0x2F7])
check("kristal: save area: level 2 open, three stars on level 1", save[0:2] == b"KO" and save[2] == 2 and save[3] == 3, save.hex())
# switched on again (RAM kept): the save is read
k, S = load_low("kristal")
k.ram[0x2E8:0x2F7] = save
run(k, 40)
check("kristal: the best score on the title", "REKOR" in screen_text(k))
tap(0x10, player=1); run(k, 10)
txt = screen_text(k)
check("kristal: the save is read when the game starts again", txt.count("KILITLI") == 1, txt)
tap(0x01); tap(0x10); run(k, 10)
check("kristal: the opened level can be chosen", st() == ST_CARD and "BUZ KOPRUSU" in screen_text(k))
# a wrong check byte: the save is not used
k, S = load_low("kristal")
k.ram[0x2E8:0x2F7] = save[:14] + bytes([save[14] ^ 1])
run(k, 40); tap(0x10, player=1); run(k, 10)
check("kristal: a broken save is not used", screen_text(k).count("KILITLI") == 2)
# left alone, the title plays a demo: the first level by itself, the door opens; a key ends it
k, S = load_low("kristal")
run(k, 660)
check("kristal: the title starts a demo when left alone", st() == ST_PLAY and peek(k, S, "demo") == 1 and "DEMO" in screen_text(k))
run(k, 330, shot="kristal_demo.png")
check("kristal: the demo collects crystals and opens the door", peek(k, S, "gems") >= 10 and peek(k, S, "exit_open") == 1
      and peek(k, S, "alive") == 1, f"gems={peek(k, S, 'gems')} alive={peek(k, S, 'alive')} ({hx()},{hy()})")
tap(0x80)
check("kristal: a key ends the demo", st() == ST_TITLE)

# Faz 6: cracked walls and dynamite, set up on level 1 by writing the map
k, S = load_low("kristal")
run(k, 40); tap(0x10, player=1); run(k, 10); tap(0x10); run(k, 30); tap(0x80); run(k, 10)
EMPTY, DIRT, WALL, ROCK, GEM, GEM2, EXIT, HERO, CRACK, DYN, LIT, GAS, BOOM = range(13)
setc = lambda x, y, v: k.wr(S["_map"] + y * W + x, v)
# a rock over a gap over a cracked wall, walled in at the sides
for y in range(8, 12):
    setc(19, y, WALL); setc(21, y, WALL); setc(20, y, EMPTY)
setc(20, 7, WALL); setc(20, 8, ROCK); setc(20, 10, CRACK)
saw_hit = False
for _ in range(80):
    run(k, 1)
    if peek(k, S, "map", 10 * W + 20) == CRACK | 0x20: saw_hit = True
check("kristal: a falling rock breaks the cracked wall and falls through", saw_hit and cell(20, 10) == EMPTY and cell(20, 11) == ROCK,
      f"{[cell(20, y) for y in range(8, 12)]}")
# dynamite: place it to the right with B, run down; a stick next to it goes off too (chain),
# the crystal and the steel wall stay, the rock goes
k.wr(S["_dyns"], 2)
setc(2, 1, EMPTY); setc(3, 1, DYN); setc(3, 2, GEM); setc(4, 1, WALL); setc(4, 2, ROCK)
walk(0x41, lambda: cell(2, 1) == LIT, limit=20)
check("kristal: B + direction on an empty cell places lit dynamite", cell(2, 1) == LIT and (hx(), hy()) == (1, 1)
      and peek(k, S, "dyns") == 1, f"{cell(2, 1)} ({hx()},{hy()})")
walk(0x04, lambda: hy() == 3)
burnt = set()
for _ in range(40):
    run(k, 1); burnt |= {(x, y) for x in range(0, 6) for y in range(0, 4) if cell(x, y) == BOOM}
run(k, 20, shot="kristal_dynamite.png")
check("kristal: the dynamite blows up the 3x3 round it", {(1, 1), (2, 1), (1, 2), (2, 2)} <= burnt and cell(2, 2) != ROCK
      and cell(1, 2) == EMPTY and not any(y in (0, 3) for _, y in burnt), sorted(burnt))
check("kristal: a stick in the blast goes off too (chain)", cell(4, 2) != ROCK and cell(3, 1) != DYN, f"{cell(4, 2)} {cell(3, 1)}")
check("kristal: crystals and steel walls stay", cell(3, 2) == GEM and cell(4, 1) == WALL and cell(1, 0) == WALL,
      f"{cell(3, 2)} {cell(4, 1)}")
check("kristal: the miner out of the blast lives", peek(k, S, "alive") == 1 and st() == ST_PLAY)
# the last stick, right above the miner, who stays: the blast kills him
walk(0x48, lambda: cell(1, 2) == LIT, limit=20)
run(k, 30)
check("kristal: the blast kills the miner", st() == ST_DEATH and "PATLADIN" in screen_text(k), f"state={st()}")

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
run(k, 40, shot="fitil_setup.png")
check("fitil: setup screen names the keys", "OYUNCU SAYISI" in screen_text(k) and "SOL SHIFT" in screen_text(k)
      and "OKLAR" in screen_text(k), screen_text(k))
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
hold(k, 0x04, 40)                         # ... and walks down, out of the flame's reach
check("fitil: P1 walked to safety", peek(k, S, "px") == 1 and peek(k, S, "py") >= 3, f"({peek(k, S, 'px')},{peek(k, S, 'py')})")
run(k, 130, shot="fitil_boom.png")
check("fitil: bomb exploded, P1 survived", G(1, 1) == 0 and peek(k, S, "palive") == 1)
# P2 and P3 blow themselves up: P1 wins the round
k.pads[1] = 0x80; k.pads[2] = 0x80
run(k, 2); k.pads[1] = 0; k.pads[2] = 0
run(k, 240, shot="fitil_round.png")
check("fitil: last player standing wins the round", peek(k, S, "pwins") == 1, f"wins={[peek(k, S, 'pwins', i) for i in range(4)]}")
check("fitil: round message", "OYUNCU TURU ALDI" in screen_text(k), screen_text(k))
# nobody leaves the arena: walking into the border is refused even if the grid said floor there
run(k, 100)                               # round 2 has started
k.wr(S["_grid"] + 1 * 15 + 0, 0)          # pretend the left border cell next to P1 were floor
hold(k, 0x02, 30)                         # P1 holds Left at (1,1)
check("fitil: the border is never walkable", peek(k, S, "round_no") == 2 and peek(k, S, "px") == 1,
      f"round {peek(k, S, 'round_no')} px {peek(k, S, 'px')}")

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
    if "DEVAM: SHIFT" in txt or "HAZIR: SHIFT" in txt:
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
check("launcher: home screen after the splash", k.test == 0xD1 and "MADENI KAZ" in txt and "7" in txt and "OYUN" in txt, txt)
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
hold(k, 0x08, 4)                                    # Up: hard drop (the redraw after the board was cleared takes 2 frames)
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

k, S = load_low("tilsim")
check("tilsim: fits below $8000 (loads at $0400)", len(rom("game_tilsim.bin")) <= 0x7C00)
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

# ---------------------------------------------------------------- koridor 64
k, S = load_low("koridor")
check("koridor: fits below $8000 (loads at $0400)", len(rom("game_koridor.bin")) <= 0x7C00)
run(k, 60, shot="koridor_title.png")
check("koridor: title names the keys", "SHIFT" in screen_text(k) and "OKLAR" in screen_text(k), screen_text(k))
hold(k, 0x10, 2); run(k, 20, shot="koridor_play.png")
check("koridor: level 1 with its HUD", "DEPO" in screen_text(k) and "MERMI" in screen_text(k), screen_text(k))
# the view is drawn: wall tiles (96..223) fill the middle row of the view
mid = [k.ppu.vram[0x4000 + 2 * (12 * 64 + c)] for c in range(40)]
check("koridor: walls in the view", all(96 <= t < 224 for t in mid), mid)
def setp(x, y, a):
    for name, v in (("px", int(x * 256)), ("py", int(y * 256)), ("ang", a)):
        k.wr(S["_" + name], v & 255); k.wr(S["_" + name] + 1, v >> 8)
f0 = k.rd(S["_frames_drawn"])
k.pads[0] = 0x01; run(k, 120); k.pads[0] = 0
fps = (k.rd(S["_frames_drawn"]) - f0) & 255
check("koridor: at least 20 frames a second while turning", fps >= 40, f"{fps} frames in 2 s")
run(k, 10)                                          # (the game still holds the last pad state for a frame)
setp(5.3, 2.5, 0)                                   # a demon straight ahead in the first room
for name, v in (("ex", 0x0780), ("ey", 0x0280)):
    k.wr(S["_" + name], v & 255); k.wr(S["_" + name] + 1, v >> 8)
run(k, 6, shot="koridor_demon.png")
oam = [k.rd(0x300 + 4 * i + 2) for i in range(64) if k.rd(0x300 + 4 * i) < 240]
check("koridor: the demon is drawn as sprites", len(oam) > 8, oam)
for _ in range(6):
    if k.rd(S["_kills"]): break
    k.pads[0] = 0x80; run(k, 4); k.pads[0] = 0; run(k, 14)
check("koridor: shooting the demon kills it", k.rd(S["_kills"]) == 1 and k.rd(S["_ammo"]) < 30,
      f"kills {k.rd(S['_kills'])} ammo {k.rd(S['_ammo'])}")
setp(2.5, 4.6, 256)                                 # walk south into the door at (2,6)
k.pads[0] = 0x08; run(k, 40); k.pads[0] = 0; run(k, 4)
check("koridor: walking into a door opens it", k.rd(S["_map"] + 6 * 32 + 2) == 0)
check("koridor: walls stop the player", k.rd(S["_py"] + 1) < 9)
setp(17.5, 10.6, 256)                               # the exit portal at (17,11)
k.pads[0] = 0x08; run(k, 30); k.pads[0] = 0; run(k, 30, shot="koridor_exit.png")
check("koridor: the portal ends the level", "BOLUM BITTI" not in screen_text(k) and k.rd(S["_won"]) == 1, screen_text(k))
hold(k, 0x10, 4); run(k, 40)
check("koridor: level 2 follows", k.rd(S["_level"]) == 2 and "REAKTOR" in screen_text(k), screen_text(k))
k.keys[0x76 >> 3] |= 1 << (0x76 & 7)                # Esc: back to the menu
run(k, 3)
check("koridor: Esc leaves the game", k.pc >= 0xE000, f"pc {k.pc:04X}")

# ---------------------------------------------------------------- sound: APU model, sound engine
import math
import numpy as np
from apu_model import APU, render_log, tone_stats
def model_mean(writes, frames=30):
    a = APU()
    for r, v in writes: a.write(r, v)
    return float(np.concatenate([a.render(735) for _ in range(frames)]).mean())
sq = [(0x10, 15), (0x11, 15), (0, 194), (1, 0), (2, 0x1F), (3, 0)]
f, _ = tone_stats([(0, r, v) for r, v in sq], 60)
check("APU model: square P=194 at 1001.6 Hz (as tb_apu)", abs(f - 1001.6) < 4, f"{f:.1f} Hz")
m = model_mean(sq)
check("APU model: 25 % duty, volume 15, master 15 -> mean level 63.3", abs(m - 63.3) < 1.5, f"{m:.1f}")
f, _ = tone_stats([(0, r, v) for r, v in [(0x10, 15), (0x11, 15), (8, 48), (9, 0), (10, 0x0F), (11, 0)]], 60)
check("APU model: triangle P=48 at 996.5 Hz", abs(f - 996.5) < 4, f"{f:.1f} Hz")
_, rms = tone_stats([(0, r, v) for r, v in [(0x10, 15), (0x11, 15), (12, 20), (13, 0), (14, 0x0F), (15, 0)]], 30)
check("APU model: noise is heard", rms > 0.02, f"rms {rms:.3f}")

# APU v2 in the model, with the numbers tb_apu checks in VHDL
def model_v2(writes, ticks=0, frames=10):
    a = APU(2)
    for r, v in writes:
        if r == "tick": a.frame_tick()
        else: a.write(r, v)
    for _ in range(ticks): a.frame_tick()
    return float(np.concatenate([a.render(735) for _ in range(frames)]).mean())
base = [(0x10, 15), (0x11, 0x1F)]
m = model_v2(base + [(0x20 + i, 0xFF) for i in range(8)] + [(0x14, 48), (0x15, 0), (0x16, 15), (0x17, 0)])
check("APU v2 model: wave channel level (tb_apu: 112..124)", 112 < m < 124, f"{m:.1f}")
m = model_v2(base + [(0x1A, 0x20), (0, 194), (1, 0), (2, 0x2C), (3, 0)], ticks=4, frames=2)
check("APU v2 model: attack after 4 frames (tb_apu: 12..22)", 12 < m < 22, f"{m:.1f}")
m = model_v2(base + [(0x1A, 0x06), (0, 194), (1, 0), (2, 0xAF), (3, 0)], ticks=20, frames=2)
check("APU v2 model: decay stops at the sustain volume (tb_apu: 45..56)", 45 < m < 56, f"{m:.1f}")
a = APU(2)
for r, v in base + [(4, 200), (5, 0), (6, 0x2F), (7, 0), (0x19, 0x8B)]: a.write(r, v)
a.frame_tick()
check("APU v2 model: sweep 200 -> 175", a.period[1] == 175, a.period[1])

def load_player(name, ver=2):
    k = Konsol(rom("boot.bin"))
    k.apu_version = ver
    g = rom(f"sesoyna_{name}.bin")
    k.ram[0x400:0x400 + len(g)] = g
    if os.path.exists(os.path.join(B, f"sesoyna_{name}.bin.ek")):
        k.cart = bytearray(rom(f"sesoyna_{name}.bin.ek")); k.cart_addr = 0
    k.pc = 0x400
    syms = {}
    for l in open(os.path.join(B, f"sesoyna_{name}.lbl")):
        _, addr, label = l.split()
        syms[label.lstrip(".")] = int(addr, 16)
    run(k, 5)
    return k, syms
NOTE_NAMES = "C C# D D# E F F# G G# A A# B".split()
def note_of(period, tri=False):
    f = 1562500 / ((32 if tri else 8) * (period + 1))
    m = round(12 * math.log2(f / 440) + 57)
    return f"{NOTE_NAMES[m % 12]}{m // 12}"
def channel_notes(log, ch, frames, tri=False):
    """the notes channel ch starts (period changes while its volume is up), in order"""
    per, vol, out = 0, 0, []
    byf = {}
    for f, r, v in log: byf.setdefault(f, []).append((r, v))
    for f in range(frames):
        for r, v in byf.get(f, ()):
            if r == 4 * ch: per = (per & 0xF00) | v
            elif r == 4 * ch + 1: per = (per & 0xFF) | (v & 15) << 8
            elif r == 4 * ch + 2: vol = v & 15
        n = note_of(per, tri) if vol else None
        if n and (not out or out[-1] != n): out.append(n)
        if not vol and out and out[-1] is not None: out.append(None)
    return [n for n in out if n]

k, S = load_player("deneme", 1)
cyc = {"in": False, "n": 0, "sum": 0, "max": 0}
tick, step0 = S["_ses_tick"], k.step
def timed_step():
    if not cyc["in"] and k.pc == tick:
        cyc.update({"in": True, "sp": k.sp, "c0": k.cycles})
    step0()
    if cyc["in"] and k.sp == cyc["sp"] + 2:
        c = k.cycles - cyc["c0"]; cyc["in"] = False
        cyc["n"] += 1; cyc["sum"] += c; cyc["max"] = max(cyc["max"], c)
k.step = timed_step
k.wr(S["_secim"], 0); k.wr(S["_komut"], 1)
k.apu_log = []
f0 = k.frames
run(k, 240)
log = [(f - f0, r, v) for f, r, v in k.apu_log]
mel = channel_notes(log, 0, 240)
check("ses: the test song's melody (square 1)", mel[:5] == ["A5", "G5", "E5", "C5", "D5"], mel[:8])
arp = channel_notes(log, 1, 60)
check("ses: arpeggio chords on square 2 (A minor)", set(arp[:6]) == {"A4", "C5", "E5"}, arp[:8])
bass = channel_notes(log, 2, 240, tri=True)
check("ses: triangle bass and kick", "A3" in bass and len(bass) > 4, bass[:10])
check("ses: noise drums", sum(1 for f, r, v in log if r == 14 and v & 15) >= 8)
check("ses: no wave channel writes on an APU v1", not any(0x14 <= r <= 0x2F for f, r, v in log))
check("ses: version 1 detected", k.rd(S["_surum"]) == 1)
# an effect borrows square 1, then the music has it back
k.wr(S["_secim"], 0); k.wr(S["_komut"], 2)
k.apu_log = []
f0 = k.frames
run(k, 90)
log = [(f - f0, r, v) for f, r, v in k.apu_log]
fx = channel_notes(log, 0, 40)
check("ses: the effect plays on square 1 (C5 E5 G5 C6 ...)", fx[:4] == ["C5", "E5", "G5", "C6"], fx[:8])
check("ses: after the effect the music has square 1 again", k.rd(S["_ses_on"] + 5) == 0 and k.rd(S["_ses_on"]) == 1
      and any(f > 50 and r == 2 and v & 15 for f, r, v in log))
k.step = step0
avg = cyc["sum"] // max(cyc["n"], 1)
check("ses: the engine takes under 9 % of a frame (avg), 15 % at most", avg < 9400 and cyc["max"] < 15600,
      f"avg {avg} max {cyc['max']} cycles of {6250000 // 60}")
run(k, 700)
check("ses: the song loops back (L)", k.rd(S["_ses_playing"]) == 1)
k.wr(S["_komut"], 3); run(k, 3)
check("ses: stop", k.rd(S["_ses_playing"]) == 0 and not any(k.rd(S["_ses_on"] + i) for i in range(5)))
# APU v2: the wave channel plays (its wave RAM loaded from the instrument's wave)
k, S = load_player("deneme", 2)
check("ses: APU v2 detected", k.rd(S["_surum"]) == 2)
k.wr(S["_secim"], 0); k.wr(S["_komut"], 1)
k.apu_log = []
f0 = k.frames
run(k, 200)
log = [(f - f0, r, v) for f, r, v in k.apu_log]
vox = [8, 13, 15, 14, 10, 6, 5, 7, 11, 12, 10, 6, 3, 2, 4, 7, 8, 8, 6, 3, 1, 1, 3, 6, 8, 9, 7, 4, 2, 2, 4, 6]
check("ses: v2 wave RAM holds the 'vox' wave", list(k.apu[0x20:0x30]) == [vox[2 * i] << 4 | vox[2 * i + 1] for i in range(16)],
      list(k.apu[0x20:0x30]))
wv = []
per, vol = 0, 0
for f, r, v in log:
    if r == 0x14: per = (per & 0xF00) | v
    elif r == 0x15: per = (per & 0xFF) | (v & 15) << 8
    elif r == 0x16: vol = v & 15
    if r == 0x16 and vol:
        n = note_of(per, tri=True)
        if not wv or wv[-1] != n: wv.append(n)
check("ses: v2 wave channel plays its part (E4 F4 ...)", wv[:2] == ["E4", "F4"], wv[:6])
check("ses: v2 channel mask has the wave channel", (0x11, 0x1F) in [(r, v) for f, r, v in k.apu_log] or k.apu[0x11] == 0x1F)

# the library's effects through k_sfx (the old SFX_ ids)
k, S = load_player("std")
for i in range(8):
    k.wr(S["_secim"], i); k.wr(S["_komut"], 2)
    k.apu_log = []
    run(k, 4)
    ok = any(r in (2, 6, 10, 14) and v & 15 for f, r, v in k.apu_log)
    if not ok: break
    run(k, 120)
check("ses: the 8 standard effects play", ok, f"effect {i}")

# a recording renders to sound (the registers set before it count too)
sys.path.insert(0, os.path.join(HERE, "../../tools"))
import sesdinle
kd, Sd = sesdinle.player("deneme")
lg, nf = sesdinle.record(kd, Sd, 1, 0, 120, True)
x = render_log(lg, nf, 2)
check("ses: a recorded song renders to sound (sesdinle)", float(np.sqrt((x ** 2).mean())) > 0.03,
      f"rms {float(np.sqrt((x ** 2).mean())):.4f}")

# ---------------------------------------------------------------- every game has its own music
def plays_music(k, S, frames=120):
    k.apu_log = []
    run(k, frames)
    loud = {r >> 2 for f, r, v in k.apu_log if r < 16 and (r & 3) == 2 and v & 15}
    return k.rd(S["_ses_playing"]) == 1 and len(loud) >= 2, loud
for g, low in (("blok", 0), ("yildiz", 0), ("fitil", 0), ("olimpiyat", 0), ("koridor", 1), ("tilsim", 1)):
    k, S = load_low(g) if low else load_game(g)
    ok, loud = plays_music(k, S)
    check(f"{g}: its title music plays (channels {sorted(loud)})", ok)
    check(f"{g}: no old k_sfx effects linked", "_sfx_std_blip" not in S)
k, S = load_low("tilsim")
ek = rom("game_tilsim.bin.ek")
run(k, 5)
check("tilsim: the music came from the cartridge's extra data to $B000",
      len(ek) > 1000 and bytes(k.rd(0xB000 + i) for i in range(len(ek))) == ek, f"{len(ek)} bytes")
k = Konsol(rom("boot.bin"), rom("cart.bin"))
k.apu_log = []
run(k, 90)
check("launcher: the opening jingle plays with the sparks", any(r == 2 and v & 15 for f, r, v in k.apu_log))
# the real cartridge: Tilsim loaded by the boot ROM finds its music after itself
full = rom("cart.bin")
k = Konsol(rom("boot.bin"), full)
k.pads[0] = 0x20; run(k, 140); k.pads[0] = 0; run(k, 5)      # Select at power-up: the boot ROM menu
for _ in range(3):
    k.pads[0] = 0x04; run(k, 3); k.pads[0] = 0; run(k, 3)    # down to TILSIM (4th entry)
k.pads[0] = 0x80; run(k, 3); k.pads[0] = 0
for _ in range(400):
    run(k, 1)
    if k.test == 0xE0: break
run(k, 30)
check("tilsim from the cartridge: extra data read in after loading", k.test == 0xE0
      and bytes(k.rd(0xB000 + i) for i in range(len(ek))) == ek, f"test {k.test}")

# no cartridge
k = run(Konsol(rom("boot.bin"), b""), 150)
check("missing cartridge message", "KARTUS BULUNAMADI" in screen_text(k), screen_text(k))

print(f"{'all software tests passed' if not failed else str(failed) + ' software test(s) failed'}")
sys.exit(1 if failed else 0)
