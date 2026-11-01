"""Lockstep: the builder page's JS NES (nesemu.js) vs tools/nesemu/nes.py (T-419 Task 10).

Same ROM, same scripted buttons; the full machine state digest must match after
every frame. Any divergence names the first frame that differs.
"""
from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from nesemu.nes import Nes  # noqa: E402

PAGE = ROOT / "tools" / "release" / "builder_page"
ROMS = [ROOT / "roms" / "Legend of Zelda, The (USA).nes", ROOT / "roms" / "Zelda Redux.nes"]
FRAMES = 2000


def scripted_buttons(frame: int) -> tuple[str, ...]:
    """Mirror of nes_trace.mjs scriptedButtons()."""
    phase = frame % 97
    if frame < 1200:
        if phase < 2:
            return ("Start",)
        if 40 <= phase < 41:
            return ("A",)
        if phase == 70:
            return ("Select",)
        if phase == 80:
            return ("Down",)
        return ()
    return (("Up",), ("Left",), ("Down",), ("Right",), ("A",), ("B",))[(frame >> 5) % 6]


def digest(nes: Nes) -> str:
    c, h = nes.cpu, hashlib.sha256()
    for a in (nes.ram, nes.wram, nes.chr, nes.ciram, nes.palram, nes.oam):
        h.update(bytes(a))
    h.update(f"{c.pc},{c.a},{c.x},{c.y},{c.sp},{c.p},{c.processorCycles},{nes.v},{nes.t},{nes.w},"
             f"{nes.ppu_ctrl},{nes.mmc1_control},{nes.mmc1_prg}".encode())
    return h.hexdigest()


@pytest.mark.skipif(shutil.which("node") is None, reason="needs Node")
@pytest.mark.parametrize("rom", [r for r in ROMS if r.is_file()], ids=lambda p: p.stem)
def test_js_nes_matches_python_every_frame(rom):
    js = subprocess.run(["node", str(PAGE / "nes_trace.mjs"), str(rom), str(FRAMES)], capture_output=True,
                        text=True, check=True, stdin=subprocess.DEVNULL).stdout.split()
    nes = Nes(rom.read_bytes())
    for frame in range(FRAMES):
        nes.set_buttons(*scripted_buttons(frame))
        nes.run_frame()
        assert js[frame] == digest(nes), f"first divergence at frame {frame}"
