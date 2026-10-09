"""Run a lockstep preset on the Genesis build headlessly (tools/emu/gpgx.py).

Same load path as tools/lockstep/capture.lua (GEN, gen_entry "fs"): the
preset's save block is written into cart SRAM (logical byte k at index
2k+1) before frame 1, Start at the title and Start on slot 0, sync on the
first live play tick (GameMode $05, RoomId set, FrameCounter advancing),
then the script ("UDLRABSs" per frame; s = C) under the preset's clock.

    python tools/emu/preset_run.py t002_q2_l1 --shot out.png
    python tools/emu/preset_run.py t002_q2_l1 --state out.state

As a module: run_preset(name) -> Genesis positioned after the script.
"""
from __future__ import annotations

import argparse
import ctypes as C
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools" / "lockstep"))
from gpgx import Genesis  # noqa: E402

NES_BASE = 0xFF8000            # NES RAM mirror in 68k work RAM (A4)
MEMORY_SAVE_RAM = 0
PLAY_MODES = (0x05, 0x09, 0x0B)
KEYS = {"U": "UP", "D": "DOWN", "L": "LEFT", "R": "RIGHT", "A": "A", "B": "B",
        "S": "START", "s": "C"}


class Session:
    def __init__(self, rom: Path = ROOT / "builds" / "Debug.md"):
        self.g = Genesis(rom)

    def nes(self, addr: int, n: int = 1) -> bytes:
        ram = self.g.ram()
        off = (NES_BASE + addr) & 0xFFFF
        return ram[off:off + n]

    def nes8(self, addr: int) -> int:
        return self.nes(addr)[0]

    def write_nes(self, addr: int, value: int):
        self.g.write_ram(NES_BASE + addr, value)

    def write_save(self, slot0: list[int]):
        lib = self.g.lib
        p = lib.retro_get_memory_data(MEMORY_SAVE_RAM)
        if not p:
            raise RuntimeError("no cart SRAM")
        buf = C.cast(p, C.POINTER(C.c_ubyte))
        for k, b in enumerate(slot0):
            buf[2 * k + 1] = b

    def press(self, keys: str, frames: int = 1):
        self.g.run(frames, *[KEYS[c] for c in keys])

    def enter_fs(self):
        g = self.g
        g.run(120); g.run(6, "START"); g.run(120); g.run(6, "START")
        for _ in range(1500):
            if self.nes8(0x12) == 0x05 and self.nes8(0xEB) != 0:
                break
            g.run(1)
        else:
            raise RuntimeError(f"GameMode never reached $05 (last ${self.nes8(0x12):02X})")
        for _ in range(300):
            before = self.nes8(0x15)
            g.run(1)
            if self.nes8(0x15) != before:
                return
        raise RuntimeError("FrameCounter never advanced after GameMode $05")

    def script(self, steps, clock: str = "tick", stages=(), cap: int | None = None,
               on_step=None):
        """Script steps; stages [(at, lua body)] run before step `at`, as
        capture.lua runs them at the tick that reaches `at`."""
        seq = [btn for n, btn in steps for _ in range(n)]
        pending = sorted(stages, key=lambda t: t[0])
        i, frames, cap = 0, 0, cap or len(seq) * 5 + 3000
        while i < len(seq) and frames < cap:
            while pending and pending[0][0] <= i:
                self.run_stage(pending.pop(0)[1])
            play = clock != "play" or self.nes8(0x12) in PLAY_MODES
            self.press(seq[i])
            frames += 1
            if play:
                i += 1
                if on_step:
                    on_step(i)
        if i < len(seq):
            raise RuntimeError(f"script clock: {i}/{len(seq)} steps in {cap} frames")
        for _, body in pending:
            self.run_stage(body)

    # ---- Lua stages (tools/lockstep/presets.py), GEN side of capture.lua ----
    def symbol(self, name: str) -> int:
        if not hasattr(self, "_elf"):
            import subprocess
            elf = ROOT / "build" / "debug_project" / "out" / "Debug.out"
            out = subprocess.run(["m68k-linux-gnu-nm", str(elf)], capture_output=True, text=True).stdout
            self._elf = {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines()) if len(p) == 3}
        return 0xFF0000 | (self._elf[name] & 0xFFFF)

    def _w(self, addr: int, value: int, size: int):
        for k in range(size):
            self.g.write_ram(addr + k, (value >> (8 * (size - 1 - k))) & 0xFF)

    def _r(self, addr: int, size: int) -> int:
        ram = self.g.ram()
        return int.from_bytes(ram[addr & 0xFFFF:(addr & 0xFFFF) + size], "big")

    def gen_b_item(self, v):
        a = self.symbol("s_b_item")
        self._w(a, int(v), 4)
        if self._r(a, 4) != int(v):
            raise RuntimeError("gen_b_item write did not stick")

    def gen_link_pos(self, x, y, nes_dir):
        face = {0x01: 3, 0x02: 2, 0x04: 0, 0x08: 1}[int(nes_dir)]
        a = self.symbol("players")
        self._w(a, int(x), 2); self._w(a + 2, int(y), 2)
        self._w(a + 6, int(nes_dir), 1); self._w(a + 7, face, 1); self._w(a + 13, 0, 1)
        self._w(self.symbol("s_link_dir"), {0x01: 4, 0x02: 3, 0x04: 1, 0x08: 2}[int(nes_dir)], 4)
        self._w(self.symbol("s_link_grid_offset"), 0, 1)

    def run_stage(self, body: str):
        from lupa import LuaRuntime
        lua = LuaRuntime(unpack_returned_tuples=True)
        fn = lua.eval("function(rd, wr, log, sys, gen_b_item, gen_link_pos)\n" + body + "\nend")
        fn(lambda a: self.nes8(int(a)), lambda a, v: self.write_nes(int(a), int(v)),
           lambda m: print("stage:", m), "GEN", self.gen_b_item, self.gen_link_pos)


def run_preset(name: str, rom: Path = ROOT / "builds" / "Debug.md") -> Session:
    import presets
    spec = json.loads((ROOT / "tools" / "lockstep" / "presets" / f"{name}.json").read_text())
    p = presets.build(spec)
    if p["gen_entry"] != "fs":
        raise SystemExit(f"{name}: gen_entry {p['gen_entry']} not supported headless")
    s = Session(rom)
    s.write_save(p["gen_slot0"])
    s.enter_fs()
    s.script(p["script"], p["clock"], p["stages"])
    return s


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("preset")
    ap.add_argument("--rom", type=Path, default=ROOT / "builds" / "Debug.md")
    ap.add_argument("--shot", type=Path)
    ap.add_argument("--state", type=Path, help="save a state after the script")
    a = ap.parse_args()
    s = run_preset(a.preset, a.rom)
    if a.shot:
        s.g.screenshot(a.shot)
    if a.state:
        a.state.write_bytes(s.g.save_state())
    print(f"{a.preset}: GameMode ${s.nes8(0x12):02X} room ${s.nes8(0xEB):02X} "
          f"level {s.nes8(0x10)} Link ({s.nes8(0x70):02X},{s.nes8(0x84):02X})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
