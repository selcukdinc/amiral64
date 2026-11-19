#!/usr/bin/env python3
"""Reference model of the konsol PPU (hw/ppu.vhd) and a scene generator for its testbench.

    ppu_model.py scene <seed> <out_dir>   writes ppu_scene.txt (register/VRAM writes for the testbench)
                                          and ppu_expect.txt (240 lines of 320 12-bit colours, hex)
Used by tests: the testbench programs the PPU with the scene and compares every pixel of a frame.
"""
import random, sys, os

W, H = 320, 240

class PPU:
    def __init__(self):
        self.vram = bytearray(0x5300)
        self.ctrl = 0
        self.scroll_x = 0
        self.scroll_y = 0
        self.sbg = self.sss = self.over = False

    def palette(self, i):
        lo, hi = self.vram[0x5000 + 2 * i], self.vram[0x5001 + 2 * i]
        return ((hi & 15) << 8) | lo

    def line(self, L):
        buf = [0] * W          # entry: (sprite<<9) | (opaque<<8) | index
        if self.ctrl & 2:
            my = (L + self.scroll_y) & 255
            row, fy = my >> 3, my & 7
            for px in range(W):
                mx = (px + self.scroll_x) & 511
                col, fx = mx >> 3, mx & 7
                e = 0x4000 + 2 * (row * 64 + col)
                tile, attr = self.vram[e], self.vram[e + 1]
                ty = 7 - fy if attr & 0x20 else fy
                tx = 7 - fx if attr & 0x10 else fx
                b = self.vram[tile * 32 + ty * 4 + tx // 2]
                c = (b >> 4) if tx % 2 == 0 else (b & 15)
                buf[px] = 0 if c == 0 else (0x100 | (attr & 15) << 4 | c)
        if self.ctrl & 4:
            listed = []
            for i in range(64):
                y = self.vram[0x5200 + 4 * i]
                if y < 240 and 0 <= L - y < 16:
                    if len(listed) < 16:
                        listed.append(i)
                    else:
                        self.over = True
            for i in reversed(listed):
                y, xl, pat, attr = self.vram[0x5200 + 4 * i: 0x5204 + 4 * i]
                x = xl | ((attr & 0x80) << 1)
                r = L - y
                if attr & 0x20: r = 15 - r
                for p in range(16):
                    px = x + p
                    if px >= W: continue
                    q = 15 - p if attr & 0x10 else p
                    b = self.vram[0x2000 + (pat & 63) * 128 + r * 8 + q // 2]
                    c = (b >> 4) if q % 2 == 0 else (b & 15)
                    if c == 0: continue
                    e = buf[px]
                    if e & 0x200: self.sss = True
                    if e & 0x100: self.sbg = True
                    if attr & 0x40 and e & 0x100: continue
                    buf[px] = 0x200 | (e & 0x100) | (attr & 15) << 4 | c
        return [self.palette(e & 255) for e in buf]

    def frame(self):
        return [self.line(L) for L in range(H)]

def scene(seed):
    rnd = random.Random(seed)
    p = PPU()
    writes = []   # ("V", addr, byte) VRAM, ("R", reg, byte) register
    def v(a, b):
        p.vram[a] = b
    for i in range(256):                 # palette
        col = rnd.randrange(4096) if i else 0x123
        v(0x5000 + 2 * i, col & 255); v(0x5001 + 2 * i, col >> 8)
    for a in range(0x0000, 0x4000):      # tiles and sprite patterns, ~35 % transparent pixels
        hi = rnd.randrange(16) if rnd.random() > 0.35 else 0
        lo = rnd.randrange(16) if rnd.random() > 0.35 else 0
        v(a, hi << 4 | lo)
    for e in range(2048):                # map
        v(0x4000 + 2 * e, rnd.randrange(256))
        v(0x4001 + 2 * e, rnd.randrange(8) | rnd.choice([0, 0x10, 0x20, 0x30]))
    for i in range(64):                  # sprites; 20 of them share lines 100..115 (overflow)
        y = 100 if i < 20 else rnd.choice([rnd.randrange(240), 245])
        x = rnd.randrange(330)
        attr = rnd.randrange(8, 16) | rnd.choice([0, 0x10, 0x20, 0x30]) | (0x40 if rnd.random() < 0.25 else 0)
        if x & 256: attr |= 0x80
        v(0x5200 + 4 * i, y); v(0x5201 + 4 * i, x & 255); v(0x5202 + 4 * i, rnd.randrange(64)); v(0x5203 + 4 * i, attr)
    # VRAM in address order through VADDR/VDATA (auto increment)
    writes.append(("R", 0, 0)); writes.append(("R", 1, 0))
    for a in range(0x5300):
        writes.append(("V", a, p.vram[a]))
    p.scroll_x, p.scroll_y, p.ctrl = 13 + 256 * (seed & 1), 21, 0x07
    writes += [("R", 5, p.scroll_x & 255), ("R", 6, p.scroll_x >> 8), ("R", 7, p.scroll_y), ("R", 3, p.ctrl)]
    return p, writes

def main():
    if len(sys.argv) != 4 or sys.argv[1] != "scene":
        sys.exit(__doc__)
    seed, out = int(sys.argv[2]), sys.argv[3]
    p, writes = scene(seed)
    with open(os.path.join(out, "ppu_scene.txt"), "w") as f:
        for kind, a, b in writes:
            # testbench format: register number (0..15) and data byte; VRAM bytes go through VDATA (reg 2)
            f.write(f"{2 if kind == 'V' else a:x} {b:02x}\n")
    frame = p.frame()
    with open(os.path.join(out, "ppu_expect.txt"), "w") as f:
        for row in frame:
            f.write(" ".join(f"{c:03x}" for c in row) + "\n")
    flags = (0x40 if p.sbg else 0) | (0x20 if p.sss else 0) | (0x08 if p.over else 0)
    with open(os.path.join(out, "ppu_flags.txt"), "w") as f:
        f.write(f"{flags:02x}\n")

if __name__ == "__main__":
    main()
