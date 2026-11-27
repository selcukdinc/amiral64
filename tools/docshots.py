#!/usr/bin/env python3
"""Screenshots for the README (docs/NN_name.png), taken on the Python konsol model.

    python3 tools/docshots.py [scene ...]      (default: every scene; run `make -C sw test` first)

Most pictures are the software tests' own screenshots (sw/build/shots, the tests play the games to those
screens), copied to docs/ under the README's names. The other scenes load a game or the cartridge, play a
few scripted inputs and save one 640x480 frame.
"""
import os, sys, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from emu6502 import Konsol, write_png

ROOT = os.path.join(HERE, "..")
B = os.path.join(ROOT, "sw", "build")
DOCS = os.path.join(ROOT, "docs")


def rom(name):
    return open(os.path.join(B, name), "rb").read()


def load(name, low=False):
    k = Konsol(rom("boot.bin"))
    g = rom(f"game_{name}.bin")
    if low:
        k.ram[0x400:0x400 + len(g)] = g
        k.pc = 0x400
        if os.path.exists(os.path.join(B, f"game_{name}.bin.ek")):   # extra data: where the loader leaves the port
            k.cart = bytearray(rom(f"game_{name}.bin.ek")); k.cart_addr = 0
    else:
        k.game[:len(g)] = g
        k.pc = 0x9000
    syms = {}
    for l in open(os.path.join(B, f"game_{name}.lbl")):
        _, addr, label = l.split()
        syms[label.lstrip(".")] = int(addr, 16)
    return k, syms


def run(k, frames, pads=None):
    for _ in range(frames):
        if pads:
            for i, v in enumerate(pads):
                k.pads[i] = v
        k.frame()
    if pads:
        k.pads[:] = [0, 0, 0, 0]


def shot(k, name):
    pic = k.frame(render=True)
    write_png(os.path.join(DOCS, name), pic)
    print("wrote docs/" + name)


def fitil():
    k, S = load("fitil")
    run(k, 40)
    run(k, 2, [0x08]); run(k, 2)            # 3 players
    run(k, 2, [0x10]); run(k, 20)
    run(k, 1, [0x80]); run(k, 2)            # P1 bomb at (1,1)
    run(k, 40, [0x04])                      # P1 walks down
    run(k, 1, [0, 0, 0x80]); run(k, 2)      # P3 bomb too
    run(k, 70, [0, 0, 0x02])                # P3 walks away
    for _ in range(80):                     # wait for the first flame
        run(k, 1)
        if any(k.rd(S["_flame"] + i) for i in range(15 * 11)):
            break
    run(k, 4)
    shot(k, "05_fitil.png")


def kristal_level(i, moves, name):
    """Kristal Ocak: level i (from the select screen, every level open), its stored solution played for a while"""
    from kristal_sim import parse_level, decode
    lv = os.path.join(ROOT, "sw", "games", "kristal", "levels")
    keys, _ = parse_level(os.path.join(lv, sorted(os.listdir(lv))[i]))
    k, S = load("kristal", low=True)
    b = [75, 79, 9] + [3, 2, 3, 1, 0, 0, 0, 0, 0] + [0x40, 0x06]        # a save: all open, a few stars
    c = 0x5A
    for v in b[2:]:
        c = (((c << 1) | (c >> 7)) & 255) ^ v
    k.ram[0x2E8:0x2F7] = bytes(b + [c])
    run(k, 60); run(k, 2, [0, 0x10]); run(k, 12)
    if i < 0:                                       # the select screen itself
        run(k, 2, [0x01]); run(k, 40)
        return shot(k, name)
    for _ in range(i):
        run(k, 2, [0x01]); run(k, 4)
    run(k, 2, [0x10]); run(k, 30); run(k, 2, [0x80]); run(k, 3)
    btn = {"": 0, "U": 0x08, "D": 0x04, "L": 0x02, "R": 0x01}
    for m in decode(keys["solution"])[:moves]:
        k.pads[0] = btn[m[1]] | 0x40 if m.startswith("b") else btn[m]
        s0 = k.rd(S["_steps"])
        for _ in range(14):
            k.frame()
            if k.rd(S["_steps"]) != s0:
                break
    k.pads[0] = 0
    run(k, 3)
    shot(k, name)


def kristal():
    kristal_level(5, 38, "03_kristal.png")              # 2-3: the mine carts
    kristal_level(8, 52, "15_kristal_karanlik.png")     # 3-3: dark, the lantern
    kristal_level(-1, 0, "16_kristal_secim.png")        # the mine select


# the README's picture <- the test's screenshot (sw/tests/test_sw.py)
FROM_TESTS = {
    "04_yildiz.png": "yildiz_play.png",
    "06_olimpiyat.png": "olimpiyat_kosu.png",
    "08_launcher.png": "launcher_blok.png",
    "09_blok.png": "blok_play.png",
    "10_tilsim_pazar.png": "tilsim_shop.png",
    "11_tilsim.png": "tilsim_scored.png",
    "12_tilsim_baslik.png": "tilsim_title.png",
    "13_tilsim_harita.png": "tilsim_map.png",
    "14_koridor.png": "koridor_demon.png",
}


def from_tests():
    for doc, src in FROM_TESTS.items():
        path = os.path.join(B, "shots", src)
        if not os.path.exists(path):
            sys.exit(f"{path} is missing: run make -C sw test first")
        shutil.copyfile(path, os.path.join(DOCS, doc))
        print(f"wrote docs/{doc} (from the tests' {src})")


def menu():
    """the boot ROM's own menu with the real cartridge (Select held at power-up)"""
    k = Konsol(rom("boot.bin"), rom("cart.bin"))
    run(k, 140, [0x20]); run(k, 30)
    shot(k, "02_menu.png")


SCENES = {"tests": from_tests, "menu": menu, "fitil": fitil, "kristal": kristal}

if __name__ == "__main__":
    for name in sys.argv[1:] or SCENES:
        SCENES[name]()
