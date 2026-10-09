"""Headless NES for build-time data generation from the user's Zelda ROM.

The builder must not depend on emulator captures. Where a build input is
"what the NES has in PPU memory after the game draws X", this machine runs
the game's own code from the user's ROM and reads the same memory a
BizHawk probe would have read: CPU RAM/WRAM, CHR-RAM, CIRAM (nametables),
PALRAM, OAM.

Model (enough for Zelda 1, which is mapper 1 / CHR-RAM):
  - CPU: py65 MPU6502 (Zelda never sets decimal mode).
  - Mapper 1 (MMC1): 5-write serial port, control/CHR/PRG registers,
    PRG modes 0-3, nametable mirroring 0-3. CHR-RAM is one 8 KB bank.
  - PPU memory: $2000-$2007 with the v/t/w latch, +1/+32 increment,
    buffered $2007 reads, mirrored nametables, palette writes.
  - PALRAM is stored the way BizHawk's NesHawk "PALRAM" domain shows it:
    writes to $3F10/$3F14/$3F18/$3F1C land in $3F00/$3F04/$3F08/$3F0C and
    the four mirror cells keep their power-on value (0).
  - Timing: 29781 CPU cycles per frame. Vblank flag set at frame start (NMI
    if PPUCTRL bit 7), cleared after 2273 cycles; sprite-0 hit set at the
    scanline given by SPRITE0_LINE and cleared at the end of vblank. No
    pixel rendering.
  - Controller 1 via set_buttons(); APU writes are ignored, reads return 0.
"""
from __future__ import annotations

from py65.devices.mpu6502 import MPU

CYCLES_PER_FRAME = 29781          # NTSC, rounded (341*262/3)
VBLANK_CYCLES = 2273              # 20 scanlines
CYCLES_PER_LINE = 113.667
SPRITE0_LINE = 40                 # Zelda's status-bar split sprite is near line 40

BUTTONS = ("A", "B", "Select", "Start", "Up", "Down", "Left", "Right")


class NesMemory:
    """CPU address space. py65 indexes it like a list."""

    def __init__(self, nes: "Nes"):
        self.nes = nes

    def __getitem__(self, addr):
        n = self.nes
        if addr < 0x2000:
            return n.ram[addr & 0x7FF]
        if addr >= 0x8000:
            if addr >= 0xC000:
                return n.prg_hi[addr - 0xC000]
            return n.prg_lo[addr - 0x8000]
        if addr >= 0x6000:
            return n.wram[addr - 0x6000]
        if addr < 0x4000:
            return n.ppu_read(addr & 7)
        if addr == 0x4016:
            return n.read_pad()
        return 0

    def __setitem__(self, addr, value):
        n = self.nes
        if addr < 0x2000:
            n.ram[addr & 0x7FF] = value
        elif addr >= 0x8000:
            n.mmc1_write(addr, value)
        elif addr >= 0x6000:
            n.wram[addr - 0x6000] = value
        elif addr < 0x4000:
            n.ppu_write(addr & 7, value)
        elif addr == 0x4014:
            page = value << 8
            mem = self
            oam, at = n.oam, n.oam_addr
            for i in range(256):
                oam[(at + i) & 0xFF] = mem[page + i]
            n.cpu.processorCycles += 513
        elif addr == 0x4016:
            n.pad_strobe = value & 1
            if n.pad_strobe:
                n.pad_shift = n.pad_state


class Nes:
    def __init__(self, ines: bytes):
        if ines[:4] != b"NES\x1a":
            raise ValueError("not an iNES image")
        if (ines[6] >> 4) | (ines[7] & 0xF0) != 1 or ines[5] != 0:
            raise ValueError("expected mapper 1 with CHR-RAM")
        nbanks = ines[4]
        self.prg = [ines[16 + i * 0x4000: 16 + (i + 1) * 0x4000] for i in range(nbanks)]
        self.ram = bytearray(0x800)
        self.wram = bytearray(0x2000)
        self.chr = bytearray(0x2000)
        self.ciram = bytearray(0x800)
        self.palram = bytearray(0x20)
        self.oam = bytearray(0x100)
        self.oam_addr = 0
        # MMC1 power-on: PRG mode 3 (last bank fixed at $C000).
        self.mmc1_shift = 0x10
        self.mmc1_control = 0x0C
        self.mmc1_prg = 0
        self.mmc1_chr0 = 0
        self.mmc1_chr1 = 0
        self._map_prg()
        # PPU
        self.ppu_ctrl = 0
        self.ppu_mask = 0
        self.vblank = False
        self.sprite0 = False
        self.v = 0
        self.t = 0
        self.w = 0
        self.read_buf = 0
        # pad
        self.pad_state = 0
        self.pad_shift = 0
        self.pad_strobe = 0
        # cpu
        self.memory = NesMemory(self)
        self.cpu = MPU(memory=self.memory, pc=None)   # pc=None: start at the reset vector
        self.cpu.reset()
        self.frame = 0
        self.frame_start = 0

    # ---- mapper ----
    def _map_prg(self):
        mode = (self.mmc1_control >> 2) & 3
        bank = self.mmc1_prg & 0x0F
        last = len(self.prg) - 1
        if mode < 2:
            b = bank & ~1
            self.prg_lo, self.prg_hi = self.prg[b], self.prg[b + 1]
        elif mode == 2:
            self.prg_lo, self.prg_hi = self.prg[0], self.prg[bank]
        else:
            self.prg_lo, self.prg_hi = self.prg[bank], self.prg[last]

    def mmc1_write(self, addr, value):
        if value & 0x80:
            self.mmc1_shift = 0x10
            self.mmc1_control |= 0x0C
            self._map_prg()
            return
        full = self.mmc1_shift & 1
        self.mmc1_shift = (self.mmc1_shift >> 1) | ((value & 1) << 4)
        if not full:
            return
        data = self.mmc1_shift
        self.mmc1_shift = 0x10
        reg = (addr >> 13) & 3
        if reg == 0:
            self.mmc1_control = data
        elif reg == 1:
            self.mmc1_chr0 = data
        elif reg == 2:
            self.mmc1_chr1 = data
        else:
            self.mmc1_prg = data
        self._map_prg()

    # ---- PPU ----
    def _nt_index(self, a):
        a &= 0x0FFF
        m = self.mmc1_control & 3
        if m == 0:
            return a & 0x3FF
        if m == 1:
            return 0x400 | (a & 0x3FF)
        if m == 2:                                  # vertical
            return a & 0x7FF
        return ((a >> 1) & 0x400) | (a & 0x3FF)     # horizontal

    def _pal_index(self, a):
        a &= 0x1F
        if a & 0x13 == 0x10:
            a &= 0x0F
        return a

    def ppu_bus_write(self, a, value):
        a &= 0x3FFF
        if a < 0x2000:
            self.chr[a] = value
        elif a < 0x3F00:
            self.ciram[self._nt_index(a)] = value
        else:
            self.palram[self._pal_index(a)] = value & 0x3F

    def ppu_bus_read(self, a):
        a &= 0x3FFF
        if a < 0x2000:
            return self.chr[a]
        if a < 0x3F00:
            return self.ciram[self._nt_index(a)]
        return self.palram[self._pal_index(a)]

    def _frame_cycle(self):
        return self.cpu.processorCycles - self.frame_start

    def ppu_read(self, reg):
        if reg == 2:
            c = self._frame_cycle()
            vb = self.vblank and c < VBLANK_CYCLES
            s0 = (c >= VBLANK_CYCLES + SPRITE0_LINE * CYCLES_PER_LINE) or \
                 (self.sprite0 and c < VBLANK_CYCLES)
            self.vblank = False
            self.w = 0
            return (0x80 if vb else 0) | (0x40 if s0 else 0)
        if reg == 4:
            return self.oam[self.oam_addr]
        if reg == 7:
            a = self.v & 0x3FFF
            if a >= 0x3F00:
                value = self.ppu_bus_read(a)
                self.read_buf = self.ppu_bus_read(a - 0x1000)
            else:
                value, self.read_buf = self.read_buf, self.ppu_bus_read(a)
            self.v = (self.v + (32 if self.ppu_ctrl & 4 else 1)) & 0x7FFF
            return value
        return 0

    def ppu_write(self, reg, value):
        if reg == 0:
            self.ppu_ctrl = value
            self.t = (self.t & 0x73FF) | ((value & 3) << 10)
        elif reg == 1:
            self.ppu_mask = value
        elif reg == 3:
            self.oam_addr = value
        elif reg == 4:
            self.oam[self.oam_addr] = value
            self.oam_addr = (self.oam_addr + 1) & 0xFF
        elif reg == 5:
            if self.w == 0:
                self.t = (self.t & 0x7FE0) | (value >> 3)
            else:
                self.t = (self.t & 0x0C1F) | ((value & 7) << 12) | ((value & 0xF8) << 2)
            self.w ^= 1
        elif reg == 6:
            if self.w == 0:
                self.t = (self.t & 0x00FF) | ((value & 0x3F) << 8)
            else:
                self.t = (self.t & 0x7F00) | value
                self.v = self.t
            self.w ^= 1
        elif reg == 7:
            self.ppu_bus_write(self.v, value)
            self.v = (self.v + (32 if self.ppu_ctrl & 4 else 1)) & 0x7FFF

    # ---- controller ----
    def set_buttons(self, *names):
        state = 0
        for nm in names:
            state |= 1 << BUTTONS.index(nm)
        self.pad_state = state

    def read_pad(self):
        if self.pad_strobe:
            return 0x40 | (self.pad_state & 1)
        bit = self.pad_shift & 1
        self.pad_shift = (self.pad_shift >> 1) | 0x80
        return 0x40 | bit

    # ---- execution ----
    def run_frame(self):
        """One frame: vblank start (+NMI), then CPU until the frame's cycles."""
        cpu = self.cpu
        self.frame_start = cpu.processorCycles
        self.vblank = True
        self.sprite0 = True          # set in the previous frame, cleared at vblank end
        if self.ppu_ctrl & 0x80:
            cpu.nmi()
        end = self.frame_start + CYCLES_PER_FRAME
        step = cpu.step
        while cpu.processorCycles < end:
            step()
        self.frame += 1

    def run_frames(self, n, until=None):
        for _ in range(n):
            self.run_frame()
            if until is not None and until(self):
                return True
        return until is None

    # ---- memory views (BizHawk domain equivalents) ----
    def bus(self, addr):
        return self.memory[addr]

    def poke(self, addr, value):
        self.memory[addr] = value & 0xFF
