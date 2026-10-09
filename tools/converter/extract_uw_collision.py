#!/usr/bin/env python3
"""extract_uw_collision.py — generate UW walkability collision grids from NES room data.

NES source: reference/aldonunez/Z_05.asm:LayoutUWFloor (line 5307)
Pipeline mirrors LayoutUWFloor exactly:
  1. unique_room_id = LevelBlockAttrsD[room_id] & 0x3F
  2. RoomLayoutsUW[unique_room_id * 12 : + 12] → 12 column descriptors
  3. Each descriptor: high_nibble → ColumnHeapUW*, low_nibble → column index
  4. Scan heap to find column (count high-bit-set bytes), decode 7 row entries
  5. Each entry: bits[2:0] = primary_idx, bits[6:4] = repeat count
  6. PrimarySquaresUW = [0xB0,0x74,0x94,0xB4,0x70,0x68,0xF4,0x24]
     Walkable if primary < 0x78

Output: build/generated/<rom_hash>/uw_collision.bin
  9 levels × 2 quests × 128 rooms × 16 cols × 11 rows (packed bytes, 1=walkable, 0=wall)

NES source:  reference/aldonunez/Z_05.asm:LayoutUWFloor
Drained C:   src/oracle/room/room_load_runtime.c:roomld_setup_obj_room_bounds
Coverage:    PARTIAL (layout decode path)
Stance:      EXTEND
"""

import json
import re
import sys
import hashlib
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DUNGEONS_C = REPO / "data" / "rooms" / "dungeons.c"
MANIFEST_JSON = REPO / "data" / "rooms" / "MANIFEST.json"

PRIMARY_SQUARES_UW = [0xB0, 0x74, 0x94, 0xB4, 0x70, 0x68, 0xF4, 0x24]
WALKABLE_THRESHOLD = 0x78

# 2-bit metatile classification (debate ph5-t52-precheck Q3 GO Option B).
# Class is stored 1 byte per cell in the output grid. uw_room_walkable()
# masks: walkable iff class == CLASS_WALK.
#
# Primary tile -> class mapping derived from NES Z_07.asm:2099 WalkableTiles
# list and Z_07.asm threshold tile $78 (ObjectFirstUnwalkableTile UW).
# Stair tile $68 stays WALK so 5.6 stair-trigger fires on entry rather
# than being blocked by collision.
CLASS_WALK   = 0
CLASS_WALL   = 1
CLASS_WATER  = 2
CLASS_HAZARD = 3   # reserved (no tile in PRIMARY_SQUARES_UW maps here yet;
                   # populated by 5.3 / Ganon hazard floor work)

PRIMARY_CLASS_UW = {
    # Below NES threshold $78 (walkable per Z_07.asm:Walker_CheckTileCollision)
    0x70: CLASS_WALK,    # floor
    0x74: CLASS_WALK,    # floor variant
    0x68: CLASS_WALK,    # stairs (walkable; 5.6 triggers on entry)
    0x24: CLASS_WALK,    # low block / lift tile
    # At/above threshold $78 (unwalkable)
    0xB0: CLASS_WATER,   # water tile (rafts step here)
    0xB4: CLASS_WATER,   # water variant
    0x94: CLASS_WALL,    # wall variant
    0xF4: CLASS_WALL,    # high wall / pillar
}


def tile_to_class(tile: int) -> int:
    """Return class enum (0-3) for a primary-square tile value.

    Default WALL for any tile not in PRIMARY_CLASS_UW (defensive — extractor
    only ever passes PrimarySquaresUW values, so default should never fire).
    """
    return PRIMARY_CLASS_UW.get(tile, CLASS_WALL)

# UW floor occupies 12 encoded columns × 7 encoded rows = 12×7 metatile interior
# Total output grid: 16 wide × 11 tall (2 border cols on each side, 2 border rows top/bottom)
GRID_COLS = 16
GRID_ROWS = 11
FLOOR_COLS = 12    # encoded columns per room (CMP #$0C)
FLOOR_ROWS = 7     # encoded rows per column (CMP #$07)
FLOOR_COL_OFFSET = 2   # first interior column index (border = 0,1)
FLOOR_ROW_OFFSET = 2   # first interior row index (border = 0,1)


def parse_dungeons_c(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    m = re.search(r"rooms_dungeons\[\d+\] = \{([^}]+)\}", text, re.DOTALL)
    if not m:
        raise ValueError(f"Could not parse rooms_dungeons array in {path}")
    nums = re.findall(r"0x([0-9a-fA-F]+)", m.group(1))
    return bytes(int(x, 16) for x in nums)


def load_manifest(path: Path) -> dict:
    with open(path) as f:
        m = json.load(f)
    tables = {}
    for e in m.get("dungeons", []):
        tables[e["name"]] = (e["byte_offset"], e["byte_size"])
    return tables, m.get("nes_rom_sha256", "unknown")


def get_table(data: bytes, tables: dict, name: str) -> bytes:
    off, sz = tables[name]
    return data[off : off + sz]


def decode_heap_column(heap: bytes, col_idx: int) -> list[int]:
    """Decode one column from a ColumnHeapUW* blob.

    Mirrors LayoutUWFloor @FindSquare + @LoopSquareRow.
    Returns list of FLOOR_ROWS primary tile values.
    """
    pos = 0
    remaining = col_idx

    # @FindSquare: scan for the col_idx-th high-bit-set byte
    while True:
        if pos >= len(heap):
            raise ValueError(f"Heap underrun scanning for column {col_idx}")
        b = heap[pos]
        pos += 1
        if b & 0x80:
            if remaining == 0:
                break   # found our column; pos now points to first row byte
            remaining -= 1
        # always advance pos (done above)

    # @LoopSquareRow: decode FLOOR_ROWS row entries
    tiles = []
    repeat_count = 0   # $08 in NES
    row = 0            # $07 in NES
    while row < FLOOR_ROWS:
        if pos >= len(heap):
            raise ValueError(f"Heap underrun decoding rows at col_idx={col_idx}, row={row}")
        descriptor = heap[pos]
        primary_idx = descriptor & 0x07
        tile = PRIMARY_SQUARES_UW[primary_idx]
        tiles.append(tile)
        count_field = (descriptor & 0x70) >> 4
        if count_field == repeat_count:
            repeat_count = 0
            pos += 1     # advance to next descriptor
        else:
            repeat_count += 1
        row += 1

    return tiles


def build_room_grid(
    data: bytes,
    tables: dict,
    unique_room_id: int,
) -> list[list[int]]:
    """Build a GRID_COLS × GRID_ROWS classification grid.

    Each cell is a 2-bit class enum: 0=WALK, 1=WALL, 2=WATER, 3=HAZARD.
    Border cells are always WALL. Interior 12×7 from RoomLayoutsUW + heaps.
    Walkable test: class == CLASS_WALK.
    """
    # Initialize all cells as WALL (border)
    grid = [[CLASS_WALL] * GRID_ROWS for _ in range(GRID_COLS)]

    layout_off, _ = tables["RoomLayoutsUW"]
    layout_base = layout_off + unique_room_id * FLOOR_COLS
    layout_row = data[layout_base : layout_base + FLOOR_COLS]

    for col_enc in range(FLOOR_COLS):
        descriptor = layout_row[col_enc]
        heap_idx = (descriptor >> 4) & 0x0F
        column_idx = descriptor & 0x0F

        heap_name = f"ColumnHeapUW{heap_idx}"
        if heap_name not in tables:
            # Heap index out of range — treat as wall
            continue
        heap = get_table(data, tables, heap_name)

        try:
            tile_rows = decode_heap_column(heap, column_idx)
        except ValueError:
            continue

        grid_col = FLOOR_COL_OFFSET + col_enc
        for row_enc, tile in enumerate(tile_rows):
            grid_row = FLOOR_ROW_OFFSET + row_enc
            grid[grid_col][grid_row] = tile_to_class(tile)

    return grid


def extract_level_quest(
    data: bytes,
    tables: dict,
    lba_d_block_name: str,
) -> list[list[list[list[int]]]]:
    """Return grids[room_id][col][row] for all 128 rooms in a LevelBlock."""
    lba_d_off, lba_d_sz = tables[lba_d_block_name]
    # Sub-table D is at offset 0x180 within the 768-byte LevelBlock (6 × 128)
    d_offset = lba_d_off + 0x180

    grids = []
    for room_id in range(128):
        unique_room_id = data[d_offset + room_id] & 0x3F
        grid = build_room_grid(data, tables, unique_room_id)
        grids.append(grid)
    return grids


def grids_to_bytes(all_grids: list) -> bytes:
    """Pack collision grids to binary.

    Layout: [level 1-9][quest 1-2][room 0-127][col 0-15][row 0-10]
    Each cell = 1 byte (0 or 1). Total = 9 × 2 × 128 × 16 × 11 = 405,504 bytes.
    """
    out = bytearray()
    for level_grids in all_grids:        # 9 levels
        for quest_grids in level_grids:  # 2 quests
            for room_grids in quest_grids:  # 128 rooms
                for col in range(GRID_COLS):
                    for row in range(GRID_ROWS):
                        out.append(room_grids[col][row])
    return bytes(out)


def main() -> int:
    print("extract_uw_collision.py — building UW collision grids")

    data = parse_dungeons_c(DUNGEONS_C)
    tables, rom_sha = load_manifest(MANIFEST_JSON)

    out_dir = REPO / "build" / "generated" / rom_sha
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "uw_collision.bin"

    # Level sets: (levels_range, q1_block, q2_block)
    # Levels 1-6 share LevelBlockUW1; levels 7-9 share LevelBlockUW2.
    # All 9 levels × 2 quests → 18 combinations, but only 4 unique LevelBlocks.
    LEVEL_BLOCK_MAP = {
        # level → (q1_lba_d_block, q2_lba_d_block)
        1: ("LevelBlockUW1Q1", "LevelBlockUW1Q2"),
        2: ("LevelBlockUW1Q1", "LevelBlockUW1Q2"),
        3: ("LevelBlockUW1Q1", "LevelBlockUW1Q2"),
        4: ("LevelBlockUW1Q1", "LevelBlockUW1Q2"),
        5: ("LevelBlockUW1Q1", "LevelBlockUW1Q2"),
        6: ("LevelBlockUW1Q1", "LevelBlockUW1Q2"),
        7: ("LevelBlockUW2Q1", "LevelBlockUW2Q2"),
        8: ("LevelBlockUW2Q1", "LevelBlockUW2Q2"),
        9: ("LevelBlockUW2Q1", "LevelBlockUW2Q2"),
    }

    all_grids = []  # [level][quest][room][col][row]
    for level in range(1, 10):
        q1_block, q2_block = LEVEL_BLOCK_MAP[level]
        level_grids = []
        for quest_num, block_name in enumerate([q1_block, q2_block], start=1):
            print(f"  Level {level}, Quest {quest_num}: {block_name}")
            quest_grids = extract_level_quest(data, tables, block_name)
            level_grids.append(quest_grids)
        all_grids.append(level_grids)

    packed = grids_to_bytes(all_grids)
    out_path.write_bytes(packed)
    print(f"Wrote {len(packed):,} bytes -> {out_path.relative_to(REPO)}")

    # Sanity: border cells always WALL; at least some WALK cells per quest
    errors = []
    for li, level_grids in enumerate(all_grids):
        for qi, quest_grids in enumerate(level_grids):
            for ri, room_grids in enumerate(quest_grids):
                # Border cells must be CLASS_WALL
                for col in range(GRID_COLS):
                    for row in [0, GRID_ROWS - 1]:
                        if room_grids[col][row] != CLASS_WALL:
                            errors.append(
                                f"L{li+1}Q{qi+1}R{ri}: border ({col},{row}) "
                                f"class={room_grids[col][row]} != WALL")
                for row in range(GRID_ROWS):
                    for col in [0, GRID_COLS - 1]:
                        if room_grids[col][row] != CLASS_WALL:
                            errors.append(
                                f"L{li+1}Q{qi+1}R{ri}: border ({col},{row}) "
                                f"class={room_grids[col][row]} != WALL")
            # At least 20% of interior cells WALK across all rooms in this quest
            walk_total = sum(
                1
                for ri in range(128)
                for c in range(FLOOR_COL_OFFSET, FLOOR_COL_OFFSET + FLOOR_COLS)
                for r in range(FLOOR_ROW_OFFSET, FLOOR_ROW_OFFSET + FLOOR_ROWS)
                if quest_grids[ri][c][r] == CLASS_WALK
            )
            interior = 128 * FLOOR_COLS * FLOOR_ROWS
            pct = walk_total / interior
            if pct < 0.20:
                errors.append(f"L{li+1}Q{qi+1}: only {pct:.1%} WALK — too few floor cells")
    if errors:
        for e in errors[:10]:
            print(f"FAIL: {e}", file=sys.stderr)
        return 1

    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
