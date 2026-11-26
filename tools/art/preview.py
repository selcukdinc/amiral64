#!/usr/bin/env python3
"""preview.py art.txt out.png: every tile/big/sprite of an art file on a grid (4x), each with its palette
(the palette listed most recently before it)."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from emu6502 import write_png

lines = [l.rstrip("\n") for l in open(sys.argv[1])]
items, pal, i = [], [0] * 16, 0
while i < len(lines):
    l = lines[i].strip(); i += 1
    if not l or l.startswith("#"): continue
    kw, *rest = l.split()
    if kw == "palette":
        pal = [int(c, 16) for c in rest[1:]] + [0] * 16; continue
    n = 8 if kw == "tile" else 16
    items.append((rest[0], [lines[i + k].strip() for k in range(n)], pal[:16])); i += n
# 2x2 groups of bigs named X_0..X_3 are drawn as one 32x32 picture
merged = []
k = 0
while k < len(items):
    n = items[k][0]
    if n.endswith("_0") and k + 3 < len(items) and items[k + 3][0] == n[:-1] + "3" and len(items[k][1]) == 16:
        rows = [items[k][1][y] + items[k + 1][1][y] for y in range(16)] + [items[k + 2][1][y] + items[k + 3][1][y] for y in range(16)]
        merged.append((n[:-2], rows, items[k][2])); k += 4
    else:
        merged.append(items[k]); k += 1
items = merged
S, cell = 2, 36
cols = 12
rows = (len(items) + cols - 1) // cols
W, H = cols * cell * S, rows * cell * S
img = [[0x334] * W for _ in range(H)]
for k, (name, rows_, p) in enumerate(items):
    ox, oy = (k % cols) * cell * S, (k // cols) * cell * S
    for y, r in enumerate(rows_):
        for x, ch in enumerate(r):
            v = 0 if ch == "." else int(ch, 16)
            c = p[v]
            rgb = c if v else 0x505
            for dy in range(S):
                for dx in range(S):
                    if oy + y * S + dy < H and ox + x * S + dx < W:
                        img[oy + y * S + dy][ox + x * S + dx] = rgb
write_png(sys.argv[2], img, scale=2)
