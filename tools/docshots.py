#!/usr/bin/env python3
"""Screenshots for the README (docs/NN_name.png), taken on the Python konsol model.

    python3 tools/docshots.py [scene ...]      (default: every scene; run `make -C sw` first)

Each scene loads a game, plays a few scripted inputs and saves one 640x480 frame.
"""
import os, sys
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


SCENES = {"fitil": fitil}

if __name__ == "__main__":
    for name in sys.argv[1:] or SCENES:
        SCENES[name]()
