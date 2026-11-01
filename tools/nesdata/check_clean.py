"""Clean-ROM gate: no NES/Redux-derived content outside .nesdata.

Two checks on a finished Debug build:
1. raw: every 16-byte window of Zelda.md outside [__nesdata_start, __nesdata_end)
   that also occurs in either NES ROM (rom_bytes_scan.windows rules: runs of
   one or two values ignored) is a hit;
2. tables: every derived C source the build compiles (its .nesdata residue
   when registered, else the original) may not keep an initialised table of
   MIN_TABLE or more numeric literals. Converted data (e.g. 2bpp->4bpp CHR)
   never matches a raw NES window, so this catches what check 1 cannot.
Exceptions are listed by name in tools/nesdata/allowed_tables.json with a reason.

    python tools/nesdata/check_clean.py --rom <zelda.nes> --redux <redux.nes>
    -> CLEAN raw_hits=<n> tables=<n>   (exit 1 when either is non-zero)

T-419 Task 8.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools" / "converter"))
from rom_bytes_scan import WINDOW, windows  # noqa: E402
from tools.nesdata.c_arrays import _blank_comments  # noqa: E402
from tools.nesdata import sources  # noqa: E402

MIN_TABLE = 16
ALLOWED = ROOT / "tools" / "nesdata" / "allowed_tables.json"
INITIALISED = re.compile(r"^[^#\n;{}]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?:__attribute__\s*\(\([^)]*\)\)\s*)?=\s*\{",
                         re.M)
NUMBER = re.compile(r"(?<![\w.])-?(?:0[xX][0-9A-Fa-f]+|\d+)[uUlL]*(?![\w.])")


def range_from_nm(nm: str) -> tuple[int, int]:
    syms = {p[2]: int(p[0], 16) for p in (l.split() for l in nm.splitlines()) if len(p) == 3}
    start, end = syms["__nesdata_start"], syms["__nesdata_end"]
    return start, end - start


def nesdata_range(elf: Path) -> tuple[int, int]:
    from platform_tools import tool_command  # noqa: PLC0415
    nm = subprocess.run(tool_command("nm", ROOT) + [str(elf)], capture_output=True, text=True,
                        check=True).stdout
    return range_from_nm(nm)


def sized_symbols(nm_s: str, data_lma: int = 0) -> list[tuple[int, int, str]]:
    """`nm -S` -> sorted (ROM offset, size, name), LTO suffixes stripped. Unsized asm
    labels span to the next symbol; d/D RAM data maps onto its load image at data_lma."""
    out, unsized = [], []
    for line in nm_s.splitlines():
        p = line.split()
        if len(p) not in (3, 4):
            continue
        addr, kind, name = int(p[0], 16), p[-2], re.sub(r"\.lto_priv\.\d+$", "", p[-1])
        if kind in "tTrR" and addr < 0xE0000000:
            (out if len(p) == 4 else unsized).append((addr, int(p[1], 16) if len(p) == 4 else 0, name))
        elif kind in "dD" and len(p) == 4:
            out.append((data_lma + addr - 0xE0FF0000, int(p[1], 16), name))
    starts = sorted({a for a, _, _ in out + unsized})
    import bisect  # noqa: PLC0415
    for addr, _, name in unsized:
        i = bisect.bisect_right(starts, addr)
        out.append((addr, (starts[i] - addr) if i < len(starts) else 0, name))
    return sorted(out)


def owner(offset: int, syms: list[tuple[int, int, str]]) -> str | None:
    import bisect  # noqa: PLC0415
    i = bisect.bisect_right([s[0] for s in syms], offset) - 1
    if i >= 0 and syms[i][0] <= offset < syms[i][0] + syms[i][1]:
        return syms[i][2]
    return None


def outside_hits(rom: bytes, start: int, size: int, nes_roms: list[bytes]) -> list[tuple[int, int]]:
    wins: set[bytes] = set()
    for nes in nes_roms:
        wins |= windows(nes)
    end = start + size
    hits = []
    for i in range(len(rom) - WINDOW + 1):
        if i + WINDOW > start and i < end:
            continue
        if rom[i:i + WINDOW] in wins:
            hits.append((i, WINDOW))
    return merge_hits(hits)


def merge_hits(hits: list[tuple[int, int]]) -> list[tuple[int, int]]:
    merged: list[list[int]] = []
    for off, n in sorted(hits):
        if merged and off <= merged[-1][0] + merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], off + n - merged[-1][0])
        else:
            merged.append([off, n])
    return [(o, n) for o, n in merged]


def residue_tables(text: str) -> list[tuple[str, int]]:
    """(name, numeric literal count) for initialised tables with >= MIN_TABLE numbers."""
    clean = _blank_comments(text)
    found = []
    for m in INITIALISED.finditer(clean):
        depth, i = 0, m.end() - 1
        while i < len(clean):
            depth += {"{": 1, "}": -1}.get(clean[i], 0)
            if depth == 0:
                break
            i += 1
        count = len(NUMBER.findall(clean[m.end():i]))
        if count >= MIN_TABLE:
            found.append((m.group(1), count))
    return found


def compiled_sources() -> list[Path]:
    inventory = json.loads(sources.INVENTORY.read_text(encoding="utf-8"))["files"]
    registered = set(sources.registered_sources())
    out = []
    for rel in inventory:
        if not rel.endswith(".c") or not (ROOT / rel).is_file():
            continue
        out.append(sources.residue_path(ROOT / rel, ROOT) if rel in registered else ROOT / rel)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--redux", type=Path, required=True)
    ap.add_argument("--build", type=Path, default=ROOT / "builds" / "Zelda.md")
    ap.add_argument("--elf", type=Path, default=ROOT / "build" / "rom_project" / "out" / "Zelda.out")
    a = ap.parse_args()
    allowed = json.loads(ALLOWED.read_text(encoding="utf-8")) if ALLOWED.is_file() else {}
    start, size = nesdata_range(a.elf)
    nes = [a.rom.read_bytes()[16:], a.redux.read_bytes()[16:]]
    from platform_tools import tool_command  # noqa: PLC0415
    nm_s = subprocess.run(tool_command("nm", ROOT) + ["-S", str(a.elf)], capture_output=True,
                          text=True, check=True).stdout
    stext = next(int(l.split()[0], 16) for l in nm_s.splitlines() if l.split()[-1:] == ["_stext"])
    syms = sized_symbols(nm_s, data_lma=stext)
    hits = []
    for off, n in outside_hits(a.build.read_bytes(), start, size, nes):
        name = owner(off, syms)
        if name not in allowed:
            hits.append((off, n, name))
    print(f"nesdata range ${start:06X} size {size}")
    for off, n, name in hits[:80]:
        print(f"  raw: ${off:06X} {n} bytes in {name or '(no sized symbol)'}")
    tables = []
    for path in compiled_sources():
        for name, count in residue_tables(path.read_text(encoding="utf-8", errors="replace")):
            if name not in allowed:
                tables.append((path, name, count))
    for path, name, count in tables:
        print(f"  table: {name} ({count} numbers) in {path.relative_to(ROOT).as_posix()}")
    print(f"CLEAN raw_hits={len(hits)} tables={len(tables)}")
    return 1 if hits or tables else 0


if __name__ == "__main__":
    raise SystemExit(main())
