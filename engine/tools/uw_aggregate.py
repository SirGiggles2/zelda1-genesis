#!/usr/bin/env python3
"""Merge all per-run NES UW dumps under engine/out/ into a single
aggregate JSON consumed by gen_uw_blob.py.

Output: engine/out/nes_uw_aggregate.json
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

import argparse
import json
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "engine" / "data"
OUT = REPO_ROOT / "engine" / "out"


def map_id_for(rom_id: str) -> int:
    return 0 if rom_id == "orig" else 1


def source_block_for(level: int, quest: int) -> str:
    if level <= 6:
        return f"LevelBlockUW1Q{quest}"
    return f"LevelBlockUW2Q{quest}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--preserve-blob", type=Path,
                    help="retain verified payloads absent from historical per-run caches; reject capture drift")
    args = ap.parse_args()
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

    if args.preserve_blob:
        # Some authenticated legacy captures exist only in the checked-in
        # blob. Regenerating from sparse local caches must not drop them.
        text = args.preserve_blob.read_text(encoding="utf-8")
        arrays = {}
        for name, width in (("index", 4), ("nt", 704), ("attr", 64), ("palette", 32)):
            body = text.split("g_uw_room_" + name + "[", 1)[1].split("= {", 1)[1].split("};", 1)[0]
            body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
            nums = [int(v.rstrip("u"), 0) for v in re.findall(r"0x[\da-fA-F]+u?|\b\d+u?", body)]
            if len(nums) % width:
                raise ValueError(f"incomplete preserved {name} array")
            arrays[name] = [nums[i:i+width] for i in range(0, len(nums), width)]
        if any(len(arrays[k]) != len(arrays["index"]) for k in ("nt", "attr", "palette")):
            raise ValueError("preserved blob payload count mismatch")
        captured = {(e["map_id"], e["quest"], e["level"], e["room_id"]): e for e in entries}
        for i, key in enumerate(arrays["index"]):
            mid, quest, level, room = key
            previous = bytes(arrays["nt"][i] + arrays["attr"][i] + arrays["palette"][i])
            if tuple(key) in captured:
                e = captured[tuple(key)]
                current = bytes(v for row in e["nt"][8:] for v in row) + bytes(e["attr"] + e["palram"])
                # NES $3F10/$14/$18/$1C mirror $3F00/$04/$08/$0C.
                # Older offline dumps expose unused raw backing slots;
                # live PPU reads expose their mirrors. Authenticate all
                # meaningful bytes while retaining the verified raw bytes.
                def ppu_payload(raw):
                    out = bytearray(raw)
                    for offset in (16, 20, 24, 28):
                        out[768 + offset] = out[768 + offset - 16]
                    return out
                if ppu_payload(previous) != ppu_payload(current):
                    raise ValueError(f"capture differs from verified blob: {key}")
                e["palram"] = arrays["palette"][i]
                continue
            play = arrays["nt"][i]
            entries.append({"rom": "redux" if mid else "orig", "map_id": mid,
                            "quest": quest, "level": level, "room_id": room,
                            "source_block": source_block_for(level, quest),
                            "nt": [[0x24] * 32 for _ in range(8)] + [play[y:y+32] for y in range(0, 704, 32)],
                            "attr": arrays["attr"][i], "palram": arrays["palette"][i]})

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
