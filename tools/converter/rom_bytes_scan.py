"""Scan a tree for embedded ROM bytes (release check for the public repo).

Raw: a run of WINDOW consecutive bytes found in a given ROM, as bytes in a
file. Any raw run fails (a ROM, CHR, data blob or binary capture).
Numeric: the same, with the numbers a source file writes out in order (C/asm
arrays: 0x12, $12, 18 ...). The reimplemented game code transcribes small
NES lookup tables (item, enemy, palette, sound tables); those are counted
and reported per file, and fail only with --strict. Runs of one or two
values (fill, zero tables) are ignored.

    python tools/converter/rom_bytes_scan.py <tree> --rom <zelda.nes> [--rom <redux.nes>] [--strict]
Exit: 0 clean; 1 raw ROM bytes found (or, with --strict, numeric runs).
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

WINDOW = 16
NUM = re.compile(rb"0[xX]([0-9A-Fa-f]{1,2})\b|\$([0-9A-Fa-f]{2})\b|(?<![\w.$])([0-9]{1,3})(?![\w.])")


def windows(rom: bytes) -> set[bytes]:
    return {rom[i:i + WINDOW] for i in range(len(rom) - WINDOW + 1)
            if len(set(rom[i:i + WINDOW])) > 2}


def numbers(data: bytes) -> bytes:
    out = bytearray()
    for m in NUM.finditer(data):
        h1, h2, d = m.groups()
        v = int(h1 or h2, 16) if (h1 or h2) else int(d)
        if v < 256:
            out.append(v)
    return bytes(out)


def covered(data: bytes, wins: set[bytes]) -> int:
    """Bytes of data inside a WINDOW-byte run found in the ROMs."""
    cov = set()
    for i in range(len(data) - WINDOW + 1):
        if data[i:i + WINDOW] in wins:
            cov.update(range(i, i + WINDOW))
    return len(cov)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("tree", type=Path)
    ap.add_argument("--rom", type=Path, action="append", required=True)
    ap.add_argument("--strict", action="store_true", help="numeric runs fail too")
    a = ap.parse_args()
    wins: set[bytes] = set()
    for r in a.rom:
        wins |= windows(r.read_bytes()[16:])
    raw_files, num_files = [], []
    n = num_total = 0
    for p in sorted(a.tree.rglob("*")):
        if not p.is_file() or ".git" in p.relative_to(a.tree).parts:
            continue
        rel = p.relative_to(a.tree).as_posix()
        if rel.startswith("sgdk/"):
            continue
        n += 1
        data = p.read_bytes()
        raw = covered(data, wins)
        num = covered(numbers(data), wins) if not raw else 0
        if raw:
            raw_files.append(f"{rel}: {raw} raw ROM bytes")
        elif num:
            num_files.append(f"{rel}: {num}")
            num_total += num
    for f in raw_files:
        print(f"  RAW {f}")
    for f in num_files:
        print(f"  table bytes {f}")
    fail = bool(raw_files) or (a.strict and bool(num_files))
    print(f"rom_bytes_scan: {'FAIL' if fail else 'PASS'} - {n} files; raw ROM bytes in "
          f"{len(raw_files)} files; transcribed table bytes {num_total} in {len(num_files)} "
          f"files ({WINDOW}-byte runs of {len(a.rom)} ROM(s))")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
