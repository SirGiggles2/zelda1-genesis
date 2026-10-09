#!/usr/bin/env python3
"""Generate RoomRom/data/uw_l1q1_expected_doors.{c,h} from NES ground truth.

For each L1Q1 room (per RoomRom/data/uw_level1_quest1_rooms.json), decodes
4-direction door types from `data/rooms/dungeons.c` LevelBlockUW1Q1 attrs
A/B per NES Z_05.asm:FindDoorAttrByDoorBit.

Cross-checks output against RoomRom/tools/uw_reachability.py door_type()
for at least 3 sample rooms (mismatch -> exit non-zero).

Marks `reachable=1` for combat-free rooms (everything in the BFS-reachable
set from start room) and `reachable=0` for boss/triforce rooms (require
combat to enter per NES game flow).

Output schema (uw_l1q1_expected_doors.h):

    struct uw_expected_door_row {
        unsigned char room_id;
        unsigned char door_type[4];   /* index = DOOR_DIR_E/W/S/N */
        unsigned char reachable;
    };
    extern const struct uw_expected_door_row uw_l1q1_expected_doors[];
    extern const unsigned char uw_l1q1_expected_doors_count;

Door type values match RoomRom/src/uw_door_state.h DOOR_TYPE_*:
    0=OPEN 1=WALL 2=FALSE 3=FALSE2 4=BOMBABLE 5=KEY 6=KEY2 7=SHUTTER

Door direction indices match DOOR_DIR_*: E=0 W=1 S=2 N=3.

Run from repo root:
    python tools/uw_expected_doors_gen.py
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DUNGEONS_C = REPO / "data" / "rooms" / "dungeons.c"
L1Q1_JSON = REPO / "RoomRom" / "data" / "uw_level1_quest1_rooms.json"
OUT_C = REPO / "RoomRom" / "data" / "uw_l1q1_expected_doors.c"
OUT_H = REPO / "RoomRom" / "data" / "uw_l1q1_expected_doors.h"

# LevelBlockUW1Q1 lives at offset 0 in dungeons.c blob (first block per
# tools/extract_rooms.py:529-530 add_block order).
LEVEL_BLOCK_UW1Q1_OFFSET = 0
ATTRS_A_REL = 0
ATTRS_B_REL = 0x80


def parse_dungeons_blob() -> bytes:
    text = DUNGEONS_C.read_text(encoding="utf-8")
    return bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", text))


def door_type(blob: bytes, room_id: int, direction: str) -> int:
    """Direct port of RoomRom/tools/uw_reachability.py:door_type().
    Decodes per NES Z_05.asm:FindDoorAttrByDoorBit."""
    base = LEVEL_BLOCK_UW1Q1_OFFSET
    a = blob[base + ATTRS_A_REL + room_id]
    b = blob[base + ATTRS_B_REL + room_id]
    if direction == "N":
        return (a >> 5) & 0x07
    if direction == "S":
        return (a >> 2) & 0x07
    if direction == "W":
        return (b >> 5) & 0x07
    if direction == "E":
        return (b >> 2) & 0x07
    raise ValueError(direction)


def cross_check_against_reachability_tool(blob: bytes, sample_rooms: list[int]) -> None:
    """P1-4 fix: invoke the existing decode in uw_reachability.py and
    compare with our output for at least 3 sample rooms."""
    sys.path.insert(0, str(REPO / "RoomRom" / "tools"))
    try:
        import uw_reachability  # type: ignore
    except ImportError as exc:
        sys.exit(f"cross-check failed: cannot import uw_reachability: {exc}")
    # uw_reachability.door_type takes a 768-byte block (LevelBlockUW1Q1
    # only). Slice our blob to that.
    block_only = blob[LEVEL_BLOCK_UW1Q1_OFFSET:LEVEL_BLOCK_UW1Q1_OFFSET + 768]
    failures: list[str] = []
    for room_id in sample_rooms:
        for d in ("N", "S", "W", "E"):
            ours = door_type(blob, room_id, d)
            theirs = uw_reachability.door_type(block_only, room_id, d)
            if ours != theirs:
                failures.append(
                    f"  room ${room_id:02X} dir {d}: gen={ours} reachability={theirs}"
                )
    if failures:
        sys.exit("cross-check FAIL:\n" + "\n".join(failures))
    print(f"cross-check PASS: {len(sample_rooms)} rooms × 4 dirs match uw_reachability")


def parse_room_id(value) -> int:
    if isinstance(value, int):
        return value
    s = str(value).strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def collect_rows(blob: bytes) -> list[dict]:
    manifest = json.loads(L1Q1_JSON.read_text(encoding="utf-8"))
    if manifest.get("level") != 1 or manifest.get("quest") != 1:
        sys.exit("manifest is not L1Q1")
    room_ids = sorted(parse_room_id(r) for r in manifest["rooms"])
    boss_room = parse_room_id(manifest.get("boss_room_id", 0xFF))
    triforce_room = parse_room_id(manifest.get("triforce_room_id", 0xFF))
    combat_locked = {boss_room, triforce_room} - {0xFF}

    rows = []
    for room_id in room_ids:
        # Direction order in output array follows RoomRom DOOR_DIR_*:
        # E=0 W=1 S=2 N=3. NES decode uses N/S/W/E strings.
        types = [
            door_type(blob, room_id, "E"),  # DOOR_DIR_E = 0
            door_type(blob, room_id, "W"),  # DOOR_DIR_W = 1
            door_type(blob, room_id, "S"),  # DOOR_DIR_S = 2
            door_type(blob, room_id, "N"),  # DOOR_DIR_N = 3
        ]
        reachable = 0 if room_id in combat_locked else 1
        rows.append({
            "room_id": room_id,
            "door_type": types,
            "reachable": reachable,
        })
    return rows


HEADER_TEMPLATE = """\
/* Auto-generated by tools/uw_expected_doors_gen.py - do not edit.
 *
 * Expected door types per L1Q1 room, decoded from data/rooms/dungeons.c
 * LevelBlockUW1Q1 attrs A/B per NES Z_05.asm:FindDoorAttrByDoorBit
 * (line 4520). Cross-checked against RoomRom/tools/uw_reachability.py
 * at gen time.
 *
 * Door direction indices: E=0 W=1 S=2 N=3 (matches DOOR_DIR_* in
 * RoomRom/src/uw_door_state.h).
 *
 * Door type values: 0=OPEN 1=WALL 2=FALSE 3=FALSE2 4=BOMBABLE 5=KEY
 * 6=KEY2 7=SHUTTER (matches DOOR_TYPE_*).
 *
 * `reachable` flag: 1 = combat-free reachable in slice-1; 0 = requires
 * combat to enter (boss room, triforce room).
 */

#ifndef ROOMROM_UW_L1Q1_EXPECTED_DOORS_H
#define ROOMROM_UW_L1Q1_EXPECTED_DOORS_H

struct uw_expected_door_row {
    unsigned char room_id;
    unsigned char door_type[4];   /* DOOR_DIR_E / W / S / N */
    unsigned char reachable;
};

extern const struct uw_expected_door_row uw_l1q1_expected_doors[];
extern const unsigned char uw_l1q1_expected_doors_count;

#endif /* ROOMROM_UW_L1Q1_EXPECTED_DOORS_H */
"""


C_PROLOGUE = """\
/* Auto-generated by tools/uw_expected_doors_gen.py - do not edit. */

#include "uw_l1q1_expected_doors.h"

const struct uw_expected_door_row uw_l1q1_expected_doors[] = {
"""


C_EPILOGUE = """\
};

const unsigned char uw_l1q1_expected_doors_count =
    (unsigned char)(sizeof(uw_l1q1_expected_doors) /
                    sizeof(uw_l1q1_expected_doors[0]));
"""


def main() -> int:
    if not DUNGEONS_C.exists():
        sys.exit(f"missing {DUNGEONS_C}")
    if not L1Q1_JSON.exists():
        sys.exit(f"missing {L1Q1_JSON}")

    blob = parse_dungeons_blob()
    cross_check_against_reachability_tool(blob, [0x73, 0x72, 0x44])

    rows = collect_rows(blob)
    body_lines = []
    for r in rows:
        types = ", ".join(f"{t}u" for t in r["door_type"])
        comment_dirs = " ".join(
            f"{name}={DOOR_TYPE_NAMES[t]}"
            for name, t in zip(("E", "W", "S", "N"), r["door_type"])
        )
        body_lines.append(
            f"    {{ 0x{r['room_id']:02X}u, {{ {types} }}, {r['reachable']}u }},"
            f"  /* {comment_dirs} */"
        )

    OUT_H.write_text(HEADER_TEMPLATE, encoding="utf-8")
    OUT_C.write_text(C_PROLOGUE + "\n".join(body_lines) + "\n" + C_EPILOGUE,
                     encoding="utf-8")
    print(f"wrote {OUT_H.relative_to(REPO)}")
    print(f"wrote {OUT_C.relative_to(REPO)}  ({len(rows)} rows)")
    return 0


DOOR_TYPE_NAMES = {
    0: "OPEN", 1: "WALL", 2: "FALSE", 3: "FALSE2",
    4: "BOMBABLE", 5: "KEY", 6: "KEY2", 7: "SHUTTER",
}


if __name__ == "__main__":
    sys.exit(main())
