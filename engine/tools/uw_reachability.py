#!/usr/bin/env python3
"""
engine underworld reachability extractor.

Parses NES Z1 underworld level data from `reference/aldonunez/dat/` and
produces engine/data/uw_level{L}_quest{Q}_rooms.json with:
  level, quest, start_room_id, source_blocks, rooms, passage_rooms,
  room_tags, edges.

Hard gates (exit non-zero on mismatch):
  - L1Q1 rooms set must be exactly:
        {0x22,0x23,0x33,0x35,0x36,0x41,0x42,0x43,0x44,0x45,
         0x52,0x53,0x54,0x63,0x72,0x73,0x74}   (17 rooms)
  - L9Q1 rooms use LevelBlockUW2Q1 reachability plus its NES-confirmed
    Ganon and Zelda chambers, which the doorway-only BFS does not reach.

Door encoding (per Z_05.asm:4515-4560 FindDoorAttrByDoorBit):
  Plane A holds N/S doors per room. Plane B holds W/E doors per room.
    N door type = (A[room] >> 5) & 0x07
    S door type = (A[room] >> 2) & 0x07
    W door type = (B[room] >> 5) & 0x07
    E door type = (B[room] >> 2) & 0x07
  Door types (per disasm): 0=open, 1..3=walls, 4=bombable, 5=key,
  6=key2, 7=shutter. Reachable types: {0, 4, 5, 6, 7}.

Adjacency: room_id is (row << 4) | col where row in 0..7, col in 0..7.
  N = room_id - 0x10, S = room_id + 0x10
  W = room_id - 1,    E = room_id + 1

LevelInfo .dat layout (256 bytes, base = LevelInfo_PalettesTransferBuf=$6B7E):
  Offset $29 ($6BA7) = LevelInfo_ShortcutOrItemPosArray (Q2 replacement target)
  Offset $2D ($6BAD) = LevelInfo_StartRoomId
  Offset $2E ($6BAE) = LevelInfo_TriforceRoomId
  Offset $33 ($6BB1) = LevelInfo_LevelNumber
  Offset $34 ($6BB2) = LevelInfo_CellarRoomIdArray (10 bytes)
  Offset $3E ($6BBC) = LevelInfo_BossRoomId

Quest 2 patches:
  - LevelBlockAttrs B byte patches per LevelBlockAttrsBQ2ReplacementOffsets
    + LevelBlockAttrsBQ2ReplacementValues (8 byte pairs, hardcoded below
    from reference/aldonunez/Z_06.asm:263-266).
  - LevelInfoUWQ2Replacements{N} overlays starting at offset $29.
"""

import json
import os
import struct
import sys
from collections import deque
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DAT_DIR = REPO_ROOT / "reference" / "aldonunez" / "dat"
ASM_FILE = REPO_ROOT / "src" / "zelda_translated" / "z_06.asm"
OUT_DIR = REPO_ROOT / "engine" / "data"

# Each LevelInfoUW{N}.dat is 256 bytes but with VARIABLE origin offset:
# L1=$6B82, descending by 4 bytes per level (L9=$6BA2). We anchor by
# searching for the FoeCounts byte pattern "03 05 06 08" in the file
# (these foe-count constants are level-invariant). All other LevelInfo
# fields are fixed offsets from the FoeCounts landmark:
#   FoeCounts            -> +0x00 (anchor)
#   StartY               -> +0x04
#   ShortcutOrItemPosArr -> +0x05
#   SubmenuMapRotation   -> +0x09
#   StatusBarMapXOffset  -> +0x0A
#   StartRoomId          -> +0x0B
#   TriforceRoomId       -> +0x0C
#   WorldFlagsAddr       -> +0x0D (lo) / +0x0E (hi)
#   LevelNumber          -> +0x0F
#   CellarRoomIdArray    -> +0x10 (10 bytes)
#   BossRoomId           -> +0x1A
FOE_COUNTS_PATTERN = bytes([0x03, 0x05, 0x06, 0x08])
START_ROOM_REL = 0x0B
TRIFORCE_ROOM_REL = 0x0C
LEVEL_NUMBER_REL = 0x0F
CELLAR_ARRAY_REL = 0x10
CELLAR_ARRAY_LEN = 10
BOSS_ROOM_REL = 0x1A
SHORTCUT_REL = 0x05  # ShortcutOrItemPosArray (Q2 replacement target)

# NOTE: LevelBlockAttrsBQ2ReplacementOffsets/Values target the OVERWORLD
# block (per Z_06.asm:237 "@PatchQ2Rooms: Replace attributes of several
# rooms in OW in second quest"), NOT underworld. The UW1Q2.dat and
# UW2Q2.dat files already contain the complete Q2 underworld data, so
# no in-place patching is needed for UW Q2 reachability.

# Reachable door types.
PASSABLE_DOORS = {0, 4, 5, 6, 7}

# L1->L6 use UW1, L7->L9 use UW2.
def block_name_for_level(level: int, quest: int) -> str:
    if level <= 6:
        return f"LevelBlockUW1Q{quest}"
    return f"LevelBlockUW2Q{quest}"


def info_name_for_level(level: int) -> str:
    return f"LevelInfoUW{level}"


def load_block(level: int, quest: int) -> bytes:
    """Load 768-byte LevelBlockUW{1,2}Q{1,2}.dat. UW Q2 .dat already
    contains the full Q2 grid; no in-place patching needed."""
    name = block_name_for_level(level, quest)
    path = DAT_DIR / f"{name}.dat"
    raw = path.read_bytes()
    if len(raw) != 768:
        raise RuntimeError(f"{path} has wrong size {len(raw)}, expected 768")
    return raw


def find_foe_counts_offset(raw: bytes) -> int:
    """Locate FoeCounts byte pattern in a LevelInfoUW{N}.dat. Each level
    has a different leading prefix length. Returns the byte offset."""
    idx = raw.find(FOE_COUNTS_PATTERN)
    if idx < 0:
        raise RuntimeError("FoeCounts pattern '03 05 06 08' not found in LevelInfo .dat")
    return idx


def parse_q2_replacement_payload(asm_text: str, level: int) -> bytes:
    """Read LevelInfoUWQ2Replacements{level} bytes from z_06.asm."""
    label = f"LevelInfoUWQ2Replacements{level}:"
    lines = asm_text.splitlines()
    try:
        i0 = next(i for i, ln in enumerate(lines) if ln.strip().startswith(label))
    except StopIteration:
        raise RuntimeError(f"label {label} not found in {ASM_FILE}")
    out = bytearray()
    for ln in lines[i0 + 1:]:
        s = ln.strip()
        if not s:
            continue
        if s.startswith(";"):
            continue
        if s.startswith("even") or s == "even":
            break
        if s.startswith("dc.b"):
            payload = s.split("dc.b", 1)[1]
            # strip inline comment
            if ";" in payload:
                payload = payload.split(";", 1)[0]
            for tok in payload.split(","):
                tok = tok.strip()
                if not tok:
                    continue
                if tok.startswith("$"):
                    out.append(int(tok[1:], 16))
                else:
                    out.append(int(tok, 0))
        elif s.endswith(":"):
            # next label, stop
            break
        else:
            # unknown directive -> stop
            break
    return bytes(out)


def parse_q2_replacement_sizes(asm_text: str) -> list[int]:
    """Read LevelInfoUWQ2ReplacementSizes (9 bytes) from z_06.asm."""
    label = "LevelInfoUWQ2ReplacementSizes:"
    lines = asm_text.splitlines()
    try:
        i0 = next(i for i, ln in enumerate(lines) if ln.strip().startswith(label))
    except StopIteration:
        raise RuntimeError("LevelInfoUWQ2ReplacementSizes not found")
    sizes: list[int] = []
    for ln in lines[i0 + 1:]:
        s = ln.strip()
        if not s or s.startswith(";"):
            continue
        if s.startswith("even") or s.endswith(":"):
            break
        if s.startswith("dc.b"):
            payload = s.split("dc.b", 1)[1]
            if ";" in payload:
                payload = payload.split(";", 1)[0]
            for tok in payload.split(","):
                tok = tok.strip()
                if not tok:
                    continue
                sizes.append(int(tok[1:], 16) if tok.startswith("$") else int(tok, 0))
        else:
            break
    return sizes


def load_levelinfo(level: int, quest: int, asm_text: str) -> tuple[bytes, int]:
    """Load 256-byte LevelInfoUW{N}.dat and return (data, foe_counts_offset).
    If quest==2, overlay LevelInfoUWQ2Replacements{N} bytes starting at
    ShortcutOrItemPosArray (foe_counts_off + SHORTCUT_REL)."""
    path = DAT_DIR / f"{info_name_for_level(level)}.dat"
    raw = bytearray(path.read_bytes())
    if len(raw) != 256:
        raise RuntimeError(f"{path} has wrong size {len(raw)}, expected 256")
    fc_off = find_foe_counts_offset(bytes(raw))
    if quest == 2:
        payload = parse_q2_replacement_payload(asm_text, level)
        sizes = parse_q2_replacement_sizes(asm_text)
        if level - 1 >= len(sizes):
            raise RuntimeError(f"missing size for level {level}")
        size = sizes[level - 1]
        if size != len(payload):
            sys.stderr.write(
                f"warn: Q2 L{level} payload {len(payload)} != size {size}; "
                f"using {min(size, len(payload))}\n"
            )
            size = min(size, len(payload))
        target = fc_off + SHORTCUT_REL
        for i in range(size):
            raw[target + i] = payload[i]
    return bytes(raw), fc_off


def adj(room_id: int, direction: str) -> int | None:
    """Underworld grid is 16 cols x 8 rows (room_id = (row<<4)|col,
    row 0..7, col 0..15). All 128 IDs are valid."""
    row = (room_id >> 4) & 0x07
    col = room_id & 0x0F
    if direction == "N":
        if row == 0:
            return None
        return ((row - 1) << 4) | col
    if direction == "S":
        if row == 7:
            return None
        return ((row + 1) << 4) | col
    if direction == "W":
        if col == 0:
            return None
        return (row << 4) | (col - 1)
    if direction == "E":
        if col == 0x0F:
            return None
        return (row << 4) | (col + 1)
    raise ValueError(direction)


def door_type(block: bytes, room_id: int, direction: str) -> int:
    a = block[0 * 128 + room_id]
    b = block[1 * 128 + room_id]
    if direction == "N":
        return (a >> 5) & 0x07
    if direction == "S":
        return (a >> 2) & 0x07
    if direction == "W":
        return (b >> 5) & 0x07
    if direction == "E":
        return (b >> 2) & 0x07
    raise ValueError(direction)


def bfs(block: bytes, start_room: int) -> tuple[set[int], list[dict]]:
    seen: set[int] = {start_room}
    edges: list[dict] = []
    queue = deque([start_room])
    while queue:
        room = queue.popleft()
        for d in ("N", "S", "W", "E"):
            dt = door_type(block, room, d)
            nxt = adj(room, d)
            if nxt is None:
                continue
            if dt in PASSABLE_DOORS:
                edges.append({
                    "from": f"0x{room:02X}",
                    "to": f"0x{nxt:02X}",
                    "dir": d,
                    "door_type": dt,
                })
                if nxt not in seen:
                    seen.add(nxt)
                    queue.append(nxt)
    return seen, edges


def extract(level: int, quest: int) -> dict:
    asm_text = ASM_FILE.read_text(encoding="utf-8", errors="replace")
    block = load_block(level, quest)
    info, fc_off = load_levelinfo(level, quest, asm_text)

    start_room = info[fc_off + START_ROOM_REL]
    triforce_room = info[fc_off + TRIFORCE_ROOM_REL]
    boss_room = info[fc_off + BOSS_ROOM_REL]
    cellar_array = list(
        info[fc_off + CELLAR_ARRAY_REL:fc_off + CELLAR_ARRAY_REL + CELLAR_ARRAY_LEN]
    )
    level_number_byte = info[fc_off + LEVEL_NUMBER_REL]
    if level_number_byte != level:
        sys.stderr.write(
            f"warn: LevelNumber field={level_number_byte}, expected {level}\n"
        )

    rooms_set, edges = bfs(block, start_room)

    # L9Q1's boss and Zelda chambers follow the final passage sequence,
    # outside this extractor's doorway-only BFS. LevelInfo names both;
    # live NES captures confirm their populated nametables. Include them
    # in the room-capture set so the connected ending has real graphics.
    if level == 9 and quest == 1:
        if boss_room != 0xFF:
            rooms_set.add(boss_room)
        if triforce_room != 0xFF:
            rooms_set.add(triforce_room)

    # Cellars are special stair-linked destinations. They are NOT door-BFS
    # reachable so the canonical 17-room L1Q1 set excludes them. Track
    # cellars only as passage_rooms; do NOT union into rooms.
    cellar_targets = [c for c in cellar_array if c != 0xFF]
    passage_rooms_set = set(cellar_targets) - rooms_set

    # Tag rooms.
    room_tags: dict[str, list[str]] = {}
    def tag(r: int, t: str) -> None:
        key = f"0x{r:02X}"
        room_tags.setdefault(key, []).append(t)

    tag(start_room, "start")
    if boss_room != 0xFF:
        tag(boss_room, "boss")
    if triforce_room != 0xFF:
        tag(triforce_room, "triforce")
    for r in cellar_targets:
        tag(r, "passage")

    rooms_sorted = sorted(rooms_set)
    return {
        "level": level,
        "quest": quest,
        "start_room_id": f"0x{start_room:02X}",
        "boss_room_id": f"0x{boss_room:02X}" if boss_room != 0xFF else None,
        "triforce_room_id": f"0x{triforce_room:02X}" if triforce_room != 0xFF else None,
        "source_blocks": [block_name_for_level(level, quest)],
        "rooms": [f"0x{r:02X}" for r in rooms_sorted],
        "passage_rooms": [f"0x{r:02X}" for r in sorted(passage_rooms_set)],
        "cellar_targets": [f"0x{r:02X}" for r in cellar_targets],
        "room_tags": room_tags,
        "edges": edges,
    }


# Hard gates -----------------------------------------------------------------

L1Q1_EXPECTED = {
    0x22, 0x23, 0x33, 0x35, 0x36, 0x41, 0x42, 0x43, 0x44, 0x45,
    0x52, 0x53, 0x54, 0x63, 0x72, 0x73, 0x74,
}


def gate_l1q1(result: dict) -> None:
    got = {int(r, 16) for r in result["rooms"]}
    if got != L1Q1_EXPECTED:
        missing = sorted(L1Q1_EXPECTED - got)
        extra = sorted(got - L1Q1_EXPECTED)
        sys.stderr.write(
            "GATE FAIL L1Q1:\n"
            f"  expected {sorted(L1Q1_EXPECTED)}\n"
            f"  got      {sorted(got)}\n"
            f"  missing  {[f'0x{r:02X}' for r in missing]}\n"
            f"  extra    {[f'0x{r:02X}' for r in extra]}\n"
        )
        sys.exit(2)


def gate_l9q1(result: dict) -> None:
    """L9Q1 must use LevelBlockUW2Q1 (proves source-block routing)."""
    if result["source_blocks"] != ["LevelBlockUW2Q1"]:
        sys.stderr.write(
            f"GATE FAIL L9Q1 source_blocks: {result['source_blocks']}\n"
        )
        sys.exit(2)
    if len(result["rooms"]) < 1:
        sys.stderr.write("GATE FAIL L9Q1: no rooms reached\n")
        sys.exit(2)


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        sys.stderr.write(
            "usage: uw_reachability.py <level 1..9> <quest 1..2> [--gate]\n"
        )
        return 1
    level = int(argv[1])
    quest = int(argv[2])
    gate = "--gate" in argv[3:]
    if level < 1 or level > 9 or quest < 1 or quest > 2:
        sys.stderr.write("level must be 1..9, quest must be 1..2\n")
        return 1

    result = extract(level, quest)

    if gate:
        if level == 1 and quest == 1:
            gate_l1q1(result)
        if level == 9 and quest == 1:
            gate_l9q1(result)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_path = OUT_DIR / f"uw_level{level}_quest{quest}_rooms.json"
    out_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out_path} ({len(result['rooms'])} rooms)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
