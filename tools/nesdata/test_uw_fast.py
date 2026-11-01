"""Fast dungeon dump (JS NES) (T-419 Task 10).

1. Emulator fidelity: sequential mode == tools/nesemu/zelda_uw_dump.dump_level() for
   every level/quest (each run against an empty output folder), byte-for-byte.
2. NES truth: clean mode (the build default) == Mesen CE AI clean captures for every
   room (tools/nesdata/probes/mesen_uw_clean.py output in build/mesen_uw_clean;
   private ROM-derived evidence, so the test skips when it is absent).
3. Speed: clean mode, both ROMs, <= 10 s.
"""
from __future__ import annotations

import json
import shutil
import sys
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))
from tools.nesdata import uw_fast  # noqa: E402

ROMS = {"orig": ROOT / "roms" / "Legend of Zelda, The (USA).nes", "redux": ROOT / "roms" / "Zelda Redux.nes"}
MESEN = ROOT / "build" / "mesen_uw_clean"
MIRRORS = {16, 20, 24, 28}  # PALRAM mirror cells: nes.py stores them BizHawk-style, Mesen mirrors them
pytestmark = pytest.mark.skipif(shutil.which("node") is None or not all(p.is_file() for p in ROMS.values()),
                                reason="needs Node and both local ROMs")


def _oracle(job):
    rom_path, rom_id, level, quest = job
    from nesemu.zelda_uw_dump import dump_level  # noqa: PLC0415
    with tempfile.TemporaryDirectory() as empty:
        res = dump_level(Path(rom_path).read_bytes(), rom_id, level, quest, Path(empty))
    return f"nes_uw_level{level}_quest{quest}_{rom_id}.json", json.dumps(res) + "\n"


def _screen(cap):
    return ([v for row in cap["nt"] for v in row], cap["attr"],
            [v for i, v in enumerate(cap["palram"]) if i not in MIRRORS])


@pytest.fixture(scope="module")
def clean_files():
    t = time.time()
    files = {}
    for rom_id, rom in ROMS.items():
        files.update(uw_fast.dump(rom.read_bytes(), rom_id))
    return files, time.time() - t


@pytest.mark.parametrize("rom_id", list(ROMS))
def test_sequential_matches_python_emulator(rom_id):
    files = uw_fast.dump(ROMS[rom_id].read_bytes(), rom_id, clean=False)
    jobs = [(str(ROMS[rom_id]), rom_id, lv, q) for lv in range(1, 10) for q in (1, 2)]
    with ProcessPoolExecutor() as ex:
        for name, text in ex.map(_oracle, jobs):
            assert files[name] == text, f"{name} differs"


@pytest.mark.skipif(not MESEN.is_dir(), reason="no Mesen clean captures (run tools/nesdata/probes/mesen_uw_clean.py)")
@pytest.mark.parametrize("rom_id", list(ROMS))
def test_clean_matches_mesen(clean_files, rom_id):
    files, _ = clean_files
    compared = 0
    for level in range(1, 10):
        for quest in (1, 2):
            mesen = json.loads((MESEN / f"mesen_uw_clean_{rom_id}_L{level}Q{quest}.json").read_text())
            ours = {r["room_id"]: r for r in json.loads(files[f"nes_uw_level{level}_quest{quest}_{rom_id}.json"])["results"]}
            for rid, cap in mesen["rooms"].items():
                assert "nt" in cap and "nt" in ours[int(rid, 16)], f"L{level}Q{quest} ${rid} not captured"
                assert _screen(ours[int(rid, 16)]) == _screen(cap), f"L{level}Q{quest} room ${rid} differs from Mesen"
                compared += 1
    assert compared >= 459


def test_clean_time(clean_files):
    _, seconds = clean_files
    assert seconds <= 10.0, f"both ROMs took {seconds:.1f} s"
