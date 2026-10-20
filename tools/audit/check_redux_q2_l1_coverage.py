"""T-308: require every authenticated Redux Q2 L1 normal-room destination.

The inventory receipt comes from the selected ROM's installed level records,
not Original's manifest. Check the actual generated lookup and its payloads,
including the runtime's UW1 sibling fallback and authenticated layout aliases.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "docs/audit/2026-10-12_T308_q2_l1_inventory.json"
ALIASES = ROOT / "data/graphics/verified_uw_layout_aliases.json"


def read_blob(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    arrays = {}
    for name, width in (("index", 4), ("nt", 704), ("attr", 64),
                        ("palette", 32), ("lookup", 128)):
        body = text.split("g_uw_room_" + name + "[", 1)[1].split("= {", 1)[1].split("};", 1)[0]
        body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        values = [int(v.rstrip("u"), 0) for v in re.findall(r"0x[\da-fA-F]+u?|\b\d+u?", body)]
        if len(values) % width:
            raise ValueError(f"truncated {name} array")
        arrays[name] = [values[i:i + width] for i in range(0, len(values), width)]
    n = len(arrays["index"])
    if any(len(arrays[k]) != n for k in ("nt", "attr", "palette")) or len(arrays["lookup"]) != 60:
        raise ValueError("incomplete generated room blob")
    return arrays


def payload(blob: dict, index: int) -> bytes:
    return bytes(blob["nt"][index] + blob["attr"][index] + blob["palette"][index])


def binding(blob: dict, key: tuple, aliases: list) -> int | None:
    mid, quest, level, room = key
    slot = blob["lookup"][(mid * 3 + quest) * 10 + level][room]
    if slot == 0:
        return None
    if not 1 <= slot <= len(blob["index"]):
        raise ValueError(f"lookup outside payload array: {key}")
    index = slot - 1
    source = blob["index"][index]
    if source != list(key):
        matching = [a for a in aliases if a["target"] == list(key) and a["source"] == source]
        digest = hashlib.sha256(bytes(blob["nt"][index])).hexdigest()
        if not matching or any(a["play_nt_sha256"] != digest for a in matching):
            raise ValueError(f"unauthenticated lookup alias: {key} -> {source}")
    return index


def check(blob: dict, rooms: list[int], aliases: list) -> dict:
    found, missing = {}, []
    for room in rooms:
        for level in range(1, 7):
            index = binding(blob, (1, 2, level, room), aliases)
            if index is not None:
                found[f"{room:02X}"] = {"level": level, "source": blob["index"][index],
                                        "payload_sha256": hashlib.sha256(payload(blob, index)).hexdigest()}
                break
        else:
            missing.append(f"{room:02X}")
    return {"profile": "redux", "quest": 2, "level": 1, "required": len(rooms),
            "covered": found, "missing": missing, "pass": not missing}


def preserve(before: dict, after: dict, aliases: list) -> dict:
    previous = {tuple(k): payload(before, i) for i, k in enumerate(before["index"])}
    current = {tuple(k): payload(after, i) for i, k in enumerate(after["index"])}
    changed = [list(k) for k, v in previous.items() if current.get(k) != v]
    alias_changes, restored_aliases = [], []
    for a in aliases:
        key = tuple(a["target"])
        b, c = binding(before, key, aliases), binding(after, key, aliases)
        if c is None or (b is not None and payload(before, b) != payload(after, c)):
            alias_changes.append(list(key))
        elif b is None:
            restored_aliases.append(list(key))
    return {"existing_entries": len(previous), "original_entries": sum(k[0] == 0 for k in previous),
            "changed_or_removed": changed, "changed_aliases": alias_changes,
            "restored_verified_aliases": restored_aliases,
            "added": [list(k) for k in current.keys() - previous.keys()],
            "pass": not changed and not alias_changes}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--blob", type=Path, default=ROOT / "engine/src/uw_room_blob.c")
    ap.add_argument("--requirements", type=Path, default=RECEIPT)
    ap.add_argument("--capture", type=Path, help="also validate a repeated extraction's installed inventory")
    ap.add_argument("--baseline", type=Path, help="require every previous payload and verified alias unchanged")
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    receipt = json.loads(args.requirements.read_text(encoding="utf-8"))
    rooms = receipt["required_rooms"]
    if args.capture:
        import sys
        sys.path.insert(0, str(ROOT / "tools"))
        from nesemu.zelda_uw_dump import normal_room_inventory
        capture = json.loads(args.capture.read_text(encoding="utf-8"))
        assert (capture["rom"], capture["quest"], capture["level"]) == ("redux", 2, 1)
        assert capture["rom_sha256"] == receipt["source_rom_sha256"]
        attrs, info = bytes(capture["installed_level_block"]), bytes(capture["installed_level_info"])
        assert hashlib.sha256(attrs + info).hexdigest() == receipt["installed_records_sha256"]
        assert normal_room_inventory(attrs, info, 1) == rooms == capture["required_rooms"]
    aliases = json.loads(ALIASES.read_text(encoding="utf-8"))
    blob = read_blob(args.blob)
    result = check(blob, rooms, aliases)
    if args.baseline:
        result["preservation"] = preserve(read_blob(args.baseline), blob, aliases)
        result["pass"] &= result["preservation"]["pass"]
    if args.out:
        args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Redux Q2 L1: {len(result['covered'])}/{len(rooms)} normal rooms; missing {result['missing']}")
    if "preservation" in result:
        print(json.dumps(result["preservation"]))
    return int(not result["pass"])


if __name__ == "__main__":
    raise SystemExit(main())
