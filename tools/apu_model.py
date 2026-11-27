#!/usr/bin/env python3
"""Python model of the konsol APU (hw/apu.vhd) that turns register writes into sound.

    from apu_model import APU, render_log, write_wav
    samples = render_log(konsol.apu_log, frames)       # the emulator's (frame, register, value) list
    write_wav("out.wav", samples)

The model follows the VHDL: channel timers at 1.5625 MHz (square: 8 steps a cycle, triangle: 32 steps,
noise: an LFSR step every P + 1 ticks), decay envelopes and lengths at 60 Hz (the frame tick comes first,
then the frame's register writes), the mix (sum of the channels x master volume) as the delta-sigma
output level. Each frame is computed 4x oversampled and averaged down to 44.1 kHz; the board's output
filter is modelled with a DC block and a 1-pole low-pass at 12 kHz.
"""
import struct, wave
import numpy as np
from scipy.signal import lfilter

TICK = 25_000_000 / 16          # channel timer clock
RATE = 44100
OVER = 4
SPF = RATE / 60                 # samples a frame (735)
DUTY = [np.array([s == 7 for s in range(8)]), np.array([s >= 6 for s in range(8)]),
        np.array([s >= 4 for s in range(8)]), np.array([s >= 2 for s in range(8)])]
TRI = np.array([s if s < 16 else 31 - s for s in range(32)])


def lfsr_bits(tap, n=32767 * 2):
    """output bits (bit 0 of the register after each step) of the noise LFSR, from the reset state 1"""
    r, out = 1, np.zeros(n, dtype=np.int8)
    for i in range(n):
        r = ((r << 1) & 0x7FFF) | ((r & 1) ^ ((r >> tap) & 1))
        out[i] = r & 1
    return out


NOISE_LONG = lfsr_bits(1)
NOISE_SHORT = lfsr_bits(6)


class APU:
    def __init__(self, version=1):
        self.version = version
        self.nch = 5 if version >= 2 else 4
        self.period = [0] * 5
        self.ctl = [0] * 5
        self.env = [0] * 5
        self.vol = [0] * 5
        self.decay_cnt = [0] * 5
        self.len = [0] * 5
        self.active = [False] * 5
        self.phase = [0.0] * 5          # in steps (square 0..8, triangle / wave 0..32, noise: LFSR steps)
        self.master = 0
        self.enable = 0
        self.wave_ram = [0] * 16
        self.sweep = [0, 0]
        self.sweep_cnt = [0, 0]
        self.adsr = [0] * 5
        self.atk_cnt = [0] * 5
        self.attacking = [False] * 5
        self.target = [0] * 5
        self.filter = 0
        self.lp = 0.0
        self.dc = 0.0
        self.hp_prev = 0.0

    # ------------------------------------------------------------ registers
    def write(self, r, v):
        ch, k = r >> 2, r & 3
        if r < 16 or (self.version >= 2 and 0x14 <= r <= 0x17):
            if r >= 0x14:
                ch = 4
            if k == 0:
                self.period[ch] = (self.period[ch] & 0xF00) | v
            elif k == 1:
                self.period[ch] = (self.period[ch] & 0xFF) | (v & 15) << 8
            elif k == 2:
                self.ctl[ch] = v
            else:
                self.env[ch] = v
                self.start(ch)
        elif r == 0x10:
            self.master = v & 15
        elif r == 0x11:
            self.enable = v & (0x1F if self.version >= 2 else 0x0F)
        elif self.version >= 2:
            if r == 0x13:
                self.filter = v & 3
            elif r in (0x18, 0x19):
                self.sweep[r - 0x18] = v
                self.sweep_cnt[r - 0x18] = 0
            elif 0x1A <= r <= 0x1E:
                self.adsr[r - 0x1A] = v
            elif 0x20 <= r <= 0x2F:
                self.wave_ram[r - 0x20] = v

    def start(self, ch):
        target = self.ctl[ch] & 15
        self.decay_cnt[ch] = 0
        self.len[ch] = (self.env[ch] >> 4) * 4
        self.active[ch] = True
        atk = self.adsr[ch] >> 4 if self.version >= 2 else 0
        self.target[ch] = target
        if atk:
            self.vol[ch] = 0
            self.attacking[ch] = True
            self.atk_cnt[ch] = 0
        else:
            self.vol[ch] = target
            self.attacking[ch] = False

    # ------------------------------------------------------------ 60 Hz
    def frame_tick(self):
        for c in range(self.nch):
            if self.attacking[c]:
                if self.atk_cnt[c] >= (self.adsr[c] >> 4) - 1:
                    self.atk_cnt[c] = 0
                    if self.vol[c] < self.target[c]:
                        self.vol[c] += 1
                    if self.vol[c] >= self.target[c]:
                        self.attacking[c] = False
                else:
                    self.atk_cnt[c] += 1
            elif self.ctl[c] & 0x80 and self.vol[c] != 0:
                sustain = self.adsr[c] & 15 if self.version >= 2 else 0
                if self.decay_cnt[c] == self.env[c] & 15:
                    self.decay_cnt[c] = 0
                    if self.vol[c] > sustain:
                        self.vol[c] -= 1
                else:
                    self.decay_cnt[c] += 1
            if self.len[c] == 1:
                self.active[c] = False
            if self.len[c]:
                self.len[c] -= 1
        if self.version >= 2:
            for c in (0, 1):
                s = self.sweep[c]
                if s & 0x80 and s & 7:
                    if self.sweep_cnt[c] >= (s >> 4) & 7:
                        self.sweep_cnt[c] = 0
                        d = self.period[c] >> (s & 7)
                        p = self.period[c] - d if s & 8 else self.period[c] + d
                        self.period[c] = min(max(p, 0), 4095)
                    else:
                        self.sweep_cnt[c] += 1

    # ------------------------------------------------------------ one frame of sound
    def render(self, n):
        """n output samples (44.1 kHz) as float mix levels 0..1023 (before the output filter)"""
        m = n * OVER
        dt = TICK / (RATE * OVER)               # timer ticks a sub-sample
        total = np.zeros(m, dtype=np.int64)
        for c in range(self.nch):
            p = self.period[c] + 1
            steps = self.phase[c] + np.arange(1, m + 1) * (dt / p)
            self.phase[c] = steps[-1]
            on = self.active[c] and self.enable >> c & 1
            if not on:
                continue
            idx = steps.astype(np.int64)
            if c < 2:
                out = DUTY[(self.ctl[c] >> 4) & 3][idx & 7] * self.vol[c]
                self.phase[c] %= 8
            elif c == 2:
                t = TRI[idx & 31]
                prod = t * self.vol[c]
                out = (prod >> 4) + ((prod >> 3) & 1)
                self.phase[c] %= 32
            elif c == 3:
                bits = NOISE_SHORT if self.ctl[3] & 0x40 else NOISE_LONG
                out = bits[idx % len(bits)] * self.vol[c]
                self.phase[c] %= len(bits)
            else:
                ws = idx & 31
                packed = np.array(self.wave_ram)
                samp = np.where(ws & 1, packed[ws >> 1] & 15, packed[ws >> 1] >> 4)
                prod = samp * self.vol[c]
                out = (prod >> 4) + ((prod >> 3) & 1)
                self.phase[c] %= 32
            total = total + out
        prod = (total * self.master).astype(np.int64)
        mix = np.minimum(prod + (prod >> 3), 1023).astype(float)
        if self.version >= 2 and self.filter:
            # 1-pole low-pass at the 1.5625 MHz channel rate: y += (x - y) / 2^(filter + 3)
            a = 1.0 - (1.0 - 2.0 ** -(self.filter + 3)) ** (TICK / (RATE * OVER))
            mix, zf = lfilter([a], [1, a - 1], mix, zi=[self.lp * (1 - a)])
            self.lp = mix[-1]
        return mix.reshape(n, OVER).mean(axis=1)


def render_log(log, frames, version=1, start=0):
    """samples (float, -1..1) of frames [start, start + frames) of an emulator APU log"""
    apu = APU(version)
    by_frame = {}
    for f, r, v in log:
        by_frame.setdefault(f, []).append((r, v))
    out = []
    acc = 0.0
    for f in range(start + frames):
        apu.frame_tick()
        for r, v in by_frame.get(f, ()):
            apu.write(r, v)
        acc += SPF
        n = int(acc)
        acc -= n
        s = apu.render(n)
        if f >= start:
            out.append(s)
    x = np.concatenate(out) / 1023.0 if out else np.zeros(0)
    return board_filter(x)


def board_filter(x):
    """DC block (the amplifier's coupling) and a 1-pole low-pass at 12 kHz"""
    a = 1 - np.exp(-2 * np.pi * 12000 / RATE)
    hp = 1 - 2 * np.pi * 20 / RATE
    lp = lfilter([a], [1, a - 1], x)
    return lfilter([1, -1], [1, -hp], lp)


def write_wav(path, x, gain=1.6):
    x = np.clip(x * gain, -1, 1)
    data = (x * 32000).astype("<i2").tobytes()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data)


def tone_stats(log, frames, ch_level=None, version=1):
    """for tests: (frequency of the strongest component in Hz, rms) of the mix over the frames"""
    x = render_log(log, frames, version)
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / RATE)
    return float(f[np.argmax(spec[1:]) + 1]), float(np.sqrt(np.mean(x ** 2)))
