"""Generated C data files -> .nesdata pieces + Nintendo-free residue files.

Extractors keep writing ordinary C, and the many tools that parse those files
keep working: sources are never modified. convert_file() evaluates every
scalar array in a registered source (c_arrays.parse), writes its exact bytes
to build/nesdata/pieces/<symbol>.bin and writes the source's residue (extern
declarations, size constants, pointer tables) to build/nesdata/residue/<same
path>, with quoted includes made absolute so they still resolve. The Debug
build compiles the residue in place of the source.

    python tools/nesdata/sources.py                          convert every registered source
    python tools/nesdata/sources.py --register [--only P..]  rebuild pieces.json (developer)

T-419. Spec: docs/superpowers/specs/2026-10-31-T419-one-file-rom-maker-design.md
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.nesdata.c_arrays import Array, parse, residue, structs_in  # noqa: E402
from tools.nesdata.pieces import PIECE_DIR  # noqa: E402

REGISTRY = ROOT / "tools" / "nesdata" / "pieces.json"
INVENTORY = ROOT / "tools" / "nesdata" / "inventory.json"
RESIDUE_DIR = "build/nesdata/residue"
DEFINE = re.compile(r"^\s*#\s*define\s+(\w+)\s+\(?\s*(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*\s*\)?\s*$", re.M)
QUOTED_INCLUDE = re.compile(r'^(\s*#\s*include\s*)"([^"]+)"', re.M)


def macros_for(path: Path) -> dict[str, int]:
    """Integer #defines from headers beside the source (array dimensions)."""
    macros: dict[str, int] = {}
    for header in sorted(path.parent.glob("*.h")):
        for name, value in DEFINE.findall(header.read_text(encoding="utf-8", errors="replace")):
            macros[name] = int(value, 0)
    return macros


def structs_for(path: Path, text: str) -> dict:
    """Scalar-field struct layouts from headers beside the source and the source itself."""
    found: dict = {}
    for header in sorted(path.parent.glob("*.h")):
        found.update(structs_in(header.read_text(encoding="utf-8", errors="replace")))
    found.update(structs_in(text))
    return found


def residue_path(source: Path, root: Path) -> Path:
    return root / RESIDUE_DIR / source.resolve().relative_to(root.resolve())


def _absolute_includes(text: str, directory: Path) -> str:
    def fix(m: re.Match) -> str:
        target = directory / m.group(2)
        return f'{m.group(1)}"{target.resolve().as_posix()}"' if target.is_file() else m.group(0)
    return QUOTED_INCLUDE.sub(fix, text)


def _write_if_changed(out: Path, data: bytes) -> None:
    """Leave identical outputs untouched so incremental builds reuse their objects."""
    if out.is_file() and out.read_bytes() == data:
        return
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)


def convert_file(path: Path, root: Path) -> list[Array]:
    text = path.read_text(encoding="utf-8")
    parsed = parse(text, macros_for(path), structs_for(path, text))
    for a in parsed.arrays:
        _write_if_changed(root / PIECE_DIR / f"{a.name}.bin", a.data)
    res = _absolute_includes(residue(text, parsed), path.parent)
    _write_if_changed(residue_path(path, root), res.encode("utf-8"))
    return list(parsed.arrays)


def register(sources: list[Path], root: Path) -> list[dict]:
    entries: list[dict] = []
    seen: dict[str, str] = {}
    for src in sources:
        rel = src.resolve().relative_to(root.resolve()).as_posix()
        for a in convert_file(src, root):
            if a.name in seen:
                raise ValueError(f"piece symbol {a.name} defined in {seen[a.name]} and {rel}")
            seen[a.name] = rel
            entries.append({"name": a.name, "symbol": a.name, "source": rel,
                            "size": len(a.data), "align": a.align})
    return entries


def registered_sources() -> list[str]:
    if not REGISTRY.is_file():
        return []
    entries = json.loads(REGISTRY.read_text(encoding="utf-8"))["pieces"]
    return list(dict.fromkeys(e["source"] for e in entries))


def convert_all(root: Path = ROOT) -> int:
    count = 0
    for rel in registered_sources():
        count += len(convert_file(root / rel, root))
    return count


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--register", action="store_true")
    ap.add_argument("--only", nargs="*", default=[], help="register only sources under these prefixes")
    a = ap.parse_args()
    if a.register:
        files = json.loads(INVENTORY.read_text(encoding="utf-8"))["files"]
        chosen = [rel for rel in files if rel.endswith(".c") and (ROOT / rel).is_file()
                  and (not a.only or rel.startswith(tuple(a.only)))]
        kept = []
        if REGISTRY.is_file():
            kept = [e for e in json.loads(REGISTRY.read_text(encoding="utf-8"))["pieces"]
                    if e["source"] not in chosen]
        fresh = register([ROOT / rel for rel in chosen], ROOT)
        entries = kept + fresh
        names = [e["name"] for e in entries]
        dupes = sorted({n for n in names if names.count(n) > 1})
        if dupes:
            raise SystemExit(f"duplicate piece symbols: {dupes}")
        order = {e["name"]: i for i, e in enumerate(entries)}
        entries.sort(key=lambda e: (e["source"], order[e["name"]]))
        REGISTRY.write_text(json.dumps({"pieces": entries}, indent=1) + "\n", encoding="utf-8")
        print(f"REGISTER pieces={len(entries)} bytes={sum(e['size'] for e in entries)} "
              f"sources={len({e['source'] for e in entries})}")
        return 0
    print(f"NESDATA converted arrays={convert_all(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
