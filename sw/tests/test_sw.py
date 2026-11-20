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
cart = rom("cart.bin")
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

# no cartridge
k = run(Konsol(rom("boot.bin"), b""), 150)
check("missing cartridge message", "KARTUS BULUNAMADI" in screen_text(k), screen_text(k))

print(f"{'all software tests passed' if not failed else str(failed) + ' software test(s) failed'}")
sys.exit(1 if failed else 0)
