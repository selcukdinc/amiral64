#!/usr/bin/env python3
"""Fast software model of the retro konsol for testing 6502 programs without HDL simulation.

A 6502 (documented opcodes, decimal mode) with the konsol memory map: RAM, game RAM, boot ROM, the PPU
(registers, VRAM, OAM DMA, vblank NMI, raster IRQ; frames rendered by tools/ppu_model.py), pads, keyboard
FIFO, timer, display registers, UART and the cartridge port. Used by sw tests and to take screenshots.

    emu6502.py run <boot.bin> [--cart cart.bin] [--frames N] [--png out.png] [--pad1 "frame:mask,..."]
"""
import sys, os, struct, zlib, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ppu_model import PPU

CYCLES_PER_FRAME = 6_250_000 // 60     # 6.25 MHz CPU, 60 Hz frames
LINES = 262                            # logical timing: 240 visible + vblank, for LINE/raster IRQ

class Konsol:
    def __init__(self, rom, cart=b"", flash=b""):
        self.ram = bytearray(0x8000)
        self.game = bytearray(0x5000)
        self.rom = bytearray(rom.ljust(0x2000, b"\xff")[:0x2000])
        self.cart = bytearray(cart)
        self.flash = bytearray(flash)     # flash cartridge (bytes from flash offset $400000)
        self.cart_src = 0
        self.ppu = PPU()
        self.vaddr = 0
        self.read_buf = 0
        self.vblank = self.f_raster = False
        self.raster = 0
        self.line = 0
        self.pads = [0, 0, 0, 0]
        self.kbd = []
        self.keys = bytearray(32)
        self.sw = 0
        self.buttons = 0
        self.frames = 0
        self.timer_period = 0
        self.timer_ctrl = 0
        self.timer_expired = False
        self.timer_acc = 0
        self.disp = bytearray(10)
        self.uart_out = bytearray()
        self.uart_in = []
        self.apu = bytearray(18)
        self.apu_log = []
        self.cart_addr = 0
        self.test = None
        self.test_log = []
        self.slow = 0
        self.dma_cycles = 0
        # CPU
        self.a = self.x = self.y = 0
        self.sp = 0xFD
        self.p = 0x24
        self.pc = self.rd16(0xFFFC)
        self.cycles = 0
        self.nmi_line = False
        self.halted = False

    # ---------------------------------------------------------------- bus
    def vram_read(self, a):
        return self.ppu.vram[a] if a < len(self.ppu.vram) else 0

    def io_read(self, a):
        page, r = (a >> 8) & 15, a & 0xFF
        if page == 0:
            r &= 15
            if r == 0: return self.vaddr & 255
            if r == 1: return self.vaddr >> 8
            if r == 2:
                v = self.read_buf
                self.vaddr = (self.vaddr + 1) & 0xFFFF
                self.read_buf = self.vram_read(self.vaddr)
                return v
            if r == 3: return self.ppu.ctrl
            if r == 4:
                v = (0x80 if self.vblank else 0) | (0x40 if self.ppu.sbg else 0) | (0x20 if self.ppu.sss else 0) \
                    | (0x10 if self.f_raster else 0) | (0x08 if self.ppu.over else 0)
                self.vblank = False
                self.ppu.sbg = self.ppu.sss = self.ppu.over = False
                return v
            if r == 5: return self.ppu.scroll_x & 255
            if r == 6: return self.ppu.scroll_x >> 8
            if r == 7: return self.ppu.scroll_y
            if r == 9: return min(self.line, 255)
            if r == 10: return self.raster
            return 0
        if page == 1: return 0
        if page == 2:
            r &= 63
            if r < 4:
                v = self.pads[r]
                if r == 0:
                    b = self.buttons
                    v |= (0x80 if b & 1 else 0) | (0x08 if b & 2 else 0) | (0x02 if b & 4 else 0) \
                         | (0x01 if b & 8 else 0) | (0x04 if b & 16 else 0)
                return v
            if r == 4: return self.kbd.pop(0) if self.kbd else 0
            if r == 5: return 1 if self.kbd else 0
            if r == 8: return self.sw & 255
            if r == 9: return self.sw >> 8
            if r == 10: return self.buttons
            if 16 <= r < 48: return self.keys[r - 16]
            return 0
        if page == 3:
            r &= 15
            if r == 0:
                self.frames_hi = (self.frames >> 8) & 255
                return self.frames & 255
            if r == 1: return getattr(self, "frames_hi", 0)
            if r == 2: return self.timer_period & 255
            if r == 3: return self.timer_period >> 8
            if r == 4: return self.timer_ctrl
            if r == 5: return 1 if self.timer_expired else 0
            return 0
        if page == 4:
            r &= 15
            return self.disp[r] if r < 10 else 0
        if page == 5:
            r &= 15
            if r == 0: return self.uart_in.pop(0) if self.uart_in else 0
            if r == 1: return 1 if self.uart_in else 0
            return 0
        if page == 7:
            if r == 0x00: return 0x4B
            if r == 0x01: return 1
            if r == 0x02: return self.slow
            if r == 0x13:
                src = self.flash if self.cart_src else self.cart
                v = src[self.cart_addr] if self.cart_addr < len(src) else 0xFF
                self.cart_addr += 1
                return v
            if r == 0x14: return 1
            if r == 0x15: return self.cart_src
            if r == 0xFF: return self.test or 0
            return 0
        return 0

    def io_write(self, a, v):
        page, r = (a >> 8) & 15, a & 0xFF
        if page == 0:
            r &= 15
            if r == 0: self.vaddr = (self.vaddr & 0xFF00) | v; self.read_buf = self.vram_read(self.vaddr)
            elif r == 1: self.vaddr = (self.vaddr & 0xFF) | v << 8; self.read_buf = self.vram_read(self.vaddr)
            elif r == 2:
                if self.vaddr < len(self.ppu.vram): self.ppu.vram[self.vaddr] = v
                self.vaddr = (self.vaddr + 1) & 0xFFFF
                self.read_buf = self.vram_read(self.vaddr)
            elif r == 3: self.ppu.ctrl = v & 15
            elif r == 4:
                if v & 0x10: self.f_raster = False
            elif r == 5: self.ppu.scroll_x = (self.ppu.scroll_x & 256) | v
            elif r == 6: self.ppu.scroll_x = (self.ppu.scroll_x & 255) | (v & 1) << 8
            elif r == 7: self.ppu.scroll_y = v
            elif r == 8:
                src = (v & 0x7F) << 8
                self.ppu.vram[0x5200:0x5300] = self.ram[src:src + 256]
                self.dma_cycles += 65
            elif r == 10: self.raster = v
        elif page == 1:
            r &= 31
            if r < 18:
                self.apu[r] = v
                self.apu_log.append((self.frames, r, v))
        elif page == 3:
            r &= 15
            if r == 2: self.timer_period = (self.timer_period & 0xFF00) | v
            elif r == 3: self.timer_period = (self.timer_period & 255) | v << 8; self.timer_acc = 0
            elif r == 4: self.timer_ctrl = v & 3
            elif r == 5:
                if v & 1: self.timer_expired = False
        elif page == 4:
            r &= 15
            if r < 10: self.disp[r] = v
        elif page == 5:
            if r & 15 == 0: self.uart_out.append(v)
        elif page == 7:
            if r == 0x02: self.slow = v & 1
            elif r == 0x10: self.cart_addr = (self.cart_addr & 0xFFFF00) | v
            elif r == 0x11: self.cart_addr = (self.cart_addr & 0xFF00FF) | v << 8
            elif r == 0x12: self.cart_addr = (self.cart_addr & 0x00FFFF) | v << 16
            elif r == 0x15: self.cart_src = v & 1
            elif r == 0xFF:
                self.test = v
                self.test_log.append(v)

    def rd(self, a):
        a &= 0xFFFF
        if a < 0x8000: return self.ram[a]
        if a < 0x9000: return self.io_read(a)
        if a < 0xE000: return self.game[a - 0x9000]
        return self.rom[a - 0xE000]

    def wr(self, a, v):
        a &= 0xFFFF
        v &= 255
        if a < 0x8000: self.ram[a] = v
        elif a < 0x9000: self.io_write(a, v)
        elif a < 0xE000: self.game[a - 0x9000] = v

    def rd16(self, a):
        return self.rd(a) | self.rd(a + 1) << 8

    # ---------------------------------------------------------------- CPU
    def push(self, v):
        self.ram[0x100 + self.sp] = v & 255
        self.sp = (self.sp - 1) & 255

    def pull(self):
        self.sp = (self.sp + 1) & 255
        return self.ram[0x100 + self.sp]

    def setnz(self, v):
        self.p = (self.p & 0x7D) | (v & 0x80) | (0 if v & 255 else 2)
        return v & 255

    def interrupt(self, vector, brk=False):
        self.push(self.pc >> 8); self.push(self.pc)
        self.push((self.p | 0x20 | (0x10 if brk else 0)) & ~(0 if brk else 0x10))
        self.p |= 0x04
        self.pc = self.rd16(vector)
        self.cycles += 7

    def adc(self, v):
        c = self.p & 1
        if self.p & 8:
            lo = (self.a & 15) + (v & 15) + c
            hi = (self.a >> 4) + (v >> 4)
            if lo > 9: lo += 6
            if lo > 15: hi += 1
            z = ((self.a + v + c) & 255) == 0
            n = hi & 8
            vflag = ~(self.a ^ v) & (self.a ^ (hi << 4)) & 0x80
            if hi > 9: hi += 6
            res = ((hi << 4) | (lo & 15)) & 255
            self.p = (self.p & 0x3C) | (1 if hi > 15 else 0) | (2 if z else 0) | (0x40 if vflag else 0) | (0x80 if n else 0)
            self.a = res
        else:
            s = self.a + v + c
            vflag = ~(self.a ^ v) & (self.a ^ s) & 0x80
            self.p = (self.p & 0xBE) | (1 if s > 255 else 0) | (0x40 if vflag else 0)
            self.a = self.setnz(s)

    def sbc(self, v):
        if self.p & 8:
            c = self.p & 1
            s = self.a - v - (1 - c)
            lo = (self.a & 15) - (v & 15) - (1 - c)
            hi = (self.a >> 4) - (v >> 4)
            if lo < 0: lo -= 6; hi -= 1
            if hi < 0: hi -= 6
            vflag = (self.a ^ v) & (self.a ^ s) & 0x80
            self.p = (self.p & 0x3C) | (1 if s >= 0 else 0) | (0x40 if vflag else 0)
            self.setnz(s & 255)
            self.a = ((hi << 4) | (lo & 15)) & 255
        else:
            self.adc(v ^ 255)

    def cmp(self, r, v):
        d = r - v
        self.p = (self.p & 0xFE) | (1 if d >= 0 else 0)
        self.setnz(d)

    def addr(self, mode):
        pc = self.pc
        if mode == "imm": self.pc += 1; return pc
        if mode == "zp": self.pc += 1; return self.rd(pc)
        if mode == "zpx": self.pc += 1; return (self.rd(pc) + self.x) & 255
        if mode == "zpy": self.pc += 1; return (self.rd(pc) + self.y) & 255
        if mode == "abs": self.pc += 2; return self.rd16(pc)
        if mode == "abx": self.pc += 2; return (self.rd16(pc) + self.x) & 0xFFFF
        if mode == "aby": self.pc += 2; return (self.rd16(pc) + self.y) & 0xFFFF
        if mode == "izx":
            self.pc += 1; z = (self.rd(pc) + self.x) & 255
            return self.rd(z) | self.rd((z + 1) & 255) << 8
        if mode == "izy":
            self.pc += 1; z = self.rd(pc)
            return ((self.rd(z) | self.rd((z + 1) & 255) << 8) + self.y) & 0xFFFF
        if mode == "ind":
            self.pc += 2; p = self.rd16(pc)
            return self.rd(p) | self.rd((p & 0xFF00) | ((p + 1) & 255)) << 8
        raise ValueError(mode)

    def step(self):
        op = self.rd(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        e = OPS.get(op)
        if e is None:
            raise RuntimeError(f"illegal opcode ${op:02X} at ${(self.pc - 1) & 0xFFFF:04X}")
        name, mode, cyc = e
        self.cycles += cyc
        getattr(self, "op_" + name)(mode)

    # loads/stores
    def op_lda(self, m): self.a = self.setnz(self.rd(self.addr(m)))
    def op_ldx(self, m): self.x = self.setnz(self.rd(self.addr(m)))
    def op_ldy(self, m): self.y = self.setnz(self.rd(self.addr(m)))
    def op_sta(self, m): self.wr(self.addr(m), self.a)
    def op_stx(self, m): self.wr(self.addr(m), self.x)
    def op_sty(self, m): self.wr(self.addr(m), self.y)
    def op_tax(self, m): self.x = self.setnz(self.a)
    def op_tay(self, m): self.y = self.setnz(self.a)
    def op_txa(self, m): self.a = self.setnz(self.x)
    def op_tya(self, m): self.a = self.setnz(self.y)
    def op_tsx(self, m): self.x = self.setnz(self.sp)
    def op_txs(self, m): self.sp = self.x
    def op_pha(self, m): self.push(self.a)
    def op_php(self, m): self.push(self.p | 0x30)
    def op_pla(self, m): self.a = self.setnz(self.pull())
    def op_plp(self, m): self.p = (self.pull() & 0xEF) | 0x20
    # arithmetic / logic
    def op_adc(self, m): self.adc(self.rd(self.addr(m)))
    def op_sbc(self, m): self.sbc(self.rd(self.addr(m)))
    def op_and(self, m): self.a = self.setnz(self.a & self.rd(self.addr(m)))
    def op_ora(self, m): self.a = self.setnz(self.a | self.rd(self.addr(m)))
    def op_eor(self, m): self.a = self.setnz(self.a ^ self.rd(self.addr(m)))
    def op_cmp(self, m): self.cmp(self.a, self.rd(self.addr(m)))
    def op_cpx(self, m): self.cmp(self.x, self.rd(self.addr(m)))
    def op_cpy(self, m): self.cmp(self.y, self.rd(self.addr(m)))
    def op_bit(self, m):
        v = self.rd(self.addr(m))
        self.p = (self.p & 0x3D) | (v & 0xC0) | (0 if self.a & v else 2)
    def rmw(self, m, f):
        if m == "acc":
            self.a = f(self.a)
        else:
            a = self.addr(m)
            self.wr(a, f(self.rd(a)))
    def _asl(self, v): self.p = (self.p & 0xFE) | (v >> 7); return self.setnz(v << 1)
    def _lsr(self, v): self.p = (self.p & 0xFE) | (v & 1); return self.setnz(v >> 1)
    def _rol(self, v):
        c = self.p & 1; self.p = (self.p & 0xFE) | (v >> 7); return self.setnz(v << 1 | c)
    def _ror(self, v):
        c = self.p & 1; self.p = (self.p & 0xFE) | (v & 1); return self.setnz(v >> 1 | c << 7)
    def op_asl(self, m): self.rmw(m, self._asl)
    def op_lsr(self, m): self.rmw(m, self._lsr)
    def op_rol(self, m): self.rmw(m, self._rol)
    def op_ror(self, m): self.rmw(m, self._ror)
    def op_inc(self, m): self.rmw(m, lambda v: self.setnz(v + 1))
    def op_dec(self, m): self.rmw(m, lambda v: self.setnz(v - 1))
    def op_inx(self, m): self.x = self.setnz(self.x + 1)
    def op_iny(self, m): self.y = self.setnz(self.y + 1)
    def op_dex(self, m): self.x = self.setnz(self.x - 1)
    def op_dey(self, m): self.y = self.setnz(self.y - 1)
    # flow
    def branch(self, cond):
        off = self.rd(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        if cond:
            self.cycles += 1
            self.pc = (self.pc + (off - 256 if off & 128 else off)) & 0xFFFF
    def op_bpl(self, m): self.branch(not self.p & 0x80)
    def op_bmi(self, m): self.branch(self.p & 0x80)
    def op_bvc(self, m): self.branch(not self.p & 0x40)
    def op_bvs(self, m): self.branch(self.p & 0x40)
    def op_bcc(self, m): self.branch(not self.p & 1)
    def op_bcs(self, m): self.branch(self.p & 1)
    def op_bne(self, m): self.branch(not self.p & 2)
    def op_beq(self, m): self.branch(self.p & 2)
    def op_jmp(self, m): self.pc = self.addr(m)
    def op_jsr(self, m):
        t = self.addr("abs")
        r = (self.pc - 1) & 0xFFFF
        self.push(r >> 8); self.push(r)
        self.pc = t
    def op_rts(self, m): self.pc = ((self.pull() | self.pull() << 8) + 1) & 0xFFFF
    def op_rti(self, m):
        self.p = (self.pull() & 0xEF) | 0x20
        self.pc = self.pull() | self.pull() << 8
    def op_brk(self, m):
        self.pc = (self.pc + 1) & 0xFFFF
        self.interrupt(0xFFFE, brk=True)
    def op_nop(self, m): pass
    def op_clc(self, m): self.p &= 0xFE
    def op_sec(self, m): self.p |= 1
    def op_cli(self, m): self.p &= 0xFB
    def op_sei(self, m): self.p |= 4
    def op_cld(self, m): self.p &= 0xF7
    def op_sed(self, m): self.p |= 8
    def op_clv(self, m): self.p &= 0xBF

    # ---------------------------------------------------------------- frames
    def run_cycles(self, n):
        end = self.cycles + n
        while self.cycles < end:
            irq = (self.timer_expired and self.timer_ctrl & 2) or (self.f_raster and self.ppu.ctrl & 8)
            if irq and not self.p & 4:
                self.interrupt(0xFFFE)
            self.step()
            if self.timer_ctrl & 1:
                self.timer_acc += 4 * (14 if self.slow else 4)  # system clocks per CPU cycle -> 25 per us
                while self.timer_acc >= 25 * (self.timer_period + 1):
                    self.timer_acc -= 25 * (self.timer_period + 1)
                    self.timer_expired = True

    def frame(self, render=False):
        """One frame: visible lines (raster IRQ), vblank (NMI). Returns the picture when render."""
        per_line = CYCLES_PER_FRAME // LINES
        pic = [] if render else None
        prev = (self.ppu.scroll_x, self.ppu.scroll_y)
        for L in range(LINES):
            self.line = L
            if render and L < 240:
                # line by line, so raster splits show: a scroll written during line L-2 applies from line L
                cur = (self.ppu.scroll_x, self.ppu.scroll_y)
                self.ppu.scroll_x, self.ppu.scroll_y = prev
                pic.append(self.ppu.line(L))
                self.ppu.scroll_x, self.ppu.scroll_y = cur
                prev = cur
            if L == 240:
                self.vblank = True
                self.frames += 1
                if self.ppu.ctrl & 1:
                    self.interrupt(0xFFFA)
            if L < 240 and L == self.raster:
                self.f_raster = True
            if L == 0:
                self.vblank = False
            self.run_cycles(per_line)
        return pic

def write_png(path, pic, scale=2):
    h, w = len(pic), len(pic[0])
    raw = bytearray()
    for row in pic:
        line = bytearray()
        for c in row:
            r, g, b = (c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17
            line += bytes((r, g, b)) * scale
        for _ in range(scale):
            raw += b"\x00" + line
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))

# opcode table: name, addressing mode, base cycles
OPS = {}
def _ops(name, *pairs):
    for code, mode, cyc in pairs:
        OPS[code] = (name, mode, cyc)
_ops("adc", (0x69,"imm",2),(0x65,"zp",3),(0x75,"zpx",4),(0x6D,"abs",4),(0x7D,"abx",4),(0x79,"aby",4),(0x61,"izx",6),(0x71,"izy",5))
_ops("and", (0x29,"imm",2),(0x25,"zp",3),(0x35,"zpx",4),(0x2D,"abs",4),(0x3D,"abx",4),(0x39,"aby",4),(0x21,"izx",6),(0x31,"izy",5))
_ops("asl", (0x0A,"acc",2),(0x06,"zp",5),(0x16,"zpx",6),(0x0E,"abs",6),(0x1E,"abx",7))
_ops("bit", (0x24,"zp",3),(0x2C,"abs",4))
for n, c in (("bpl",0x10),("bmi",0x30),("bvc",0x50),("bvs",0x70),("bcc",0x90),("bcs",0xB0),("bne",0xD0),("beq",0xF0)):
    _ops(n, (c, "rel", 2))
_ops("brk", (0x00,"imp",7))
_ops("cmp", (0xC9,"imm",2),(0xC5,"zp",3),(0xD5,"zpx",4),(0xCD,"abs",4),(0xDD,"abx",4),(0xD9,"aby",4),(0xC1,"izx",6),(0xD1,"izy",5))
_ops("cpx", (0xE0,"imm",2),(0xE4,"zp",3),(0xEC,"abs",4))
_ops("cpy", (0xC0,"imm",2),(0xC4,"zp",3),(0xCC,"abs",4))
_ops("dec", (0xC6,"zp",5),(0xD6,"zpx",6),(0xCE,"abs",6),(0xDE,"abx",7))
_ops("eor", (0x49,"imm",2),(0x45,"zp",3),(0x55,"zpx",4),(0x4D,"abs",4),(0x5D,"abx",4),(0x59,"aby",4),(0x41,"izx",6),(0x51,"izy",5))
for n, c in (("clc",0x18),("sec",0x38),("cli",0x58),("sei",0x78),("clv",0xB8),("cld",0xD8),("sed",0xF8),
             ("tax",0xAA),("txa",0x8A),("dex",0xCA),("inx",0xE8),("tay",0xA8),("tya",0x98),("dey",0x88),("iny",0xC8),
             ("txs",0x9A),("tsx",0xBA),("nop",0xEA),("rts",0x60),("rti",0x40)):
    _ops(n, (c, "imp", 6 if n in ("rts", "rti") else 2))
_ops("pha", (0x48,"imp",3)); _ops("php", (0x08,"imp",3)); _ops("pla", (0x68,"imp",4)); _ops("plp", (0x28,"imp",4))
_ops("inc", (0xE6,"zp",5),(0xF6,"zpx",6),(0xEE,"abs",6),(0xFE,"abx",7))
_ops("jmp", (0x4C,"abs",3),(0x6C,"ind",5))
_ops("jsr", (0x20,"abs",6))
_ops("lda", (0xA9,"imm",2),(0xA5,"zp",3),(0xB5,"zpx",4),(0xAD,"abs",4),(0xBD,"abx",4),(0xB9,"aby",4),(0xA1,"izx",6),(0xB1,"izy",5))
_ops("ldx", (0xA2,"imm",2),(0xA6,"zp",3),(0xB6,"zpy",4),(0xAE,"abs",4),(0xBE,"aby",4))
_ops("ldy", (0xA0,"imm",2),(0xA4,"zp",3),(0xB4,"zpx",4),(0xAC,"abs",4),(0xBC,"abx",4))
_ops("lsr", (0x4A,"acc",2),(0x46,"zp",5),(0x56,"zpx",6),(0x4E,"abs",6),(0x5E,"abx",7))
_ops("ora", (0x09,"imm",2),(0x05,"zp",3),(0x15,"zpx",4),(0x0D,"abs",4),(0x1D,"abx",4),(0x19,"aby",4),(0x01,"izx",6),(0x11,"izy",5))
_ops("rol", (0x2A,"acc",2),(0x26,"zp",5),(0x36,"zpx",6),(0x2E,"abs",6),(0x3E,"abx",7))
_ops("ror", (0x6A,"acc",2),(0x66,"zp",5),(0x76,"zpx",6),(0x6E,"abs",6),(0x7E,"abx",7))
_ops("sbc", (0xE9,"imm",2),(0xE5,"zp",3),(0xF5,"zpx",4),(0xED,"abs",4),(0xFD,"abx",4),(0xF9,"aby",4),(0xE1,"izx",6),(0xF1,"izy",5))
_ops("sta", (0x85,"zp",3),(0x95,"zpx",4),(0x8D,"abs",4),(0x9D,"abx",5),(0x99,"aby",5),(0x81,"izx",6),(0x91,"izy",6))
_ops("stx", (0x86,"zp",3),(0x96,"zpy",4),(0x8E,"abs",4))
_ops("sty", (0x84,"zp",3),(0x94,"zpx",4),(0x8C,"abs",4))

def parse_pad(spec):
    """'10:08,40:00' -> {10: 0x08, 40: 0x00}: pad value from that frame on."""
    out = {}
    for part in filter(None, (spec or "").split(",")):
        f, m = part.split(":")
        out[int(f)] = int(m, 16)
    return out

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["run"])
    ap.add_argument("rom")
    ap.add_argument("--cart")
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--png")
    ap.add_argument("--pad1")
    ap.add_argument("--pad2")
    a = ap.parse_args()
    k = Konsol(open(a.rom, "rb").read(), open(a.cart, "rb").read() if a.cart else b"")
    p1, p2 = parse_pad(a.pad1), parse_pad(a.pad2)
    for f in range(a.frames):
        if f in p1: k.pads[0] = p1[f]
        if f in p2: k.pads[1] = p2[f]
        pic = k.frame(render=bool(a.png) and f == a.frames - 1)
    if a.png:
        write_png(a.png, pic)
    print(f"frames {a.frames}, test {k.test if k.test is None else hex(k.test)}, uart {bytes(k.uart_out)!r}, "
          f"7seg {bytes(k.disp[3::-1]).hex()}")

if __name__ == "__main__":
    main()
