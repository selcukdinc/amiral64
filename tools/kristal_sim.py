#!/usr/bin/env python3
"""Kristal Ocak: a model of the game's rules (sw/games/kristal/main.c) and a route planner for the levels.

The model follows the C code step by step: the miner's move (or B + direction), the fuses, then the physics
scan (bottom-up, left to right, a thing that moved right is skipped, carts move every other step). It is
used to design the levels and to turn each level's route into the moves the tests play on the emulator.

    python3 tools/kristal_sim.py sw/games/kristal/levels/1-1.txt     # plan the route, print the result
    python3 tools/kristal_sim.py --write sw/games/kristal/levels/*.txt   # and store the moves in the files

A level file: "key: value" lines (name, need, time, intro, route; solution is written by --write), then the
13 rows of 32 cells. Cells: W steel wall, . dirt, space empty, O rock, * crystal, + big crystal (5),
E exit, P miner, C cracked wall, D dynamite, G firedamp, R rail, A cart (going right), Y oil.

Route words: "g x,y" walk there (shortest way through dirt, taking what is on it), "m RRDD" these moves,
"b R" B + direction (dig/take without moving; on an empty cell: place dynamite), "w n" wait n steps,
"x" walk to the exit (waiting until it is open), "auto" (or "auto n") walk to the nearest crystal it can
reach safely until there are enough (or n more). A planned move must leave the miner alive for 6 more
steps of waiting, or the planner tries another way.
"""
import sys, os, re
from collections import deque

W, H = 32, 13
EMPTY, DIRT, WALL, ROCK, GEM, GEM2, EXIT, HERO, CRACK, DYN, LIT, GAS, BOOM, RAIL, CART, OIL = range(16)
FALLING, WOBBLE, CART_R = 0x40, 0x20, 0x10
CH = {"W": WALL, ".": DIRT, " ": EMPTY, "O": ROCK, "*": GEM, "+": GEM2, "E": EXIT, "P": HERO, "C": CRACK,
      "D": DYN, "G": GAS, "R": RAIL, "A": CART | CART_R, "Y": OIL}
SHOW = {EMPTY: " ", DIRT: ".", WALL: "W", ROCK: "O", GEM: "*", GEM2: "+", EXIT: "E", HERO: "P", CRACK: "C",
        DYN: "D", LIT: "!", GAS: "G", BOOM: "#", RAIL: "=", CART: "A", OIL: "Y"}
DIRS = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}
NFUSE = 6
EXIT_STEPS = 6          # steps from the door starting to open until it can be entered (3 stages, 8 frames each)


def kind(c):
    return c & 0x0F


def free(c):
    return c == EMPTY or c == RAIL


def parse_level(path):
    keys, rows = {}, []
    for line in open(path).read().split("\n"):
        m = re.match(r"^([a-z]+): ?(.*)$", line)
        if m and not rows:
            keys[m.group(1)] = m.group(2)
        elif line.startswith("W") or rows:
            if line:
                rows.append(line)
    assert len(rows) == H and all(len(r) == W for r in rows), f"{path}: the map must be {W}x{H}"
    return keys, rows


class Game:
    def __init__(self, rows, need, dark=False):
        self.map = [[0] * W for _ in range(H)]
        self.rail = [[EMPTY] * W for _ in range(H)]
        self.need = need
        self.gems = self.gems_total = self.dyns = 0
        self.alive, self.won, self.exit_open, self.open_step = True, False, False, None
        self.fuse = []          # [x, y, t, gas] in slots
        self.fuse = [[0, 0, 0, 0] for _ in range(NFUSE)]
        self.cart_step = 0
        self.steps = 0
        for y in range(H):
            for x in range(W):
                c = rows[y][x]
                self.rail[y][x] = RAIL if c in "RA" else EMPTY
                v = CH[c]
                self.map[y][x] = v
                if c == "P":
                    self.hx, self.hy = x, y
                if c == "E":
                    self.ex, self.ey = x, y
                if c == "*":
                    self.gems_total += 1
                if c == "+":
                    self.gems_total += 5

    def copy(self):
        g = Game.__new__(Game)
        g.__dict__ = {k: (v if not isinstance(v, list) else [list(r) if isinstance(r, list) else r for r in v])
                      for k, v in self.__dict__.items()}
        return g

    def vacate(self, x, y):
        self.map[y][x] = self.rail[y][x]

    # ---- the miner
    def collect(self, t):
        self.gems += 5 if t == GEM2 else 1
        if self.gems >= self.need and not self.exit_open:
            self.exit_open, self.open_step = True, self.steps

    def new_fuse(self, x, y, t, gas):
        for f in self.fuse:
            if not f[2]:
                f[:] = [x, y, t, gas]
                return True
        return False

    def light(self, x, y, t):
        if self.new_fuse(x, y, t, 0):
            self.map[y][x] = LIT

    def snap_at(self, dx, dy):
        nx, ny = self.hx + dx, self.hy + dy
        t = kind(self.map[ny][nx])
        if free(self.map[ny][nx]) and self.dyns:
            self.dyns -= 1
            self.light(nx, ny, 4)
            return
        if t == DIRT:
            pass
        elif t == GAS:
            self.new_fuse(nx, ny, 5, 1)
        elif t in (GEM, GEM2):
            self.collect(t)
        elif t == DYN:
            self.dyns += 1
        elif t == OIL:
            pass
        else:
            return
        self.vacate(nx, ny)

    def move_hero(self, dx, dy):
        nx, ny = self.hx + dx, self.hy + dy
        t = kind(self.map[ny][nx])
        if t not in (EMPTY, DIRT, ROCK, GEM, GEM2, EXIT, DYN, GAS, RAIL, OIL):
            return
        if t == ROCK:
            if dy or not free(self.map[ny][nx + dx]):
                return
            self.map[ny][nx + dx] = ROCK
        elif t in (GEM, GEM2):
            self.collect(t)
        elif t == EXIT:
            if not self.exit_open or self.steps - self.open_step < EXIT_STEPS:
                return
            self.won = True
        elif t == DYN:
            self.dyns += 1
        elif t == GAS:
            self.new_fuse(nx, ny, 5, 1)
        self.vacate(self.hx, self.hy)
        self.hx, self.hy = nx, ny
        self.map[ny][nx] = HERO

    # ---- dynamite and firedamp
    def explode(self, x, y):
        for cy in (y - 1, y, y + 1):
            for cx in (x - 1, x, x + 1):
                k = kind(self.map[cy][cx])
                if k in (WALL, EXIT, GEM, GEM2):
                    continue
                if k == HERO:
                    self.alive = False
                    continue
                if k == DYN:
                    self.light(cx, cy, 1)
                    continue
                if k == GAS:
                    self.new_fuse(cx, cy, 1, 1)
                if k == LIT and (cx != x or cy != y):
                    for f in self.fuse:
                        if f[2] and f[0] == cx and f[1] == cy:
                            f[2] = 1
                    continue
                self.map[cy][cx] = BOOM

    def fuses(self):
        for f in self.fuse:
            if f[2]:
                f[2] -= 1
                if not f[2]:
                    self.explode(f[0], f[1])

    # ---- rocks, crystals, carts
    def cart(self, x, y, c):
        m = self.map
        d = 1 if c & CART_R else -1
        nx = x + d
        n = m[y][nx]
        nk = n & 0x0F
        if kind(m[y - 1][x]) == ROCK:
            return 0
        if nk == ROCK and not (n & FALLING) and self.rail[y][nx] and free(m[y][nx + d]):
            m[y][nx + d] = ROCK
        elif nk == HERO:
            self.alive = False
        elif n != RAIL:
            m[y][x] = c ^ CART_R
            return 0
        m[y][nx] = c
        self.vacate(x, y)
        return 1 if d > 0 else 0

    def physics(self):
        m = self.map
        self.cart_step += 1
        for y in range(H - 2, 0, -1):
            x = 1
            while x < W - 1:
                c = m[y][x]
                k = c & 0x0F
                if k in (BOOM, CRACK):
                    if c & WOBBLE:
                        self.vacate(x, y)
                    elif k == BOOM:
                        m[y][x] = BOOM | WOBBLE
                    x += 1
                    continue
                if k == CART:
                    if self.cart_step & 1:
                        x += self.cart(x, y, c)
                    x += 1
                    continue
                if k not in (ROCK, GEM, GEM2):
                    x += 1
                    continue
                b = m[y + 1][x] & 0x0F
                round_b = b in (ROCK, GEM, GEM2)
                if b in (EMPTY, RAIL):
                    if c & (FALLING | WOBBLE):
                        m[y + 1][x] = k | FALLING
                        self.vacate(x, y)
                    else:
                        m[y][x] = k | WOBBLE
                elif b == HERO and (c & FALLING):
                    self.alive = False
                elif b == CRACK and k == ROCK and (c & FALLING):
                    m[y + 1][x] = CRACK | WOBBLE
                    m[y][x] = ROCK
                elif round_b and free(m[y][x - 1]) and free(m[y + 1][x - 1]):
                    m[y][x - 1] = k | FALLING
                    self.vacate(x, y)
                elif round_b and free(m[y][x + 1]) and free(m[y + 1][x + 1]):
                    m[y][x + 1] = k | FALLING
                    self.vacate(x, y)
                    x += 1
                elif c & (FALLING | WOBBLE):
                    m[y][x] = k
                x += 1

    def step(self, move):
        """one game step; move: "" (wait), "U".."R", or "bU".."bR" (B + direction)"""
        self.steps += 1
        if move.startswith("b"):
            self.snap_at(*DIRS[move[1]])
        elif move:
            self.move_hero(*DIRS[move])
        self.fuses()
        if self.won or not self.alive:
            return
        self.physics()

    def show(self):
        return "\n".join("".join(SHOW[kind(c)] for c in r) for r in self.map)


# ---------------------------------------------------------------- the planner
PASS = (EMPTY, DIRT, GEM, GEM2, DYN, RAIL, OIL)


def safe_after(g, move, look=6):
    t = g.copy()
    t.step(move)
    if not t.alive:
        return None
    if t.won:
        return t
    u = t.copy()
    for _ in range(look):
        u.step("")
        if not u.alive:
            return None
    return t


def path_to(g, tx, ty, allow_exit=False):
    """moves of a shortest way through passable cells (rocks are walls) from the miner to (tx, ty)"""
    start = (g.hx, g.hy)
    prev = {start: None}
    q = deque([start])
    while q:
        p = q.popleft()
        if p == (tx, ty):
            break
        for d, (dx, dy) in DIRS.items():
            n = (p[0] + dx, p[1] + dy)
            if n in prev:
                continue
            k = kind(g.map[n[1]][n[0]])
            ok = k in PASS or (n == (tx, ty) and (k in PASS or k == GAS or (allow_exit and k == EXIT)))
            if not ok:
                continue
            prev[n] = (p, d)
            q.append(n)
    if (tx, ty) not in prev:
        return None
    out, p = [], (tx, ty)
    while prev[p]:
        p, d = prev[p]
        out.append(d)
    return out[::-1]


def walk_to(g, tx, ty, moves, allow_exit=False, limit=300):
    """step by step along a shortest way; a deadly step is replaced by another safe move or a wait"""
    waits = 0
    for _ in range(limit):
        if (g.hx, g.hy) == (tx, ty) or g.won:
            return g
        p = path_to(g, tx, ty, allow_exit)
        if not p:
            raise RuntimeError(f"no way from ({g.hx},{g.hy}) to ({tx},{ty})\n{g.show()}")
        t = safe_after(g, p[0])
        if t is not None and ((t.hx, t.hy) != (g.hx, g.hy) or t.won):
            moves.append(p[0])
            g, waits = t, 0
            continue
        # the way's first step is deadly (or blocked for now): wait, or after a while any other safe move
        options = [""] if waits < 6 else []
        options += [d for d in DIRS if d != p[0]] + ([""] if waits >= 6 else [])
        for m in options:
            t = safe_after(g, m)
            if t is not None and (m == "" or (t.hx, t.hy) != (g.hx, g.hy)):
                moves.append(m)
                waits = waits + 1 if m == "" else 0
                g = t
                break
        else:
            raise RuntimeError(f"stuck at ({g.hx},{g.hy}) on the way to ({tx},{ty}): every move is deadly\n{g.show()}")
    raise RuntimeError(f"did not reach ({tx},{ty}) (at {g.hx},{g.hy})\n{g.show()}")


def plan(rows, need, route):
    g = Game(rows, need)
    moves = []
    for word in [w.strip() for w in route.split(";") if w.strip()]:
        op, _, arg = word.partition(" ")
        if op == "g":
            x, y = map(int, arg.split(","))
            g = walk_to(g, x, y, moves)
        elif op == "m":
            for d in arg.replace(" ", ""):
                g.step(d)
                moves.append(d)
                if not g.alive:
                    raise RuntimeError(f"died on the move {d} of '{word}'\n{g.show()}")
        elif op == "b":
            g.step("b" + arg.strip())
            moves.append("b" + arg.strip())
        elif op == "w":
            for _ in range(int(arg)):
                g.step("")
                moves.append("")
                if not g.alive:
                    raise RuntimeError(f"died waiting ('{word}')\n{g.show()}")
        elif op == "auto":
            goal = g.gems + int(arg) if arg else g.need
            while g.gems < goal:
                cands = []
                for y in range(H):
                    for x in range(W):
                        if kind(g.map[y][x]) in (GEM, GEM2):
                            p = path_to(g, x, y)
                            if p:
                                cands.append((len(p), x, y))
                for _, x, y in sorted(cands):
                    try:
                        trial = []
                        g2 = walk_to(g.copy(), x, y, trial)
                        g, moves = g2, moves + trial
                        break
                    except RuntimeError:
                        continue
                else:
                    raise RuntimeError(f"auto: no crystal reachable ({g.gems}/{goal})\n{g.show()}")
        elif op == "x":
            if not g.exit_open:
                raise RuntimeError(f"the door is shut: {g.gems}/{g.need} crystals\n{g.show()}")
            g = walk_to(g, g.ex, g.ey, moves, allow_exit=True)
        else:
            raise ValueError(word)
        if not g.alive:
            raise RuntimeError(f"died at '{word}'\n{g.show()}")
    return g, moves


def encode(moves):
    """moves as text: U D L R, u d l r for B + direction, . for a wait"""
    return "".join("." if not m else m[1].lower() if m.startswith("b") else m for m in moves)


def decode(s):
    return ["" if c == "." else ("b" + c.upper() if c.islower() else c) for c in s]


def main():
    args = sys.argv[1:]
    write = "--write" in args
    files = [a for a in args if a != "--write"]
    bad = 0
    for f in files:
        keys, rows = parse_level(f)
        need = int(keys["need"])
        try:
            g, moves = plan(rows, need, keys.get("route", ""))
        except RuntimeError as e:
            print(f"{f}: {e}")
            bad += 1
            continue
        ok = g.won
        print(f"{os.path.basename(f)}: {'SOLVED' if ok else 'NOT SOLVED'} in {len(moves)} steps "
              f"({len(moves) * 6 / 60:.0f} s of {keys['time']}), crystals {g.gems}/{need} of {g.gems_total}")
        if not ok:
            print(g.show())
            bad += 1
        if write and ok:
            text = open(f).read()
            sol = "solution: " + encode(moves)
            text = re.sub(r"^solution: .*$", sol, text, flags=re.M) if "solution:" in text else \
                text.replace("\nW", "\n" + sol + "\nW", 1)
            open(f, "w").write(text)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
