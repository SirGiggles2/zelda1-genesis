#!/usr/bin/env python3
"""Merge all per-run NES UW dumps under RoomRom/out/ into a single
aggregate JSON consumed by gen_uw_blob.py.

Output: RoomRom/out/nes_uw_aggregate.json
Schema:
{
  "version": 1,
  "entries": [
    {
      "rom": "orig"|"redux",
      "map_id": 0|1,
      "quest": 1|2,
      "level": L,
      "room_id": 0..127,
      "source_block": "LevelBlockUW1Q1"|...,
      "settle_frames": int,
      "nt":  [[..32..] x 30],
      "attr": [..64..],
      "palram": [..32..]
    },
    ...
  ]
}

Skips settle_timeout entries.
Idempotent.
"""

import json
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "RoomRom" / "data"
OUT = REPO_ROOT / "RoomRom" / "out"


def map_id_for(rom_id: str) -> int:
    return 0 if rom_id == "orig" else 1


def source_block_for(level: int, quest: int) -> str:
    if level <= 6:
        return f"LevelBlockUW1Q{quest}"
    return f"LevelBlockUW2Q{quest}"


def main() -> int:
    entries = []
    for level in range(1, 10):
        for quest in (1, 2):
            for rom_id in ("orig", "redux"):
                p = OUT / f"nes_uw_level{level}_quest{quest}_{rom_id}.json"
                if not p.exists():
                    continue
                try:
                    data = json.loads(p.read_text(encoding="utf-8"))
                except Exception as e:
                    print(f"WARN: skip {p.name}: parse error {e}", file=sys.stderr)
                    continue
                if not data.get("boot_ok") or not data.get("warp_ok"):
                    continue
                src_block = source_block_for(level, quest)
                for r in data.get("results") or []:
                    if r.get("code") == "settle_timeout":
                        continue
                    if "nt" not in r:
                        continue
                    entries.append({
                        "rom": rom_id,
                        "map_id": map_id_for(rom_id),
                        "quest": quest,
                        "level": level,
                        "room_id": r["room_id"],
                        "source_block": src_block,
                        "settle_frames": r.get("settle_frames"),
                        "nt": r["nt"],
                        "attr": r["attr"],
                        "palram": r["palram"],
                    })

    entries.sort(key=lambda e: (e["map_id"], e["quest"], e["level"], e["source_block"], e["room_id"]))
    out_path = OUT / "nes_uw_aggregate.json"
    out_path.write_text(
        json.dumps({"version": 1, "entries": entries}, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {out_path} ({len(entries)} entries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
