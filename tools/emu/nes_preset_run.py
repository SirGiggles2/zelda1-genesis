"""Run a lockstep preset on the user's NES ROM headlessly (tools/nesemu).

NES side of tools/lockstep/capture.lua, the counterpart of preset_run.py:
the preset's file A image is written into WRAM before frame 1, then
120 idle / Start x6 / 120 idle / Start x6, sync on GameMode $05 with
RoomId set and FrameCounter advancing, then the script with its stages.

    python tools/emu/nes_preset_run.py ROM.nes t002_q2_l1

As a module: NesSession(rom) has the Session interface preset_run.py
uses (nes8, nes, write_nes, press, enter_fs, script, run_stage), so a
scenario runs unchanged on both consoles.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "nesemu"))
sys.path.insert(0, str(ROOT / "tools" / "lockstep"))
from nes import Nes  # noqa: E402

PLAY_MODES = (0x05, 0x09, 0x0B)
KEYS = {"U": "Up", "D": "Down", "L": "Left", "R": "Right", "A": "A", "B": "B",
        "S": "Start", "s": "Select"}


class NesSession:
    def __init__(self, rom: Path):
        self.n = Nes(Path(rom).read_bytes())

    def _cell(self, addr: int):
        """Work RAM ($0000-$07FF) or cart WRAM ($6000-$7FFF), the two
        ranges a stage addresses (the Genesis mirrors both at $FF8000+)."""
        if 0 <= addr < 0x800:
            return self.n.ram, addr
        if 0x6000 <= addr < 0x8000:
            return self.n.wram, addr - 0x6000
        raise ValueError(f"NES address ${addr:04X} is not RAM/WRAM")

    def nes(self, addr: int, n: int = 1) -> bytes:
        mem, off = self._cell(addr)
        return bytes(mem[off:off + n])

    def nes8(self, addr: int) -> int:
        mem, off = self._cell(addr)
        return mem[off]

    def write_nes(self, addr: int, value: int):
        mem, off = self._cell(addr)
        mem[off] = value & 0xFF

    def write_save(self, wram: dict[int, int]):
        for a, b in wram.items():
            self.n.wram[a - 0x6000] = b

    def press(self, keys: str, frames: int = 1):
        self.n.set_buttons(*[KEYS[c] for c in keys])
        self.n.run_frames(frames)
        self.n.set_buttons()

    def enter_fs(self):
        self.press("", 1)                               # capture.lua ATTACH_FRAMES
        self.press("", 120); self.press("S", 6); self.press("", 120); self.press("S", 6)
        for _ in range(1500):
            if self.nes8(0x12) == 0x05 and self.nes8(0xEB) != 0:
                break
            self.press("")
        else:
            raise RuntimeError(f"GameMode never reached $05 (last ${self.nes8(0x12):02X})")
        for _ in range(300):
            before = self.nes8(0x15)
            self.press("")
            if self.nes8(0x15) != before:
                return
        raise RuntimeError("FrameCounter never advanced after GameMode $05")

    def script(self, steps, clock: str = "tick", stages=(), cap: int | None = None,
               on_step=None):
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

    def run_stage(self, body: str):
        from lupa import LuaRuntime
        lua = LuaRuntime(unpack_returned_tuples=True)
        fn = lua.eval("function(rd, wr, log, sys, gen_b_item, gen_link_pos)\n" + body + "\nend")
        fn(lambda a: self.nes8(int(a)), lambda a, v: self.write_nes(int(a), int(v)),
           lambda m: print("stage:", m), "NES", lambda v: None, lambda x, y, d: None)

    # BizHawk NesHawk domain equivalents.
    def oam(self) -> bytes:
        return bytes(self.n.oam)

    def chr(self) -> bytes:
        return bytes(self.n.chr)

    def palram(self) -> bytes:
        return bytes(self.n.palram)


def run_preset(name: str, rom: Path) -> NesSession:
    import presets
    spec = json.loads((ROOT / "tools" / "lockstep" / "presets" / f"{name}.json").read_text())
    p = presets.build(spec)
    s = NesSession(rom)
    s.write_save(p["nes_wram"])
    s.enter_fs()
    s.script(p["script"], p["clock"], p["stages"])
    return s


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("preset")
    a = ap.parse_args()
    s = run_preset(a.preset, a.rom)
    print(f"{a.preset}: GameMode ${s.nes8(0x12):02X} room ${s.nes8(0xEB):02X} "
          f"level {s.nes8(0x10)} Link ({s.nes8(0x70):02X},{s.nes8(0x84):02X})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
