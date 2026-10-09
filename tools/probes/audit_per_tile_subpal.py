#!/usr/bin/env python3
"""Phase I audit (2026-05-18 VRAM cleanup): per-NES-tile-ID sub-palette
usage across all rooms. Decides whether selective BG dedup is viable.

For each NES BG tile ID (0..255), count distinct sub-palettes it appears
with across all UW + OW rooms. If most tiles use 1-2 sub-pals, the BG_4x
replication can be selectively reduced.

UW: g_uw_room_nt[room][cell_idx] = NES tile ID at cell (col, row).
    g_uw_room_attr[room][quad_idx] = packed AT byte (4 sub-pals per byte).
    Each AT byte covers a 2x2 group of cells.

OW: data/rooms/overworld.c rooms_overworld blob. Bytes 0..127 = tile
    IDs (column-major mapping per data/rooms/overworld_offsets.h). The
    attr is a single byte per room (one sub-pal for entire room).

Output: histogram of (distinct sub-pals per tile), plus list of tiles
that need 4-way replication.
"""

from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
UW_BLOB_C = REPO / "engine" / "src" / "uw_room_blob.c"
OW_C = REPO / "data" / "rooms" / "overworld.c"


def parse_uw_blob():
    """Returns list of (room_idx, nt_bytes[22*32=704], attr_bytes[64])."""
    text = UW_BLOB_C.read_text(encoding="utf-8", errors="ignore")

    # g_uw_room_nt — flat array of (room_count * 704) bytes
    nt_match = re.search(r"g_uw_room_nt\[\d+\]\[\d+\] = \{(.+?)\n\};",
                         text, re.S)
    if not nt_match:
        return []
    nt_bytes = [int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})",
                                                nt_match.group(1))]

    attr_match = re.search(r"g_uw_room_attr\[\d+\]\[64\] = \{(.+?)\n\};",
                           text, re.S)
    if not attr_match:
        return []
    attr_bytes = [int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})",
                                                  attr_match.group(1))]

    CELLS_PER_ROOM = 22 * 32   # 704
    ATTR_PER_ROOM = 64

    n_nt = len(nt_bytes) // CELLS_PER_ROOM
    n_attr = len(attr_bytes) // ATTR_PER_ROOM
    n_rooms = min(n_nt, n_attr)

    rooms = []
    for r in range(n_rooms):
        nt = nt_bytes[r * CELLS_PER_ROOM:(r + 1) * CELLS_PER_ROOM]
        at = attr_bytes[r * ATTR_PER_ROOM:(r + 1) * ATTR_PER_ROOM]
        rooms.append((r, nt, at))
    return rooms


def uw_quad_subpal(attr_byte, quad_pos):
    """quad_pos = 0..3 (TL, TR, BL, BR within attr byte)."""
    return (attr_byte >> (quad_pos * 2)) & 0x03


def collect_uw_per_tile_subpals(rooms):
    """Returns dict {tile_id: set(sub_pals)}.

    Uses the EXACT attr formula from src/game/dungeon/uw_render.c:380-392:
        at_idx = ((nt_row >> 2) << 3) | (nt_col >> 2)   -- 8 quads/row, 8 rows
        shift  = (((nt_row >> 1) & 1) << 2) | (((nt_col >> 1) & 1) << 1)
        sub_pal = (attr[at_idx] >> shift) & 0x03

    UW room nt is 22 rows x 32 cols (engine/src/uw_room_blob.h
    ROOMROM_UW_BLOB_ROWS/COLS). Cell index = row * 32 + col.
    Cells render starting at nt_row = row + 8 (HUD occupies 0..7).
    T-114: the loops were transposed (32 rows x 22 cols), so columns
    22..31 — the east third of every room, east doors included — were
    never collected and their (tile, sub-pal) combos drew blank.
    """
    out = defaultdict(set)
    for room_idx, nt, attr in rooms:
        for row in range(22):
            for col in range(32):
                nt_offset = row * 32 + col
                if nt_offset >= len(nt):
                    continue
                tile_id = nt[nt_offset]
                # Match uw_render.c::attr_palette_for() exactly:
                nt_row = row + 8
                nt_col = col
                at_idx = ((nt_row >> 2) << 3) | (nt_col >> 2)
                if at_idx >= len(attr):
                    continue
                attr_byte = attr[at_idx & 0x3F]
                shift = (((nt_row >> 1) & 1) << 2) | (((nt_col >> 1) & 1) << 1)
                sub_pal = (attr_byte >> shift) & 0x03
                out[tile_id].add(sub_pal)
    return out


def parse_ow_blob():
    """Returns the rooms_overworld byte array (4114 bytes per current state)."""
    if not OW_C.exists():
        return []
    text = OW_C.read_text(encoding="utf-8", errors="ignore")
    m = re.search(r"rooms_overworld\[\d+\]\s*=\s*\{(.+?)\};", text, re.S)
    if not m:
        return []
    return [int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))]


# OW renderer tables mirrored from src/game/world/render/ow_render.c
OW_S_PRIMARY_SQUARES = [
    0x24,0x6F,0xF3,0xFA,0x98,0x90,0x8F,0x95,
    0x8E,0x90,0x74,0x76,0xF3,0x24,0x26,0x89,
    0x03,0x04,0x70,0xC8,0xBC,0x8D,0x8F,0x93,
    0x95,0xC4,0xCE,0xD8,0xB0,0xB4,0xAA,0xAC,
    0xB8,0x9C,0xA6,0x9A,0xA2,0xA0,0xE5,0xE6,
    0xE7,0xE8,0xE9,0xEA,0xC0,0xE0,0x78,0x7A,
    0x7E,0x80,0xCC,0xD0,0xD4,0xDC,0x89,0x84,
    0x24,0x24,0x24,0x24,0x6F,0x6F,0x6F,0x6F,
]
OW_S_SECONDARY_SQUARES = [
    0x24,0x24,0x24,0x24,0x6F,0x6F,0x6F,0x6F,
    0xF3,0xF3,0xF3,0xF3,0xFA,0xFA,0xFA,0xFA,
    0x98,0x95,0x26,0x26,0x90,0x95,0x90,0x95,
    0x8F,0x90,0x8F,0x90,0x95,0x96,0x95,0x96,
    0x8E,0x93,0x90,0x95,0x90,0x95,0x92,0x97,
    0x74,0x74,0x75,0x75,0x76,0x77,0x76,0x77,
    0xF3,0x24,0xF3,0x24,0x24,0x24,0x24,0x24,
    0x26,0x26,0x26,0x26,0x89,0x88,0x8B,0x88,
]
OW_S_HEAP_OFFSETS = [0, 53, 102, 168, 236, 286, 346, 405, 464, 526, 591, 660, 721, 775, 841, 893]
OW_S_TILE_OBJECT_PRIMARY = [0xC8, 0xD8, 0xC4, 0xBC, 0xC0, 0xC0]
OW_S_PAL_TO_ATTR = [0x00, 0x55, 0xAA, 0xFF]

OW_ATTRS_A_OFFSET = 0
OW_ATTRS_B_OFFSET = 128
OW_ATTRS_D_OFFSET = 384
OW_LAYOUTS_OFFSET = 1166
OW_HEAP_BLOB_OFFSET = 3150


def ow_normalize_primary_tile(raw):
    """Mirror of normalize_primary_tile() in ow_render.c:234-243."""
    if 0xE5 <= raw <= 0xEA:
        return OW_S_TILE_OBJECT_PRIMARY[raw - 0xE5]
    return raw


def ow_tile_palette(tile_col, tile_row, outer_pal, inner_pal):
    """Mirror of ow_tile_palette() in ow_render.c:285-307."""
    attr_index = ((tile_row >> 2) << 3) + (tile_col >> 2)
    attr_col = attr_index & 0x07
    attr = OW_S_PAL_TO_ATTR[outer_pal & 0x03]
    inner_attr = OW_S_PAL_TO_ATTR[inner_pal & 0x03]
    if 9 <= attr_index < 0x27 and attr_col != 0 and attr_col != 7:
        if attr_index >= 0x21:
            attr = (inner_attr & 0x0F) | (attr & 0xF0)
        else:
            attr = inner_attr
    shift = 0
    if tile_col & 0x02:
        shift += 2
    if tile_row & 0x02:
        shift += 4
    return (attr >> shift) & 0x03


def collect_ow_per_tile_subpals(rooms_bytes):
    """Walk all 128 OW rooms via the renderer's metatile/heap decode.
    Returns dict {tile_id: set(sub_pals)}.

    Mirrors render_one_metatile_col() at ow_render.c:400-467 exactly:
    - attr lookup uses OW_ATTRS_A_OFFSET / _B_OFFSET / _D_OFFSET
    - heap traversal with 0x80 column-boundary markers
    - 0x40 repeat-state bit on sq_byte
    - tile dispatch via PRIMARY (sq_idx >= 0x10) or SECONDARY squares
    - 4 tiles per metatile: TL, BL, TR, BR
    - 11 rows per column, 16 cols per room
    - playfield base row = HUD rows (7) below origin; renderer dst_row_base
      passed in; for sub-pal computation we use ON-PLANE row (room rendered
      with HUD above). Use renderer's convention: cells appear at tile_row =
      ROOMROM_HUD_ROWS + (metatile_row * 2 + sub_row).
    """
    HUD_ROWS = 7  # ROOMROM_HUD_ROWS per ow_render.h:8
    out = defaultdict(set)
    for room_id in range(128):
        if (OW_ATTRS_D_OFFSET + room_id) >= len(rooms_bytes):
            break
        outer_pal = rooms_bytes[OW_ATTRS_A_OFFSET + room_id] & 0x03
        inner_pal = rooms_bytes[OW_ATTRS_B_OFFSET + room_id] & 0x03
        unique_id = rooms_bytes[OW_ATTRS_D_OFFSET + room_id] & 0x7F
        col_dirs_base = OW_LAYOUTS_OFFSET + unique_id * 16
        for src_col in range(16):
            desc = rooms_bytes[col_dirs_base + src_col]
            heap_idx = (desc >> 4) & 0x0F
            col_in_heap = desc & 0x0F
            heap_ptr = OW_HEAP_BLOB_OFFSET + OW_S_HEAP_OFFSETS[heap_idx]
            # Skip col_in_heap columns (0x80 marks column boundaries)
            cols_found = col_in_heap
            y = 0
            while True:
                if heap_ptr + y >= len(rooms_bytes):
                    break
                if rooms_bytes[heap_ptr + y] & 0x80:
                    if cols_found == 0:
                        break
                    cols_found -= 1
                y += 1
            heap_ptr += y
            # Render 11 metatile rows
            row = 0
            repeat_state = 0
            while row < 11:
                if heap_ptr >= len(rooms_bytes):
                    break
                sq_byte = rooms_bytes[heap_ptr]
                sq_idx = sq_byte & 0x3F
                if sq_idx >= 0x10:
                    p = ow_normalize_primary_tile(OW_S_PRIMARY_SQUARES[sq_idx])
                    tiles = [p, p + 1, p + 2, p + 3]
                else:
                    b = sq_idx * 4
                    tiles = [
                        OW_S_SECONDARY_SQUARES[b],
                        OW_S_SECONDARY_SQUARES[b + 1],
                        OW_S_SECONDARY_SQUARES[b + 2],
                        OW_S_SECONDARY_SQUARES[b + 3],
                    ]
                # Position 4 tiles: TL=(c*2, r*2), BL=(c*2, r*2+1), TR=(c*2+1, r*2), BR=(c*2+1, r*2+1)
                base_col = src_col * 2
                base_row = HUD_ROWS + row * 2
                positions = [
                    (base_col,     base_row),
                    (base_col,     base_row + 1),
                    (base_col + 1, base_row),
                    (base_col + 1, base_row + 1),
                ]
                for tile_id, (tcol, trow) in zip(tiles, positions):
                    sub_pal = ow_tile_palette(tcol, trow, outer_pal, inner_pal)
                    out[tile_id].add(sub_pal)
                row += 1
                if sq_byte & 0x40:
                    repeat_state ^= 0x40
                    if repeat_state != 0:
                        continue
                heap_ptr += 1
    return out


def histogram_report(label, per_tile):
    histogram = defaultdict(int)
    multi = []
    for tile_id, subpals in per_tile.items():
        n = len(subpals)
        histogram[n] += 1
        if n >= 3:
            multi.append((tile_id, sorted(subpals)))
    total = sum(histogram.values())
    print(f"\n{label} per-tile sub-pal distribution (across {total} unique tile IDs):")
    for k in sorted(histogram.keys()):
        pct = 100.0 * histogram[k] / total
        print(f"  tiles using {k} sub-pal(s): {histogram[k]:4d}  ({pct:5.1f}%)")
    copies_needed = sum(len(s) for s in per_tile.values())
    copies_4x = total * 4
    print(f"\n{label} replication accounting:")
    print(f"  current (4x replication): {copies_4x:4d} tile copies")
    print(f"  selective dedup minimum:  {copies_needed:4d} tile copies")
    print(f"  savings:                  {copies_4x - copies_needed:4d} tiles "
          f"({100.0 * (copies_4x - copies_needed) / copies_4x:.1f}%)")
    if multi:
        print(f"\n{label} tiles needing 3+ sub-pals (worst-case):")
        for tile_id, sps in sorted(multi)[:15]:
            print(f"  tile 0x{tile_id:02X}: {sps}")
        if len(multi) > 15:
            print(f"  ... and {len(multi) - 15} more")
    return histogram


def main():
    uw_rooms = parse_uw_blob()
    print(f"UW rooms parsed: {len(uw_rooms)}")
    if not uw_rooms:
        print("ERROR: no UW rooms parsed; check uw_room_blob.c format")
        return 1
    per_tile_uw = collect_uw_per_tile_subpals(uw_rooms)
    histogram_report("UW BG", per_tile_uw)

    # Phase J Step 1 (2026-05-18 §36.1 must-fix): OW per-tile audit
    ow_bytes = parse_ow_blob()
    print(f"\nOW blob bytes: {len(ow_bytes)}")
    if not ow_bytes:
        print("ERROR: no OW blob parsed; check overworld.c format")
        return 1
    per_tile_ow = collect_ow_per_tile_subpals(ow_bytes)
    histogram_report("OW BG", per_tile_ow)

    # COMBINED UW + OW (per-tile sub-pal usage across both scene types)
    combined = defaultdict(set)
    for tile_id, sps in per_tile_uw.items():
        combined[tile_id].update(sps)
    for tile_id, sps in per_tile_ow.items():
        combined[tile_id].update(sps)
    print(f"\nCOMBINED UW+OW per-tile sub-pal distribution:")
    histogram_report("COMBINED", combined)

    per_tile = combined  # for the rest of the script
    multi_subpal_tiles = [(tid, sorted(sps)) for tid, sps in per_tile.items() if len(sps) >= 3]
    # Keep variable for legacy summary block below
    histogram = defaultdict(int)
    for sps in per_tile.values():
        histogram[len(sps)] += 1
    total_tiles_used = sum(histogram.values())
    total_copies_needed = sum(len(s) for s in per_tile.values())
    total_copies_4x = total_tiles_used * 4
    savings_tiles = total_copies_4x - total_copies_needed
    savings_pct = 100.0 * savings_tiles / total_copies_4x

    print(f"\nReplication accounting (COMBINED UW+OW BG):")
    print(f"  current (4x replication): {total_copies_4x:4d} tile copies")
    print(f"  selective dedup minimum:  {total_copies_needed:4d} tile copies")
    print(f"  potential savings:        {savings_tiles:4d} tiles ({savings_pct:.1f}%)")

    print(f"\nTiles needing 3+ sub-pals (worst-case replication):")
    for tile_id, sps in sorted(multi_subpal_tiles)[:20]:
        print(f"  tile 0x{tile_id:02X}: {sps}")
    if len(multi_subpal_tiles) > 20:
        print(f"  ... and {len(multi_subpal_tiles) - 20} more")

    # Phase J truncation analysis: max NES tile ID used.
    max_tile_id = max(per_tile_uw.keys())
    print(f"\nTruncation analysis (UW BG only):")
    print(f"  max NES tile ID used: 0x{max_tile_id:02X} ({max_tile_id})")
    print(f"  current 4x bank: 256 tiles/subpal x 4 = 1024 tiles")
    print(f"  truncated bank: {max_tile_id+1} tiles/subpal x 4 = {(max_tile_id+1)*4} tiles")
    print(f"  truncation savings: {1024 - (max_tile_id+1)*4} tiles")

    # Per-sub-pal max tile ID (smarter truncation):
    max_per_subpal = {0: -1, 1: -1, 2: -1, 3: -1}
    for tile_id, sps in per_tile_uw.items():
        for sp in sps:
            if tile_id > max_per_subpal[sp]:
                max_per_subpal[sp] = tile_id
    total_per_subpal_truncated = sum(max + 1 for max in max_per_subpal.values() if max >= 0)
    print(f"  per-sub-pal max tile IDs: {[f'pal{k}=0x{v:02X}' for k,v in max_per_subpal.items()]}")
    print(f"  per-sub-pal-truncated bank: {total_per_subpal_truncated} tiles")
    print(f"  per-sub-pal truncation savings: {1024 - total_per_subpal_truncated} tiles")

    return 0


if __name__ == "__main__":
    sys.exit(main())
