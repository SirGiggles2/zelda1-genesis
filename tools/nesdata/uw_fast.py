"""Fast dungeon room dump: same files as tools/nesemu/zelda_uw_dump.py.

Clean mode: every room is captured from the post-warp state, so no room inherits
door state from the previous capture; it matches Mesen CE AI captures for all 918
rooms (tools/nesdata/probes/mesen_uw_clean.py). Runs tools/release/builder_page/uw_dump.js (the JS NES) through
tools/nesdata/host.py: under Node natively, directly in the builder page.
The Python dump stays the oracle (tools/nesdata/test_uw_fast.py).

    python tools/nesdata/uw_fast.py --rom <rom.nes> [--rom-id orig|redux] [--out-dir engine/out]

T-419 Task 10.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.nesdata import host  # noqa: E402


def manifests(root: Path = ROOT) -> dict[str, list[str]]:
    return {f"{lv}_{q}": json.loads((root / "engine" / "data" / f"uw_level{lv}_quest{q}_rooms.json")
                                    .read_text(encoding="utf-8"))["rooms"]
            for lv in range(1, 10) for q in (1, 2)}


def dump(rom: bytes, rom_id: str, clean: bool = True) -> dict[str, str]:
    """{filename: JSON text} for levels 1-9, quests 1-2 (clean: Mesen-verified capture mode)."""
    return host.uw_dump(rom, rom_id, hashlib.sha256(rom).hexdigest(), manifests(), clean)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--rom-id", default="orig", choices=("orig", "redux"))
    ap.add_argument("--out-dir", type=Path, default=ROOT / "engine" / "out")
    a = ap.parse_args()
    a.out_dir.mkdir(parents=True, exist_ok=True)
    rc = 0
    for name, text in dump(a.rom.read_bytes(), a.rom_id).items():
        (a.out_dir / name).write_text(text, encoding="utf-8")
        res = json.loads(text)
        print(f"L{res['level']}Q{res['quest']} {a.rom_id}: boot={res['boot_ok']} warp={res['warp_ok']} "
              f"captured={res.get('rooms_captured', 0)}/{res['rooms_requested']}")
        if res.get("fatal_error") or res.get("rooms_failed"):
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
