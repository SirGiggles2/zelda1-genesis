"""Locate shipped-by-hash data blobs in the user's ROMs.

Some build inputs are byte strings stored verbatim in a ROM's PRG (Redux
overworld layouts, column heaps, CHR blocks) that used to come from a
third-party source tree. The package ships only a template of
(name, rom, length, sha256); the builder finds each blob by hashing every
window of that length in the matching ROM's PRG and writes it to
<out-dir>/<name>.

Usage:
    python tools/builder/rom_blobs.py --template RoomRom/data/redux_sources.template.json \\
        --redux <Zelda Redux.nes> --out-dir RoomRom/out/redux_src
    python tools/builder/rom_blobs.py --make-redux-template      (developer)
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REDUX_TEMPLATE = ROOT / "RoomRom" / "data" / "redux_sources.template.json"


def prg_of(ines: bytes) -> bytes:
    if ines[:4] != b"NES\x1a":
        raise ValueError("not an iNES image")
    return ines[16:16 + ines[4] * 0x4000]


def locate(prg: bytes, length: int, sha: str) -> bytes | None:
    for i in range(len(prg) - length + 1):
        w = prg[i:i + length]
        if hashlib.sha256(w).hexdigest() == sha:
            return w
    return None


def extract(template: list[dict], roms: dict[str, Path], out_dir: Path) -> int:
    out_dir.mkdir(parents=True, exist_ok=True)
    prgs = {}
    errors = []
    for entry in template:
        rom = entry["rom"]
        if rom not in roms:
            errors.append(f"{entry['name']}: {rom} ROM not supplied")
            continue
        if rom not in prgs:
            prgs[rom] = prg_of(roms[rom].read_bytes())
        data = b""
        for part in entry.get("parts", [entry]):
            found = locate(prgs[rom], part["length"], part["sha256"])
            if found is None:
                errors.append(f"{entry['name']}: {part['length']}-byte blob not found in {rom} ROM")
                break
            data += found
        else:
            (out_dir / entry["name"]).write_bytes(data)
    for e in errors:
        print(f"ERROR: {e}", file=sys.stderr)
    if not errors:
        print(f"wrote {len(template)} blobs to {out_dir}")
    return 1 if errors else 0


# ---- developer: derive the Redux template from the committed outputs ----

def _c_arrays(path: Path) -> dict[str, list[int]]:
    text = path.read_text(encoding="ascii")
    out = {}
    for m in re.finditer(r"const unsigned (char|short) (\w+)\[\d+\] = \{(.*?)\};", text, re.S):
        out[m.group(2)] = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", m.group(3))]
    return out


def genesis_to_nes(gen: list[int]) -> bytes:
    """Inverse of gen_redux_roomrom.nes_tile_to_genesis (2-bit colours)."""
    out = bytearray()
    for t in range(0, len(gen), 32):
        p0 = bytearray(8)
        p1 = bytearray(8)
        for row in range(8):
            for px in range(8):
                b = gen[t + row * 4 + px // 2]
                c = (b & 0x0F) if px & 1 else (b >> 4)
                if c > 3:
                    raise ValueError("colour index > 3 cannot come from NES 2bpp")
                p0[row] |= (c & 1) << (7 - px)
                p1[row] |= ((c >> 1) & 1) << (7 - px)
        out += p0 + p1
    return bytes(out)


def make_redux_template() -> int:
    src = ROOT / "RoomRom" / "src"
    ow = _c_arrays(src / "redux_overworld.c")
    bg = _c_arrays(src / "redux_overworld_bg.c")
    hud = _c_arrays(src / "redux_hud_chr.c")
    blob = bytes(ow["rooms_overworld_redux"])
    offsets = ow["rooms_overworld_redux_heap_offsets"]
    layouts_off, heap_off = 1166, 3150          # gen_redux_roomrom OW_*_OFFSET
    heap = blob[heap_off:]
    blobs = [("overworld_column_data.bin", blob[layouts_off:heap_off])]
    for i, start in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < len(offsets) else len(heap)
        blobs.append((f"column_heap_{i:02d}.bin", heap[start:end]))
    # Heap 15 as gen_redux_roomrom parsed it from the Redux source: the
    # $F0-$FF column block (113 bytes at org $AF80, then $FF fill) followed
    # by tile_definitions (org $A97C), because the parser's end marker
    # ("// Dungeon screens") only appears after that table. Two located parts.
    HEAP15_BLOCK = 113
    blobs[-1] = (blobs[-1][0], (blobs[-1][1][:HEAP15_BLOCK], blobs[-1][1][HEAP15_BLOCK:]))
    blobs += [
        ("data_02b.bin", genesis_to_nes(bg["redux_overworld_bg_chr"])),
        ("OverworldAssets.bin", genesis_to_nes(bg["redux_overworld_secret_chr"])),
        ("automap_tiles.bin", genesis_to_nes(hud["redux_automap_chr"])),
    ]
    def rec(d):
        return {"length": len(d), "sha256": hashlib.sha256(d).hexdigest()}
    template = []
    for n, d in blobs:
        if isinstance(d, tuple):
            template.append({"name": n, "rom": "redux", "parts": [rec(x) for x in d]})
        else:
            template.append({"name": n, "rom": "redux", **rec(d)})
    REDUX_TEMPLATE.write_text(json.dumps(template, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {REDUX_TEMPLATE.relative_to(ROOT)} ({len(template)} blobs)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", type=Path, default=REDUX_TEMPLATE)
    ap.add_argument("--rom", type=Path)
    ap.add_argument("--redux", type=Path)
    ap.add_argument("--out-dir", type=Path, default=ROOT / "RoomRom" / "out" / "redux_src")
    ap.add_argument("--make-redux-template", action="store_true")
    args = ap.parse_args()
    if args.make_redux_template:
        return make_redux_template()
    roms = {k: p for k, p in (("orig", args.rom), ("redux", args.redux)) if p}
    return extract(json.loads(args.template.read_text(encoding="utf-8")), roms, args.out_dir)


if __name__ == "__main__":
    sys.exit(main())
