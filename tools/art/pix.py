"""Tiny pixel-art drawing helpers for the art generators (canvas of palette indices -> art.txt blocks)."""
import math

class Canvas:
    def __init__(self, w, h, fill=0):
        self.w, self.h = w, h
        self.p = [[fill] * w for _ in range(h)]

    def set(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def get(self, x, y):
        return self.p[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def rect(self, x0, y0, x1, y1, c):           # inclusive corners
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def frame(self, x0, y0, x1, y1, c):
        for x in range(x0, x1 + 1):
            self.set(x, y0, c); self.set(x, y1, c)
        for y in range(y0, y1 + 1):
            self.set(x0, y, c); self.set(x1, y, c)

    def disc(self, cx, cy, r, c):
        for y in range(self.h):
            for x in range(self.w):
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                    self.set(x, y, c)

    def ring(self, cx, cy, r0, r1, c):
        for y in range(self.h):
            for x in range(self.w):
                d = (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2
                if r0 * r0 <= d <= r1 * r1:
                    self.set(x, y, c)

    def poly(self, pts, c):
        """filled polygon (even-odd, pixel centres)"""
        for y in range(self.h):
            yc = y + 0.5
            xs = []
            for i in range(len(pts)):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
                if (y0 <= yc < y1) or (y1 <= yc < y0):
                    xs.append(x0 + (yc - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[::2], xs[1::2]):
                for x in range(self.w):
                    if a <= x + 0.5 <= b:
                        self.set(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / max(n, 1)
            self.set(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), c)

    def outline(self, c, inside=None):
        """colour c around every non-zero pixel (on zero pixels)"""
        src = [row[:] for row in self.p]
        for y in range(self.h):
            for x in range(self.w):
                if src[y][x] == 0 and any(0 <= x + dx < self.w and 0 <= y + dy < self.h and src[y + dy][x + dx] != 0
                                          for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    self.p[y][x] = c

    def text_rows(self, x0, y0, w, h):
        return ["".join("." if v == 0 else "%x" % v for v in self.p[y][x0:x0 + w]) for y in range(y0, y0 + h)]

def block(kind, name, rows):
    return f"{kind} {name}\n" + "\n".join(rows) + "\n"

def bigs(cv, name, cols, rows_):
    """cut a canvas into 16x16 'big' blocks name_0, name_1 ... (row-major)"""
    out, n = [], 0
    for by in range(rows_):
        for bx in range(cols):
            out.append(block("big", f"{name}_{n}", cv.text_rows(bx * 16, by * 16, 16, 16)))
            n += 1
    return "\n".join(out)

def tiles(cv, name, cols, rows_):
    out, n = [], 0
    for by in range(rows_):
        for bx in range(cols):
            out.append(block("tile", f"{name}_{n}", cv.text_rows(bx * 8, by * 8, 8, 8)))
            n += 1
    return "\n".join(out)

def pal(name, cols):
    return f"palette {name} " + " ".join(cols) + "\n"
