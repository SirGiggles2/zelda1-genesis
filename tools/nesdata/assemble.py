"""Rebuild the .nesdata section from a NES ROM + Redux ROM, in-process.

The player's browser cannot start processes, so every extraction step runs
inside this interpreter (runpy) with the same argv, environment and working
folder build.py gives it; steps reach native tools only through
tools/nesdata/host.py. The step list is build.plan_steps() (one source of
truth) minus audits, plus the two build-time generators build_rom.py runs
(Redux music, multiplayer poses), then the generated-C -> piece conversion.

    python tools/nesdata/assemble.py --rom <zelda.nes> --redux <redux.nes> --start <hex> --out <file>

T-419 Task 11. Spec: docs/superpowers/specs/2026-10-31-T419-one-file-rom-maker-design.md
"""
from __future__ import annotations

import argparse
import os
import runpy
import sys
import time
from pathlib import Path
from typing import Callable, Iterable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools" / "converter"))
from tools.nesdata.pieces import PIECES, Piece  # noqa: E402

# Audits and freshness bookkeeping: they check the developer tree and write no piece.
SKIPPED = {"generated asset catalog", "record generated freshness", "verify generated freshness",
           "check_uw_room_coverage.py"}
# Generators build_rom.py runs before compiling (their output is registered).
BUILD_TIME = (("build_redux_music.py", "tools/audio/build_redux_music.py", ()),
              ("mp_link_poses.py", "tools/converter/mp_link_poses.py", ()))


class StepError(RuntimeError):
    def __init__(self, label: str, code):
        super().__init__(f"step {label} failed (exit {code})")
        self.label, self.code = label, code


def steps(rom: Path, redux: Path) -> list[tuple[str, str, tuple[str, ...]]]:
    """(label, script relative to ROOT, args) in pipeline order."""
    import build  # noqa: PLC0415
    out = []
    for label, argv in build.plan_steps(rom, redux):
        if label in SKIPPED:
            continue
        script = Path(argv[1]).resolve().relative_to(ROOT.resolve()).as_posix()
        out.append((label, script, tuple(str(a) for a in argv[2:])))
    return out + [(label, script, args) for label, script, args in BUILD_TIME]


class _NoProcess(Exception):
    pass


def run_step(label: str, script: str, args: Iterable[str], root: Path) -> None:
    """Run one step as `python <script> <args>` would, in this interpreter. Like the
    browser, a step may not start processes except through tools/nesdata/host.py."""
    import subprocess  # noqa: PLC0415
    from tools.nesdata import host  # noqa: PLC0415
    real_popen = subprocess.Popen

    class GuardedPopen(real_popen):  # type: ignore[misc, valid-type]
        def __init__(self, *a, **k):
            if not host.processes_allowed():
                raise _NoProcess(f"{label} tried to start a process: {a[0] if a else k.get('args')}")
            super().__init__(*a, **k)

    saved_argv, saved_cwd = sys.argv, os.getcwd()
    modules_before = set(sys.modules)
    sys.argv = [str(root / script), *args]
    # `python script.py` puts the script's folder first on sys.path; do the same.
    script_dir = str((root / script).parent)
    sys.path.insert(0, script_dir)
    os.chdir(root)
    subprocess.Popen = GuardedPopen
    try:
        runpy.run_path(str(root / script), run_name="__main__")
    except SystemExit as exc:
        if exc.code not in (None, 0):
            raise StepError(label, exc.code) from None
    except _NoProcess as exc:
        raise StepError(label, str(exc)) from None
    finally:
        subprocess.Popen = real_popen
        if script_dir in sys.path:
            sys.path.remove(script_dir)
        # A fresh interpreter per step would not keep this step's project modules.
        for name in [n for n in sys.modules if n not in modules_before]:
            path = getattr(sys.modules[name], "__file__", None) or ""
            if path and Path(path).resolve().is_relative_to(root.resolve()) and not name.startswith("tools.nesdata"):
                del sys.modules[name]
        sys.argv = saved_argv
        os.chdir(saved_cwd)


def section(pieces: Iterable[Piece], root: Path, start: int) -> bytes:
    """The .nesdata bytes exactly as debug.ld lays them out from `start` (= __nesdata_start):
    the input section aligned to its largest piece alignment, each piece .balign'ed,
    end rounded up to 4."""
    pieces = list(pieces)
    up = lambda v, a: (v + a - 1) // a * a  # noqa: E731
    pos = up(start, max(p.align for p in pieces))
    out = bytearray(pos - start)
    for p in pieces:
        aligned = up(pos, p.align)
        out += bytes(aligned - pos)
        data = (root / p.path).read_bytes()
        if len(data) != p.size:
            raise ValueError(f"nesdata piece {p.name}: {len(data)} bytes, registry says {p.size}")
        out += data
        pos = aligned + len(data)
    out += bytes(up(pos, 4) - pos)
    return bytes(out)


def assemble(root: Path, rom: Path, redux: Path, start: int,
             progress: Callable[[int, int, str], None] = lambda i, n, label: None) -> bytes:
    from tools.nesdata import sources  # noqa: PLC0415
    os.environ["ZELDA_NES_ROM"] = str(rom)
    os.environ["ZELDA_REDUX_ROM"] = str(redux)
    plan = steps(rom, redux)
    total = len(plan) + 1
    for i, (label, script, args) in enumerate(plan, 1):
        progress(i, total, label)
        run_step(label, script, args, root)
    progress(total, total, "nesdata pieces")
    sources.convert_all(root)
    return section(PIECES, root, start)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--redux", type=Path, required=True)
    ap.add_argument("--start", type=lambda s: int(s, 16), required=True, help="__nesdata_start (hex)")
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    times: list[tuple[str, float]] = []
    t0 = last = time.time()

    def report(i: int, n: int, label: str) -> None:
        nonlocal last
        now = time.time()
        if times or i > 1:
            times[-1] = (times[-1][0], now - last)
        times.append((label, 0.0))
        last = now
        print(f"@@step {i}/{n} {label}", flush=True)

    data = assemble(ROOT, a.rom.resolve(), a.redux.resolve(), a.start, report)
    times[-1] = (times[-1][0], time.time() - last)
    a.out.write_bytes(data)
    print(f"NESDATA {len(data)} bytes in {time.time() - t0:.1f} s")
    for label, seconds in sorted(times, key=lambda t: -t[1])[:8]:
        print(f"  {seconds:6.1f} s  {label}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
