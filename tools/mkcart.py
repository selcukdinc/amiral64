#!/usr/bin/env python3
"""Builds the konsol cartridge image (the games the boot ROM's menu offers).

    mkcart.py out.bin out.mem "NAME:players:game.bin" ...      (players like 1 or 2-4)

Layout: 16-byte header ("K65C", game count, version 1), 8 directory entries of 32 bytes
(name 20 bytes, offset 3, size 2, load address 2, min players, max players), games from $0200.
The image is padded with $FF to 64 KB (the BRAM cartridge, 16 x 4 KB banks written as
<out>_00.mem ...; the same bytes go to the QSPI flash).
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../Scripts"))

SIZE = 64 * 1024          # BRAM cartridge: 16 banks of 4 KB
GAME_MAX = 0x5000
LOAD = 0x9000

def build(games):
    img = bytearray(b"\xff" * SIZE)
    img[0:16] = b"K65C" + bytes([len(games), 1]) + bytes(10)
    off = 0x200
    for i, (name, pmin, pmax, data) in enumerate(games):
        if len(data) > GAME_MAX:
            sys.exit(f"mkcart: {name} is {len(data)} bytes, more than {GAME_MAX}")
        if off + len(data) > SIZE:
            sys.exit("mkcart: the cartridge is full")
        e = 16 + 32 * i
        img[e:e + 32] = (name.upper().encode("ascii")[:20].ljust(20, b"\0") + off.to_bytes(3, "little")
                         + len(data).to_bytes(2, "little") + LOAD.to_bytes(2, "little") + bytes([pmin, pmax]) + bytes(3))
        img[off:off + len(data)] = data
        off = (off + len(data) + 255) & ~255
    return img

def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    games = []
    for spec in sys.argv[3:]:
        name, players, path = spec.split(":", 2)
        pmin, _, pmax = players.partition("-")
        games.append((name, int(pmin), int(pmax or pmin), open(path, "rb").read()))
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
