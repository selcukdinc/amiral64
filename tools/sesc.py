#!/usr/bin/env python3
"""sesc: compiles the konsol's music and sound effects (MML text) for the sound engine (sw/lib/ses.s).

    sesc.py music.ses out.s out.h [list] [--segment NAME]

Output: ca65 source with the data (exported _mus_NAME, _sfx_NAME, and the lists _<list>_muzikler,
_<list>_efektler for the sound test; list defaults to "ses") and a C header declaring them. The data goes
to RODATA, or to the segment NAME (EKDATA: the cartridge's extra data, see sw/game_c_ek.cfg).

File format (one statement per line, # starts a comment):

  inst NAME  v= 15 13 | 11 10 / 6 3 0   a= 0 12 0   p= 0 -3   d= 2 1 |   w= WAVE
        sequences, one value a frame: v volume 0..15, a arpeggio (semitones), p pitch (period change a
        frame, added up), d duty 0-3 (+4: noise short mode), w a wave of the wave channel.
        '|' marks the loop point, '/' the release part (where a rest jumps to), 'x*n' repeats a value
        n times, 'a..b' counts from a to b. The first instrument is the default one.
  wave NAME  32 values 0..15 (one cycle of the wave channel)
  macro NAME  MML               a phrase to call from songs (*NAME); it starts at o4 l8, its own state
  song NAME  bpm=120 [grid=16] [filtre=0..3]   (or tempo=raw: rows a frame x 256; filtre: the APU v2
                                output low-pass, 1 = 15.5 kHz, 2 = 7.8 kHz, 3 = 3.9 kHz)
    sq1: MML                    channels sq1 sq2 tri noi wav; a channel can go on over many lines
  end
  sfx NAME  [pri=N]             a sound effect: like a song, but note lengths are in frames
    sq1: MML
  end
  include FILE

MML: c d e f g a b (+ or # sharp, - flat) [length][.]   r rest   ^ hold (tie)   n0..n15[,length] noise
  o4 octave   > <   l8 default length   @NAME instrument   v12 channel volume   ~depth,speed vibrato
  ~0 off   p4 portamento   k-2 detune   T130 bpm   K+2 transpose   [ ... ]3 repeat   *NAME call
  L loop point (the channel goes back here at its end)   | (bar lines are ignored)
Lengths are fractions of a whole note (grid rows a whole note; default 16 rows = 16ths), or %n rows.
Notes: C2 is the lowest (o2 c).
"""
import sys, os, re

CHANNELS = ["sq1", "sq2", "tri", "noi", "wav"]
SEMI = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
EV_REST, EV_HOLD, EV_INST, EV_VOL, EV_VIB, EV_PORT, EV_DET, EV_TEMPO = range(0x60, 0x68)
EV_CALL, EV_RET, EV_JUMP, EV_LSTART, EV_LEND, EV_END = range(0x68, 0x6E)


class Err(Exception):
    pass


def tempo_of(bpm, grid):
    t = round(bpm / 60 * grid / 4 * 256 / 60)
    if not 1 <= t <= 255:
        raise Err(f"tempo out of range (bpm {bpm}, grid {grid})")
    return t


def parse_values(toks):
    """sequence tokens -> (values, loop, release)"""
    vals, loop, rel = [], 255, 255
    for t in toks:
        if t == "|":
            loop = len(vals)
        elif t == "/":
            rel = len(vals)
        elif "*" in t:
            v, n = t.split("*")
            vals += [int(v)] * int(n)
        elif ".." in t[1:]:
            a, b = t.split("..")
            a, b = int(a), int(b)
            vals += list(range(a, b + (1 if b >= a else -1), 1 if b >= a else -1))
        else:
            vals.append(int(t))
    if not vals:
        raise Err("empty sequence")
    if rel == 0:
        raise Err("the release part cannot start at the first value")
    if loop != 255 and loop >= len(vals):
        raise Err("loop point after the last value")
    if rel != 255 and rel >= len(vals):
        raise Err("release point after the last value")
    if len(vals) > 250:
        raise Err("sequence too long")
    return vals, loop, rel


class Compiler:
    def __init__(self):
        self.insts = {}           # name -> (definition order, {key: (vals, loop, rel)})
        self.used = []            # instruments the streams use, in table order (the first defined one first)
        self.waves = {}           # name -> (index, 32 values)
        self.macros = {}          # name -> MML text
        self.songs = []           # (name, tempo, grid, {ch: text})
        self.sfx = []             # (name, pri, {ch: text})
        self.blobs = []           # (label, items) items: int byte or ("w", label)
        self.macro_done = {}      # (name, grid, ch kind) -> label
        self.nlab = 0

    # ------------------------------------------------------------ reading
    def read(self, path):
        lines = open(path).read().split("\n")
        i = 0
        cur = None
        while i < len(lines):
            raw = lines[i]
            ln = i + 1
            i += 1
            line = raw.split("#")[0].strip()
            if not line:
                continue
            try:
                words = line.split()
                kw = words[0]
                if cur is not None:
                    if kw == "end":
                        cur = None
                        continue
                    m = re.match(r"(\w+)\s*:(.*)$", line)
                    if not m or m.group(1) not in CHANNELS or m.group(1) == "_flags":
                        raise Err(f"expected 'channel: MML' or 'end', got '{line}'")
                    cur.setdefault(m.group(1), []).append((m.group(2), ln))
                    continue
                if kw == "include":
                    self.read(os.path.join(os.path.dirname(path), words[1]))
                elif kw == "inst":
                    self.inst(words[1], line.split(None, 2)[2] if len(words) > 2 else "")
                elif kw == "wave":
                    vals = [int(v) for v in words[2:]]
                    if len(vals) != 32 or not all(0 <= v <= 15 for v in vals):
                        raise Err("a wave is 32 values 0..15")
                    self.waves[words[1]] = (len(self.waves), vals)
                elif kw == "macro":
                    self.macros[words[1]] = (line.split(None, 2)[2] if len(words) > 2 else "", ln)
                elif kw == "song":
                    opts = dict(w.split("=") for w in words[2:])
                    grid = int(opts.get("grid", 16))
                    tempo = int(opts["tempo"]) if "tempo" in opts else tempo_of(float(opts.get("bpm", 120)), grid)
                    cur = {"_flags": int(opts.get("filtre", 0)) & 3}
                    self.songs.append((words[1], tempo, grid, cur))
                elif kw == "sfx":
                    opts = dict(w.split("=") for w in words[2:])
                    cur = {}
                    self.sfx.append((words[1], int(opts.get("pri", 5)), cur))
                else:
                    raise Err(f"unknown statement '{kw}'")
            except Err as e:
                raise Err(f"{path}:{ln}: {e}")
            except (ValueError, KeyError, IndexError) as e:
                raise Err(f"{path}:{ln}: bad statement ({e})")
        if cur is not None:
            raise Err(f"{path}: missing 'end'")

    def inst(self, name, text):
        seqs = {}
        key, toks = None, []
        for t in text.split():
            m = re.match(r"([vapdw])=(.*)$", t)
            if m:
                if key:
                    seqs[key] = toks
                key, toks = m.group(1), ([m.group(2)] if m.group(2) else [])
            else:
                toks.append(t)
        if key:
            seqs[key] = toks
        out = {}
        for k, toks in seqs.items():
            if k == "w":
                out["w"] = toks[0]
            else:
                out[k] = parse_values(toks)
        self.insts[name] = (len(self.insts), out)

    # ------------------------------------------------------------ MML
    def use_inst(self, name):
        """index of an instrument in the output table (only the instruments used are written)"""
        if not self.used:
            self.used.append(min(self.insts, key=lambda n: self.insts[n][0]))   # the default, index 0
        if name not in self.used:
            self.used.append(name)
        if len(self.used) > 127:
            raise Err("more than 127 instruments")
        return self.used.index(name)

    def label(self, base):
        self.nlab += 1
        return f"L{self.nlab}_{base}"

    def compile_mml(self, text, ch, grid, frames, top=True, ctx="", state=None):
        """MML -> items. state: octave, length, transpose (a macro starts from its own).
        top: may use loops/calls (a macro body is flattened: no calls, loops repeated)"""
        st = state or {"o": 4, "l": 1 if frames else grid // 8, "K": 0}
        st.setdefault("dur", None)
        items = []
        loop_stack = []
        lpoint = None
        s = text
        i = 0

        def num(signed=False):
            nonlocal i
            m = re.match(r"[+-]?\d+" if signed else r"\d+", s[i:])
            if not m:
                raise Err(f"{ctx}: number expected at '{s[i:i+10]}'")
            i += len(m.group(0))
            return int(m.group(0))

        def length():
            nonlocal i
            if i < len(s) and s[i] == "%":
                i += 1
                return num()
            m = re.match(r"\d+", s[i:])
            if m:
                i += len(m.group(0))
                n = int(m.group(0))
                if frames:
                    rows = n
                else:
                    if grid % n:
                        raise Err(f"{ctx}: length {n} does not fit the grid {grid}")
                    rows = grid // n
            else:
                rows = st["l"]
            base = rows
            while i < len(s) and s[i] == ".":
                i += 1
                base //= 2
                rows += base
            if rows <= 0:
                raise Err(f"{ctx}: zero length")
            return rows

        def row(ev, rows):
            first = True
            while rows > 0:
                d = min(rows, 127)
                if st["dur"] != d:
                    items.append(0x80 | d)
                    st["dur"] = d
                items.append(ev if first else EV_HOLD)
                first = False
                rows -= d

        while i < len(s):
            c = s[i]
            if c in " \t|":
                i += 1
            elif c in SEMI:
                if ch == "noi":
                    raise Err(f"{ctx}: the noise channel plays n0..n15")
                i += 1
                n = SEMI[c]
                while i < len(s) and s[i] in "+#-":
                    n += -1 if s[i] == "-" else 1
                    i += 1
                note = (st["o"] - 2) * 12 + n + st["K"]
                if not 0 <= note <= 95:
                    raise Err(f"{ctx}: note out of range (o{st['o']} {c})")
                if ch in ("tri", "wav") and note > 71:
                    raise Err(f"{ctx}: too high for the {ch} channel (B7 at most)")
                row(note, length())
            elif c == "n":
                i += 1
                v = num()
                if not 0 <= v <= 15:
                    raise Err(f"{ctx}: noise n0..n15")
                rows = st["l"]
                if i < len(s) and s[i] == ",":
                    i += 1
                    rows = length()
                row(v, rows)
            elif c == "r":
                i += 1
                row(EV_REST, length())
            elif c == "^":
                i += 1
                row(EV_HOLD, length())
            elif c == "o":
                i += 1
                st["o"] = num()
            elif c == ">":
                i += 1
                st["o"] += 1
            elif c == "<":
                i += 1
                st["o"] -= 1
            elif c == "l":
                i += 1
                st["l"] = length()
            elif c == "@":
                m = re.match(r"@(\w+)", s[i:])
                i += len(m.group(0))
                if m.group(1) not in self.insts:
                    raise Err(f"{ctx}: no instrument '{m.group(1)}'")
                items += [EV_INST, self.use_inst(m.group(1))]
            elif c == "v":
                i += 1
                v = num()
                if not 0 <= v <= 15:
                    raise Err(f"{ctx}: volume 0..15")
                items += [EV_VOL, v]
            elif c == "~":
                i += 1
                d = num()
                sp = 0
                if i < len(s) and s[i] == ",":
                    i += 1
                    sp = num()
                if d > 15 or sp > 15:
                    raise Err(f"{ctx}: vibrato depth and speed 0..15")
                items += [EV_VIB, d << 4 | sp]
            elif c == "p":
                i += 1
                items += [EV_PORT, num()]
            elif c == "k":
                i += 1
                items += [EV_DET, num(True) & 255]
            elif c == "T":
                i += 1
                items += [EV_TEMPO, tempo_of(num(), grid)]
            elif c == "K":
                i += 1
                st["K"] = num(True)
            elif c == "[":
                i += 1
                if not top:
                    loop_stack.append(("flat", len(items), dict(st)))
                elif any(k == "real" for k, *_ in loop_stack):
                    loop_stack.append(("flat", len(items), dict(st)))
                else:
                    loop_stack.append(("real", len(items), None))
                    items.append(None)               # count, filled in at ']'
                    st["dur"] = None
            elif c == "]":
                i += 1
                if not loop_stack:
                    raise Err(f"{ctx}: ']' without '['")
                n = num() if i < len(s) and s[i].isdigit() else 2
                kind, at, _ = loop_stack.pop()
                if kind == "real":
                    items[at] = EV_LSTART
                    items.insert(at + 1, n)
                    items.append(EV_LEND)
                    st["dur"] = None
                else:
                    body = items[at:]
                    for _ in range(n - 1):
                        items += body
                    st["dur"] = None
            elif c == "*":
                m = re.match(r"\*(\w+)", s[i:])
                i += len(m.group(0))
                name = m.group(1)
                if name not in self.macros:
                    raise Err(f"{ctx}: no macro '{name}'")
                body, ln = self.macros[name]
                if top:
                    lab = self.macro_label(name, ch, grid, frames)
                    items += [EV_CALL, ("w", lab)]
                else:
                    # inside another macro: inline it
                    sub = self.compile_mml(body, ch, grid, frames, top=False, ctx=f"macro {name}",
                                           state={"o": 4, "l": 1 if frames else grid // 8, "K": 0})
                    items += sub
                st["dur"] = None
            elif c == "L":
                i += 1
                if not top:
                    raise Err(f"{ctx}: L only in songs")
                lpoint = len(items)
                st["dur"] = None
            else:
                raise Err(f"{ctx}: unexpected '{c}' at '{s[i:i+12]}'")
        if loop_stack:
            raise Err(f"{ctx}: '[' without ']'")
        if top:
            self.check_rows(items, ctx)
        return items if not top else (items, lpoint)

    def check_rows(self, items, ctx):
        rows_ev = [x for x in items if isinstance(x, int) and (x < 0x60 or x in (EV_REST, EV_HOLD))]
        calls = [x for x in items if x == EV_CALL]
        if not rows_ev and not calls:
            raise Err(f"{ctx}: the channel never plays a row")

    def macro_label(self, name, ch, grid, frames):
        key = (name, grid, frames, ch == "noi")
        if key not in self.macro_done:
            body, ln = self.macros[name]
            lab = self.label("m_" + name)
            self.macro_done[key] = lab
            items = self.compile_mml(body, ch, grid, frames, top=False, ctx=f"macro {name} (line {ln})",
                                     state={"o": 4, "l": 1 if frames else grid // 8, "K": 0})
            self.check_rows(items, f"macro {name}")
            self.blobs.append((lab, items + [EV_RET]))
        return self.macro_done[key]

    def stream(self, name, ch, parts, grid, frames, loop_ok):
        text = " ".join(t for t, _ in parts)
        ctx = f"{name} {ch} (line {parts[0][1]})"
        items, lpoint = self.compile_mml(text, ch, grid, frames, ctx=ctx)
        lab = self.label(f"{name}_{ch}")
        if lpoint is not None and loop_ok:
            items.append(EV_JUMP)
            items.append(("w", lab + "_L"))
        else:
            items.append(EV_END)
        self.blobs.append((lab, items, lpoint))
        return lab

    # ------------------------------------------------------------ lengths
    def rows(self, items):
        """rows one pass of a stream plays (loops counted, calls followed)"""
        ops1 = {EV_INST, EV_VOL, EV_VIB, EV_PORT, EV_DET, EV_TEMPO}
        total, dur, i, stack = 0, 4, 0, []
        while i < len(items):
            x = items[i]
            if isinstance(x, tuple):
                i += 1
                continue
            if x >= 0x80:
                dur = x & 0x7F
            elif x < 0x60 or x in (EV_REST, EV_HOLD):
                total += dur
            elif x in ops1:
                i += 1
            elif x == EV_LSTART:
                stack.append((total, items[i + 1]))
                i += 1
            elif x == EV_LEND:
                t0, n = stack.pop()
                total = t0 + (total - t0) * n
            elif x == EV_CALL:
                lab = items[i + 1][1]
                body = next(b[1] for b in self.blobs if b[0] == lab)
                total += self.rows(body)
                i += 1
            i += 1
        return total

    def loop_rows(self, lab):
        """rows from a stream's L point to its end (None: no loop)"""
        blob = next(b for b in self.blobs if b[0] == lab)
        if len(blob) < 3 or blob[2] is None:
            return None
        items = blob[1]
        # rows of the part after the loop point: walk from the start of the stream, count after the label
        return self.rows(items[blob[2]:])

    # ------------------------------------------------------------ output
    def write(self, src, out_s, out_h, lst="ses", segment=None):
        if not self.insts:
            self.insts["_flat"] = (0, {})
        exp = []
        body = []
        # songs (wave table address: a label, filled in below)
        wtab = "ses_wtab"
        self.lengths = {}
        for name, tempo, grid, chans in self.songs:
            labs = []
            for ch in CHANNELS:
                labs.append(self.stream(name, ch, chans[ch], grid, False, True) if ch in chans else "0")
            per = {ch: self.rows(next(b[1] for b in self.blobs if b[0] == lab)) for ch, lab in zip(CHANNELS, labs) if lab != "0"}
            loops = {ch: self.loop_rows(lab) for ch, lab in zip(CHANNELS, labs) if lab != "0"}
            lp = {ch: v for ch, v in loops.items() if v is not None}
            if lp:
                if len(lp) != len(per) or len(set(lp.values())) > 1:
                    raise Err(f"song {name}: the channels loop over different lengths (rows: {loops})")
                period = next(iter(lp.values()))
                starts = {ch: per[ch] - lp[ch] for ch in lp}
                if len({v % period for v in starts.values()}) > 1:
                    raise Err(f"song {name}: the loops start out of step (rows before L: {starts}, loop {period})")
            elif len(set(per.values())) > 1:
                raise Err(f"song {name}: the channels are not the same length (rows: {per})")
            rows = max(per.values())
            self.lengths[name] = (rows * 256 + tempo - 1) // tempo
            exp.append(f"_mus_{name}")
            body.append(f"_mus_{name}: .byte {tempo}, {chans.get('_flags', 0)}")
            body.append(f"        .word ses_itab, {wtab}, " + ", ".join(labs))
        for name, pri, chans in self.sfx:
            parts = [(CHANNELS.index(ch), self.stream(name, ch, chans[ch], 1, True, False)) for ch in CHANNELS if ch in chans]
            exp.append(f"_sfx_{name}")
            body.append(f"_sfx_{name}: .word ses_itab, {wtab}")
            body.append(f"        .byte {len(parts)}")
            for chn, lab in parts:
                body.append(f"        .byte {chn}, {pri}")
                body.append(f"        .word {lab}")
        # instruments
        wused = []
        ibody = body
        body = []
        body.append("ses_itab:")
        if not self.used:
            self.use_inst(min(self.insts, key=lambda n: self.insts[n][0]))
        names = self.used
        body.append("        .word " + ", ".join(f"ses_i_{n}" for n in names))
        for n in names:
            seqs = self.insts[n][1]
            if "w" in seqs:
                if seqs["w"] not in self.waves:
                    raise Err(f"instrument {n}: no wave '{seqs['w']}'")
                if seqs["w"] not in wused:
                    wused.append(seqs["w"])
                seqs["d"] = ([wused.index(seqs["w"])], 255, 255)
            ptrs = [f"ses_i_{n}_{k}" if k in seqs else "0" for k in "vapd"]
            body.append(f"ses_i_{n}: .word " + ", ".join(ptrs))
            for k in "vapd":
                if k in seqs:
                    vals, loop, rel = seqs[k]
                    if k == "v" and not all(0 <= v <= 15 for v in vals):
                        raise Err(f"instrument {n}: volume 0..15")
                    body.append(f"ses_i_{n}_{k}: .byte {len(vals)}, {loop}, {rel}, " + ", ".join(str(v & 255) for v in vals))
        # waves
        if wused:
            body.append("ses_wtab:")
            for n in wused:
                vals = self.waves[n][1]
                packed = [vals[2 * j] << 4 | vals[2 * j + 1] for j in range(16)]
                body.append("        .byte " + ", ".join(str(b) for b in packed) + f"   ; {n}")
        body = body + ibody
        if not wused:
            body.append("ses_wtab = 0")
        # streams
        for blob in self.blobs:
            lab, items = blob[0], blob[1]
            lpoint = blob[2] if len(blob) > 2 else None
            body.append(f"{lab}:")
            line = []

            def flush():
                if line:
                    body.append("        .byte " + ", ".join(line))
                    line.clear()
            for k, it in enumerate(items):
                if lpoint is not None and k == lpoint:
                    flush()
                    body.append(f"{lab}_L:")
                if isinstance(it, tuple):
                    flush()
                    body.append(f"        .word {it[1]}")
                else:
                    line.append(str(it))
                    if len(line) == 16:
                        flush()
            if lpoint is not None and lpoint == len(items):
                flush()
                body.append(f"{lab}_L:")
            flush()
        exp += [f"_{lst}_muzikler", f"_{lst}_efektler"]
        body.append(f"_{lst}_muzikler: .word " + ", ".join([f"_mus_{n}" for n, *_ in self.songs] + ["0"]))
        body.append(f"_{lst}_efektler: .word " + ", ".join([f"_sfx_{n}" for n, *_ in self.sfx] + ["0"]))
        with open(out_s, "w") as f:
            f.write(f"; generated by tools/sesc.py from {src}; edit the .ses file\n")
            f.write("        .export " + ", ".join(exp) + "\n")
            f.write(f'        .segment "{segment}"\n' if segment else "        .rodata\n")
            f.write("\n".join(body) + "\n")
        with open(out_h, "w") as f:
            g = re.sub(r"\W", "_", os.path.basename(out_h)).upper()
            f.write(f"/* generated by tools/sesc.py from {src} */\n#ifndef {g}\n#define {g}\n")
            for name, *_ in self.songs:
                f.write(f"extern const unsigned char mus_{name}[];\n")
                f.write(f"#define MUS_{name.upper()}_KARE {self.lengths[name]}   /* frames of one pass */\n")
            for name, *_ in self.sfx:
                f.write(f"extern const unsigned char sfx_{name}[];\n")
            f.write(f"extern const unsigned char *const {lst}_muzikler[];   /* 0-terminated */\n")
            f.write(f"extern const unsigned char *const {lst}_efektler[];\n#endif\n")


def main():
    args = sys.argv[1:]
    segment = None
    if "--segment" in args:
        i = args.index("--segment")
        segment = args[i + 1]
        del args[i:i + 2]
    if len(args) not in (3, 4):
        sys.exit(__doc__)
    c = Compiler()
    try:
        c.read(args[0])
        c.write(args[0], args[1], args[2], args[3] if len(args) == 4 else "ses", segment)
    except Err as e:
        sys.exit(f"sesc: {e}")


if __name__ == "__main__":
    main()
