"""engine/data/item_chr_manifest.json from the user's ROMs.

The manifest's tile records embed 16-byte NES patterns captured from the
original and Redux ROMs. The package ships the template
(engine/data/item_chr_manifest.template.json): the same JSON with each
tile's "bytes" replaced, in place, by "bytes_sha256". The builder restores
"bytes" by looking each hash up among every 16-byte window of the variant's ROM
PRG, falling back to the original ROM (the Redux variant reuses original
patterns for some tiles). Output is byte-identical to the committed
manifest.

Usage:
    python tools/converter/item_chr_manifest.py --rom <zelda.nes> --redux <redux.nes>
    python tools/converter/item_chr_manifest.py --make-template   (developer)
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "engine" / "data" / "item_chr_manifest.json"
TEMPLATE = ROOT / "engine" / "data" / "item_chr_manifest.template.json"


def dump(obj) -> str:
    return json.dumps(obj, indent=2, ensure_ascii=True) + "\n"


def swap_key(d: dict, old: str, new: str, value) -> dict:
    return {(new if k == old else k): (value if k == old else v) for k, v in d.items()}


def make_template() -> int:
    m = json.loads(MANIFEST.read_text(encoding="utf-8"))
    for v in m["variants"]:
        v["tiles"] = {tid: swap_key(meta, "bytes", "bytes_sha256",
                                    hashlib.sha256(bytes.fromhex(meta["bytes"])).hexdigest())
                      for tid, meta in v["tiles"].items()}
    TEMPLATE.write_text(dump(m), encoding="utf-8", newline="\n")
    print(f"wrote {TEMPLATE.relative_to(ROOT)}")
    return 0


def window_index(ines: bytes) -> dict[str, bytes]:
    prg = ines[16:16 + ines[4] * 0x4000]
    idx = {}
    for i in range(len(prg) - 15):
        w = prg[i:i + 16]
        idx.setdefault(hashlib.sha256(w).hexdigest(), w)
    return idx


def fill(roms: dict[str, Path]) -> int:
    m = json.loads(TEMPLATE.read_text(encoding="utf-8"))
    missing = []
    indexes = {rid: window_index(p.read_bytes()) for rid, p in roms.items()}
    for v in m["variants"]:
        rom_id = v["rom_id"]
        if rom_id not in roms or "orig" not in roms:
            missing.append(f"variant {rom_id}: ROM not supplied")
            continue
        # The Redux variant keeps original patterns for 35 tiles (sources
        # prg_dat_authoritative / reference_aldonunez_*): look in the
        # variant's own ROM first, then the original.
        idx = dict(indexes["orig"])
        idx.update(indexes[rom_id])
        tiles = {}
        for tid, meta in v["tiles"].items():
            data = idx.get(meta["bytes_sha256"])
            if data is None:
                missing.append(f"{rom_id} tile {tid}: pattern not found in ROM")
                data = b""
            tiles[tid] = swap_key(meta, "bytes_sha256", "bytes", data.hex().upper())
        v["tiles"] = tiles
    if missing:
        for msg in missing:
            print(f"ERROR: {msg}", file=sys.stderr)
        return 1
    MANIFEST.write_text(dump(m), encoding="utf-8", newline="\n")
    print(f"wrote {MANIFEST.relative_to(ROOT)}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", type=Path)
    ap.add_argument("--redux", type=Path)
    ap.add_argument("--make-template", action="store_true")
    args = ap.parse_args()
    if args.make_template:
        return make_template()
    roms = {k: p for k, p in (("orig", args.rom), ("redux", args.redux)) if p}
    return fill(roms)


if __name__ == "__main__":
    sys.exit(main())
