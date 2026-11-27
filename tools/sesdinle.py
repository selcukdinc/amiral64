#!/usr/bin/env python3
"""Listen to the konsol's music without the board: plays every song and sound effect of a music file on
the Python model (the real sound engine running on the emulated 6502) and writes WAV files.

    python3 tools/sesdinle.py NAME ... [--saniye 40] [--v2]     (make -C sw ses-dinle does all of them)

NAME: std (the library's effects), deneme (the test song) or a game with games/NAME/muzik.ses.
Needs sw/build/sesoyna_NAME.bin (make -C sw build/sesoyna_NAME.bin). Writes sw/build/ses/NAME_*.wav.
A song is recorded until it ends or loops back (at most --saniye seconds), an effect until it is quiet.
"""
import os, sys, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from emu6502 import Konsol
from apu_model import render_log, write_wav

B = os.path.join(HERE, "..", "sw", "build")
OUT = os.path.join(B, "ses")


def player(name, v2=False):
    k = Konsol(open(os.path.join(B, "boot.bin"), "rb").read())
    k.apu_version = 2 if v2 else 1
    g = open(os.path.join(B, f"sesoyna_{name}.bin"), "rb").read()
    k.ram[0x400:0x400 + len(g)] = g
    k.pc = 0x400
    syms = {}
    for l in open(os.path.join(B, f"sesoyna_{name}.lbl")):
        _, addr, label = l.split()
        syms[label.lstrip(".")] = int(addr, 16)
    for _ in range(5):
        k.frame()
    return k, syms


def rd16(k, a):
    return k.rd(a) | k.rd(a + 1) << 8


def items(k, syms, lst):
    out, a = [], syms[lst]
    while rd16(k, a):
        out.append(rd16(k, a))
        a += 2
    return out


def names(k, syms, prefix, addrs):
    by_addr = {v: n[len(prefix):] for n, v in syms.items() if n.startswith(prefix)}
    return [by_addr.get(a, f"{i}") for i, a in enumerate(addrs)]


def song_frames(name):
    """frames of one pass of each song (from the header sesc writes)"""
    import re
    h = os.path.join(B, name, "muzik.h")
    out = {}
    if os.path.exists(h):
        for m in re.finditer(r"#define MUS_(\w+)_KARE (\d+)", open(h).read()):
            out[m.group(1).lower()] = int(m.group(2))
    return out


def record(k, syms, cmd, idx, max_frames, song):
    k.wr(syms["_secim"], idx)
    k.wr(syms["_komut"], cmd)
    start = k.frames
    k.apu_log = []
    quiet = 0
    for f in range(max_frames):
        k.frame()
        if song:
            if f > 10 and k.rd(syms["_ses_playing"]) == 0:
                break
        else:
            on = any(k.rd(syms["_ses_on"] + 5 + c) for c in range(5))
            quiet = 0 if on else quiet + 1
            if quiet > 20:
                break
    n = k.frames - start
    log = [(f - start, r, v) for f, r, v in k.apu_log]
    k.wr(syms["_komut"], 3)
    for _ in range(3):
        k.frame()
    return log, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="+")
    ap.add_argument("--saniye", type=float, default=40)
    ap.add_argument("--v2", action="store_true", help="an APU v2 (wave channel)")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for name in a.names:
        k, syms = player(name, a.v2)
        songs = items(k, syms, "_ses_muzikler")
        sfx = items(k, syms, "_ses_efektler")
        lens = song_frames(name)
        for i, nm in enumerate(names(k, syms, "_mus_", songs)):
            limit = min(int(a.saniye * 60), lens.get(nm, 10 ** 6) + 90)
            log, n = record(k, syms, 1, i, limit, True)
            path = os.path.join(OUT, f"{name}_{nm}{'_v2' if a.v2 else ''}.wav")
            write_wav(path, render_log(log, n, 2 if a.v2 else 1))
            print(f"{path}  ({n / 60:.1f} s)")
        for i, nm in enumerate(names(k, syms, "_sfx_", sfx)):
            log, n = record(k, syms, 2, i, 600, False)
            path = os.path.join(OUT, f"{name}_efekt_{nm}{'_v2' if a.v2 else ''}.wav")
            write_wav(path, render_log(log, n + 10, 2 if a.v2 else 1))
            print(f"{path}  ({n / 60:.1f} s)")


if __name__ == "__main__":
    main()
