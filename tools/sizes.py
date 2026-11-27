#!/usr/bin/env python3
"""ROM / RAM use of every C game, from the ld65 map files (sw/build/game_*.map).

    python3 tools/sizes.py [--md docs/boyutlar.md]

Prints a table: binary size, room left in the load area ($9000: 20 KB, $0400: 30 KB), code, read-only
data, variables (BSS) and zero page. With --md it also writes the table as Markdown.
"""
import os, re, sys, glob, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "..", "sw", "build")
LOW = {"tilsim", "koridor"}                      # loaded at $0400 (sw/Makefile LOW_GAMES)


def segments(path):
    seg, on = {}, False
    for line in open(path):
        if line.startswith("Segment list"):
            on = True
            continue
        if on:
            m = re.match(r"(\w+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})", line)
            if m:
                seg[m.group(1)] = int(m.group(4), 16)
            elif line.startswith("Exports"):
                break
    return seg


def rows():
    out = []
    for m in sorted(glob.glob(os.path.join(B, "game_*.map"))):
        name = os.path.basename(m)[5:-4]
        s = segments(m)
        binsize = os.path.getsize(os.path.join(B, f"game_{name}.bin"))
        limit = 0x7800 if name in LOW else 0x5000
        rodata = s.get("RODATA", 0) + s.get("DATA", 0)
        out.append((name, "$0400" if name in LOW else "$9000", binsize, limit - binsize, s.get("CODE", 0),
                    rodata, s.get("BSS", 0), s.get("ZEROPAGE", 0)))
    return out


HEAD = ("oyun", "yükleme", "ikili", "boş yer", "kod", "veri", "BSS (RAM)", "zero page")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--md")
    a = ap.parse_args()
    r = rows()
    print("%-10s %-7s %7s %8s %7s %7s %9s %9s" % HEAD)
    for x in r:
        print("%-10s %-7s %7d %8d %7d %7d %9d %9d" % x)
    if a.md:
        with open(a.md, "w") as f:
            f.write("# Oyunların bellek kullanımı\n\n")
            f.write("`python3 tools/sizes.py --md docs/boyutlar.md` ile üretilir (`make -C sw` sonrası). Bayt cinsinden.\n")
            f.write("`$9000`'dan yüklenen oyunlar için yer 20 KB (20480), `$0400`'dan yüklenenler için 30 KB (30720).\n\n")
            f.write("| " + " | ".join(HEAD) + " |\n|" + "---|" * len(HEAD) + "\n")
            for x in r:
                f.write("| " + " | ".join(str(v) for v in x) + " |\n")
        print("wrote " + a.md)
