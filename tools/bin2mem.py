#!/usr/bin/env python3
"""Turns a binary (e.g. a cc65/ld65 output) into a memory init file for $readmemh or VHDL textio.

    bin2mem.py in.bin out.mem [--width 8|16|32] [--size N] [--fill 0xFF]

One word per line in hex (most significant byte first for wider words, little-endian input order).
--size pads (with --fill) or checks the depth in words, so the HDL array and the file always agree.
"""
import argparse, sys

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--width", type=int, default=8, choices=(8, 16, 32))
    ap.add_argument("--size", type=lambda s: int(s, 0), help="depth in words")
    ap.add_argument("--fill", type=lambda s: int(s, 0), default=0xFF)
    a = ap.parse_args()
    data = open(a.input, "rb").read()
    n = a.width // 8
    if len(data) % n:
        data += bytes([a.fill & 0xFF]) * (n - len(data) % n)
    words = [int.from_bytes(data[i:i + n], "little") for i in range(0, len(data), n)]
    if a.size is not None:
        if len(words) > a.size:
            sys.exit(f"bin2mem: {a.input} has {len(words)} words, more than --size {a.size}")
        fill = int.from_bytes(bytes([a.fill & 0xFF]) * n, "little")
        words += [fill] * (a.size - len(words))
    digits = a.width // 4
    with open(a.output, "w") as f:
        f.write("".join(f"{w:0{digits}x}\n" for w in words))

if __name__ == "__main__":
    main()
