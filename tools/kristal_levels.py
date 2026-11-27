#!/usr/bin/env python3
"""Kristal Ocak levels: sw/games/kristal/levels/<mine>-<n>.txt (see tools/kristal_sim.py) -> a C header with
the levels packed two cells to a byte, their names, targets, times and one-line introductions.

    python3 tools/kristal_levels.py build/kristal/levels.h sw/games/kristal/levels/*.txt
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kristal_sim import parse_level, decode, W, H

CELLS = " .WO*+EPCDGRAY"          # the nibble codes (main.c: cells[])


def main():
    out, files = sys.argv[1], sorted(sys.argv[2:], key=lambda f: os.path.basename(f))
    lv = []
    for f in files:
        m = re.match(r"(\d)-(\d)\.txt$", os.path.basename(f))
        keys, rows = parse_level(f)
        cells = "".join(rows)
        packed = [CELLS.index(cells[i]) << 4 | CELLS.index(cells[i + 1]) for i in range(0, W * H, 2)]
        lv.append((int(m.group(1)), int(m.group(2)), keys, packed))
    n = len(lv)
    q = lambda s: '"' + s.replace('"', "") + '"'
    o = [f"/* made by tools/kristal_levels.py from games/kristal/levels/<mine>-<n>.txt: do not edit */",
         f"#define NLEVEL {n}",
         f'static const char cells[] = "{CELLS}";',
         "static const unsigned char lv_mine[NLEVEL] = { " + ", ".join(str(a - 1) for a, _, _, _ in lv) + " };",
         "static const unsigned char lv_num[NLEVEL] = { " + ", ".join(str(b) for _, b, _, _ in lv) + " };",
         "static const unsigned char need[NLEVEL] = { " + ", ".join(k["need"] for _, _, k, _ in lv) + " };",
         "static const unsigned char lv_time[NLEVEL] = { " + ", ".join(k["time"] for _, _, k, _ in lv) + " };",
         "static const char *const lv_name[NLEVEL] = { " + ", ".join(q(k["name"]) for _, _, k, _ in lv) + " };",
         "static const char *const lv_intro[NLEVEL] = {\n    " + ",\n    ".join(q(k["intro"]) for _, _, k, _ in lv) + " };"]
    for i, (_, _, _, p) in enumerate(lv):
        o.append(f"static const unsigned char lv_map{i}[{len(p)}] = {{\n    " +
                 ",\n    ".join(", ".join(str(v) for v in p[r:r + 16]) for r in range(0, len(p), 16)) + " };")
    o.append("static const unsigned char *const lv_map[NLEVEL] = { " + ", ".join(f"lv_map{i}" for i in range(n)) + " };")
    # the title's demo: the first level's solution as (buttons, steps) pairs
    BTN = {"": 0, "U": 0x08, "D": 0x04, "L": 0x02, "R": 0x01}
    moves = decode(lv[0][2].get("solution", ""))
    runs = []
    for m in moves:
        b = BTN[m[1]] | 0x40 if m.startswith("b") else BTN[m]
        if runs and runs[-1][0] == b and runs[-1][1] < 255:
            runs[-1][1] += 1
        else:
            runs.append([b, 1])
    runs.append([0, 20])
    o.append("static const unsigned char demo_path[] = {\n    " +
             ",\n    ".join(", ".join(f"{b}, {c}" for b, c in runs[i:i + 8]) for i in range(0, len(runs), 8)) + " };")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "w").write("\n".join(o) + "\n")


if __name__ == "__main__":
    main()
