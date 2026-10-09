"""Gameplay-entry check for a converted Genesis ROM (P10.4), headless.

Runs the lockstep "newgame" preset (staged new-game save card "LINK", no
items -> File Select -> gameplay, then walk left and up) on the user's NES
ROM (tools/nesemu) and on the converted Genesis ROM (tools/emu/gpgx.py, Genesis Plus GX core from
tools/emu/build_gpgx.sh), exactly as tools/emu/scenario.py drives both.
PASS = the Genesis ROM reaches a play mode and the game-state cells below
equal the NES at the end of the script.

    python tools/converter/boot_check.py --rom <zelda.nes> --genesis <built.md>
Exit: 0 PASS; 1 a cell differs or gameplay was not reached; 2 no emulator core.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "emu"))
sys.path.insert(0, str(ROOT / "tools" / "lockstep"))

PRESET = "newgame"
# NES RAM cells (Z_00 names): mode, level/room, Link object slot 0.
CELLS = (("GameMode", 0x12), ("Submode", 0x13), ("CurLevel", 0x10), ("RoomId", 0xEB),
         ("LinkX", 0x70), ("LinkY", 0x84), ("LinkDir", 0x98))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", type=Path, required=True, help="user's NES Zelda ROM")
    ap.add_argument("--genesis", type=Path, required=True, help="converted Genesis ROM")
    ap.add_argument("--report", type=Path, help="write the cell table as JSON")
    a = ap.parse_args()
    import gpgx  # noqa: PLC0415
    if not gpgx.CORE.exists():
        print(f"ERROR: emulator core missing: {gpgx.CORE} (run tools/emu/build_gpgx.sh)",
              file=sys.stderr)
        return 2
    import presets  # noqa: PLC0415
    from nes_preset_run import NesSession  # noqa: PLC0415
    from preset_run import PLAY_MODES, Session  # noqa: PLC0415
    from scenario import drive  # noqa: PLC0415

    spec = json.loads((ROOT / "tools" / "lockstep" / "presets" / f"{PRESET}.json").read_text())
    p = presets.build(spec)
    nes = NesSession(a.rom)
    seed = drive(nes, p, {})
    try:
        gen = Session(a.genesis)
    except OSError as exc:
        # A Linux core may coexist with Windows builds in a shared checkout.
        # An incompatible library is unavailable, not a gameplay failure.
        print(f"ERROR: emulator core unavailable on this host: {exc}", file=sys.stderr)
        return 2
    drive(gen, p, {}, seed)

    rows = {name: (nes.nes8(addr), gen.nes8(addr)) for name, addr in CELLS}
    bad = [n for n, (x, y) in rows.items() if x != y]
    in_play = rows["GameMode"][1] in PLAY_MODES
    for n, (x, y) in rows.items():
        print(f"  {n:<9} NES ${x:02X}  GEN ${y:02X}{'' if x == y else '  DIFF'}")
    if a.report:
        a.report.write_text(json.dumps({"preset": PRESET, "cells": rows,
                                        "pass": in_play and not bad}, indent=1) + "\n")
    if not in_play or bad:
        print(f"boot_check: FAIL ({'not in play mode' if not in_play else 'cells differ: ' + ', '.join(bad)})")
        return 1
    print(f"boot_check: PASS - preset {PRESET}: gameplay reached, {len(rows)}/{len(rows)} "
          "cells equal to the NES")
    return 0


if __name__ == "__main__":
    sys.exit(main())
