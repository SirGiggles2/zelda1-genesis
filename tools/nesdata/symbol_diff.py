"""Compare the bytes of the .nesdata piece symbols between two Debug builds.

Proves a .nesdata move changed where data lives, not what it is (RULE RF-1).
md.ld places .rodata inside .text, so nm reports LTO data tables as t/T like
code; every sized ROM symbol (t/T/r/R) is read at its address, initialised
RAM data (d/D) from its load image at the end of .text (_stext). Only the
selected symbols are compared: code that references a moved table changes
its embedded addresses by design (behaviour is proven by the A/B diff).
LTO-private suffixes (.lto_priv.N) are stripped so a moved static keeps its name.

    python tools/nesdata/symbol_diff.py ROM_A ELF_A ROM_B ELF_B [--symbols a,b]
    -> SYMDIFF changed=<n> missing=<n> added=<n>   (exit 1 on any changed or missing)
    Default symbols: every Piece.symbol in tools/nesdata/pieces.py.

T-419 Task 2.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "converter"))
sys.path.insert(0, str(ROOT))
RAM_BASE = 0xE0FF0000
LTO_SUFFIX = re.compile(r"\.lto_priv\.\d+$")


def parse_nm(text: str, data_lma: int) -> dict[str, tuple[int, int]]:
    """`nm -S` lines -> {name: (rom offset, size)} for ROM-resident data."""
    syms: dict[str, tuple[int, int]] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        addr, size, kind, name = int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]
        if kind in "rRtT":
            offset = addr
        elif kind in "dD":
            offset = data_lma + (addr - RAM_BASE)
        else:
            continue
        syms[LTO_SUFFIX.sub("", name)] = (offset, size)
    return syms


def select(syms: dict[str, bytes], names: list[str]) -> dict[str, bytes]:
    return {n: syms[n] for n in names if n in syms}


def locate_added(added: list[str], new: dict[str, bytes], old_image: bytes) -> dict[str, bool]:
    """Symbols the old build did not name: are their bytes anywhere in the old ROM?
    True = LTO hid the name; False = the old link dropped it or folded it into code."""
    return {name: old_image.find(new[name]) >= 0 for name in added}


def compare(a: dict[str, bytes], b: dict[str, bytes]) -> tuple[list[str], list[str], list[str]]:
    changed = sorted(n for n in a.keys() & b.keys() if a[n] != b[n])
    return changed, sorted(a.keys() - b.keys()), sorted(b.keys() - a.keys())


def contents(rom: Path, elf: Path) -> dict[str, bytes]:
    from platform_tools import tool_command  # noqa: PLC0415
    nm = subprocess.run(tool_command("nm", ROOT) + ["-S", str(elf)], capture_output=True,
                        text=True, check=True).stdout
    stext = next(int(l.split()[0], 16) for l in nm.splitlines() if l.split()[-1:] == ["_stext"])
    image = rom.read_bytes()
    return {name: image[off:off + size] for name, (off, size) in parse_nm(nm, stext).items()}


def main() -> int:
    import argparse  # noqa: PLC0415
    from tools.nesdata.pieces import PIECES  # noqa: PLC0415
    ap = argparse.ArgumentParser(description="compare .nesdata piece symbol bytes")
    ap.add_argument("files", nargs=4, type=Path, metavar="ROM_A ELF_A ROM_B ELF_B")
    ap.add_argument("--symbols", help="comma list; default every piece symbol")
    a = ap.parse_args()
    rom_a, elf_a, rom_b, elf_b = a.files
    names = a.symbols.split(",") if a.symbols else [p.symbol for p in PIECES]
    old, new = select(contents(rom_a, elf_a), names), select(contents(rom_b, elf_b), names)
    changed, missing, added = compare(old, new)
    absent = sorted(set(names) - old.keys() - new.keys())
    print(f"SYMDIFF changed={len(changed)} missing={len(missing)} added={len(added)} "
          f"compared={len(old.keys() & new.keys())}")
    found = locate_added(added, new, rom_a.read_bytes())
    for label, items in (("changed", changed), ("missing", missing), ("in neither build", absent)):
        for name in items[:40]:
            print(f"  {label}: {name}")
    for name in added:
        where = "bytes in old ROM" if found[name] else "NOT in old ROM (dropped/folded: check references)"
        print(f"  added: {name} ({len(new[name])} B) {where}")
    return 1 if changed or missing or absent else 0


if __name__ == "__main__":
    raise SystemExit(main())
