#!/usr/bin/env python3
"""Builds the konsol cartridge image (the games the boot ROM's menu offers).

    mkcart.py out.bin out.mem "NAME:players:game.bin[:icon[:load]]" ...      (players like 1 or 2-4)

A name starting with '*' marks the launcher (flags bit0): the boot ROM starts it instead of its own menu.
icon: a number the launcher uses for the game's picture (0 = generic).
load: load address in hex (default 9000: up to 20 KB; 0400: up to 31 KB, see sw/game_c_low.cfg).

Layout: 16-byte header ("K65C", game count, version 1), 8 directory entries of 32 bytes
(name 20 bytes, offset 3, size 2, load address 2, min players, max players, flags, icon, 0), games from $0200.
A game's extra data (<game.bin>.ek, see sw/game_c_ek.cfg) follows the game directly; the size in the
directory counts only the game, so after loading it the cartridge port points at the extra data.
The image is padded with $FF to 256 KB (the BRAM cartridge, 64 x 4 KB banks written as
<out>_00.mem ...; the same bytes go to the QSPI flash).
"""
import sys, os

SIZE = 256 * 1024         # BRAM cartridge: 64 banks of 4 KB
GAME_MAX = {0x9000: 0x5000, 0x0400: 0x7C00}

def build(games):
    img = bytearray(b"\xff" * SIZE)
    img[0:16] = b"K65C" + bytes([len(games), 1]) + bytes(10)
    off = 0x200
    for i, (name, pmin, pmax, data, flags, icon, load, ek) in enumerate(games):
        if load not in GAME_MAX:
            sys.exit(f"mkcart: {name}: load address {load:04X} is not 9000 or 0400")
        if len(data) > GAME_MAX[load]:
            sys.exit(f"mkcart: {name} is {len(data)} bytes, more than {GAME_MAX[load]}")
        if off + len(data) + len(ek) > SIZE:
            sys.exit("mkcart: the cartridge is full")
        e = 16 + 32 * i
        img[e:e + 32] = (name.upper().encode("ascii")[:20].ljust(20, b"\0") + off.to_bytes(3, "little")
                         + len(data).to_bytes(2, "little") + load.to_bytes(2, "little") + bytes([pmin, pmax, flags, icon, 0]))
        img[off:off + len(data)] = data
        img[off + len(data):off + len(data) + len(ek)] = ek
        off = (off + len(data) + len(ek) + 255) & ~255
    return img

def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    games = []
    for spec in sys.argv[3:]:
        name, players, path, *rest = spec.split(":")
        pmin, _, pmax = players.partition("-")
        flags = 1 if name.startswith("*") else 0
        icon = int(rest[0]) if rest else 0
        load = int(rest[1], 16) if len(rest) > 1 else 0x9000
        ek = open(path + ".ek", "rb").read() if os.path.exists(path + ".ek") else b""
        games.append((name.lstrip("*"), int(pmin), int(pmax or pmin), open(path, "rb").read(), flags, icon, load, ek))
    if len(games) > 8:
        sys.exit("mkcart: at most 8 games")
    img = build(games)
    open(sys.argv[1], "wb").write(img)
    with open(sys.argv[2], "w") as f:
        f.write("".join(f"{b:02x}\n" for b in img))
    base = sys.argv[2][:-len(".mem")]
    for n in range(SIZE // 4096):
        with open(f"{base}_{n:02x}.mem", "w") as f:
            f.write("".join(f"{b:02x}\n" for b in img[n * 4096:(n + 1) * 4096]))

if __name__ == "__main__":
    main()
