#!/usr/bin/env python3
"""
extract_rooms.py - Extract room and level data from NES Zelda 1 to Genesis C arrays.

S2 Phase C2: ports the old .inc emission to Genesis-ready C-array output under
data/rooms/.  The column-encoded room format is preserved verbatim so the
Genesis runtime can interpret these tables directly without pre-expansion.

Data sources:
  - ROM PRG banks for .INCBIN blocks and common bank data
  - Aldonunez disassembly for inline room tables and patch tables

Primary outputs (C arrays, data/rooms/):
  overworld.c   -- overworld room attributes, layouts, column heaps,
                   level info, common data block
  dungeons.c    -- underworld room attributes (Q1 + Q2), dungeon level
                   info, UW column heaps, Q2 patch tables
  MANIFEST.json -- schema_version + nes_rom_sha256 + per-block metadata

Legacy outputs (--legacy-inc flag only):
  src/data/rooms_overworld.inc, rooms_underworld1.inc, rooms_underworld2.inc,
  level_info.inc, room_layouts.inc, room_columns.inc, room_common.inc,
  room_patches.inc
"""

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

# Canonical NES ROM SHA256.
NES_ROM_SHA256 = "8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac"

INES_HEADER_SIZE = 16
PRG_BANK_SIZE = 0x4000

LEVEL_BLOCK_SIZE = 0x0300
# LevelInfo blocks are 252 bytes in ROM, not 256 (2026-08-03, byte-verified).
#
# The old 0x100 value drifted 4 bytes per level, so LevelInfoUW<n> was read
# 4*(n-1) bytes late and every field after the first landed at the wrong
# offset. Ground truth, Variables.inc: block base LevelInfo_PalettesTransferBuf
# = $6B7E, LevelInfo_StartRoomId = $6BAD -> in-block offset 47. With base
# PRG $193FC and stride 252, offset 47 reproduces the live-NES-SRAM
# StartRoomId for all nine dungeons (73 7d 7c 71 76 79 79 7e 76), and
# TriforceRoomId@48, SubmenuMapRotation@45 and LevelNumber@51 (= 1..9) all
# match too. With stride 256 none of them do.
#
# The block opens with its transfer-buffer descriptor ($3F00, len $20 — PPU
# palette RAM), which the 0x100 walk skipped entirely.
LEVEL_INFO_SIZE = 0x00FC

# ...but the SRAM region the runtime installs is 256 bytes ($6B7E..$6C7D per
# Variables.inc) and level_info_install.c copies 256 at a 256-byte blob stride.
# So advance the ROM walk by 252 (the real record size) while emitting 256
# bytes per block. The trailing 4 bytes are the next record's descriptor
# header, exactly what a 256-byte copy off contiguous ROM records yields; the
# last field the game reads is LevelInfo_DeathPaletteSeries ($6C5A = offset
# 220), so they are never consumed. This keeps the blob layout and the runtime
# unchanged while fixing the alignment.
LEVEL_INFO_EMIT = 0x0100
COMMON_DATA_SIZE = 0x008E

ROOM_LAYOUT_OW_SIZE = 0x0790
ROOM_LAYOUT_UW_SIZE = 0x0300

BANK_ISR_SIGNATURE = bytes(
    [
        0x78, 0xD8, 0xA9, 0x00, 0x8D, 0x00, 0x20,
        0xA2, 0xFF, 0x9A, 0xAD, 0x02, 0x20,
    ]
)

LEVEL_BLOCK_LABELS = [
    "LevelBlockOW",
    "LevelBlockUW1Q1",
    "LevelBlockUW2Q1",
    "LevelBlockUW1Q2",
    "LevelBlockUW2Q2",
]

LEVEL_INFO_LABELS = [
    "LevelInfoOW",
    "LevelInfoUW1",
    "LevelInfoUW2",
    "LevelInfoUW3",
    "LevelInfoUW4",
    "LevelInfoUW5",
    "LevelInfoUW6",
    "LevelInfoUW7",
    "LevelInfoUW8",
    "LevelInfoUW9",
]

OW_HEAP_LABELS = [
    "ColumnHeapOW0", "ColumnHeapOW1", "ColumnHeapOW2", "ColumnHeapOW3",
    "ColumnHeapOW4", "ColumnHeapOW5", "ColumnHeapOW6", "ColumnHeapOW7",
    "ColumnHeapOW8", "ColumnHeapOW9", "ColumnHeapOWA", "ColumnHeapOWB",
    "ColumnHeapOWC", "ColumnHeapOWD", "ColumnHeapOWE", "ColumnHeapOWF",
]

UW_HEAP_LABELS = [
    "ColumnHeapUW0", "ColumnHeapUW1", "ColumnHeapUW2", "ColumnHeapUW3",
    "ColumnHeapUW4", "ColumnHeapUW5", "ColumnHeapUW6", "ColumnHeapUW7",
    "ColumnHeapUW8", "ColumnHeapUW9",
]

Q2_REPLACEMENT_LABELS = [
    "LevelInfoUWQ2Replacements1",
    "LevelInfoUWQ2Replacements2",
    "LevelInfoUWQ2Replacements3",
    "LevelInfoUWQ2Replacements4",
    "LevelInfoUWQ2Replacements5",
    "LevelInfoUWQ2Replacements6",
    "LevelInfoUWQ2Replacements7",
    "LevelInfoUWQ2Replacements8",
    "LevelInfoUWQ2Replacements9",
]

Z05_TARGET_LABELS = {
    "RoomLayoutsOW",
    "RoomLayoutOWCave0",
    "RoomLayoutOWCave1",
    "RoomLayoutOWCave2",
    "RoomLayoutsUW",
    "RoomLayoutUWCellar0",
    "RoomLayoutUWCellar1",
    "ColumnHeapUWCellar",
    "ColumnDirectoryUW",
    *OW_HEAP_LABELS,
    *UW_HEAP_LABELS,
}

Z06_TARGET_LABELS = {
    "LevelBlockAttrsBQ2ReplacementOffsets",
    "LevelBlockAttrsBQ2ReplacementValues",
    "LevelInfoUWQ2ReplacementAddrs",
    "LevelInfoUWQ2ReplacementSizes",
    "ColumnDirectoryOW",
    *Q2_REPLACEMENT_LABELS,
}

INCBIN_SIZES = {
    "RoomLayoutsOW": ROOM_LAYOUT_OW_SIZE,
    "RoomLayoutsUW": ROOM_LAYOUT_UW_SIZE,
}

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")


# ---------------------------------------------------------------------------
# ROM / ASM parsing
# ---------------------------------------------------------------------------

def read_ines_rom(path: str) -> bytes:
    with open(path, "rb") as f:
        header = f.read(INES_HEADER_SIZE)
    if header[:4] != b"NES\x1a":
        raise ValueError(f"Not a valid iNES ROM: {path}")
    prg_banks = header[4]
    with open(path, "rb") as f:
        f.seek(INES_HEADER_SIZE)
        prg_data = f.read(prg_banks * PRG_BANK_SIZE)
    return prg_data


def strip_comment(line: str) -> str:
    return line.split(";", 1)[0].strip()


def parse_byte_token(token: str) -> int:
    token = token.strip()
    if not token:
        raise ValueError("Empty .BYTE token")
    if token.startswith("$"):
        return int(token[1:], 16)
    return int(token, 10)


def parse_asm_data_blocks(
    path: str,
    target_labels: set,
    incbin_sizes: dict | None = None,
) -> dict:
    if incbin_sizes is None:
        incbin_sizes = {}

    blocks: dict = {}
    current = None

    with open(path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = strip_comment(raw_line)
            if not line:
                if current is not None:
                    block = blocks[current]
                    if (
                        block["bytes"]
                        or block["addr_labels"]
                        or block["incbin_size"] is not None
                    ):
                        current = None
                continue

            label_match = LABEL_RE.match(line)
            if label_match:
                label = label_match.group(1)
                current = label if label in target_labels else None
                if current is not None and current not in blocks:
                    blocks[current] = {
                        "bytes": bytearray(),
                        "addr_labels": [],
                        "incbin_size": None,
                    }
                continue

            if current is None:
                continue

            if line.startswith(".BYTE"):
                items = [item.strip() for item in line[5:].split(",") if item.strip()]
                blocks[current]["bytes"].extend(parse_byte_token(item) for item in items)
            elif line.startswith(".ADDR"):
                items = [item.strip() for item in line[5:].split(",") if item.strip()]
                blocks[current]["addr_labels"].extend(items)
            elif line.startswith(".INCBIN"):
                if current not in incbin_sizes:
                    raise ValueError(f"Missing size for .INCBIN label {current}")
                blocks[current]["incbin_size"] = incbin_sizes[current]

    missing = sorted(label for label in target_labels if label not in blocks)
    if missing:
        raise ValueError(f"Missing expected labels in {path}: {', '.join(missing)}")

    return blocks


def find_unique_pattern(data: bytes, pattern: bytes, description: str) -> int:
    pos = data.find(pattern)
    if pos < 0:
        raise ValueError(f"Could not find {description}")
    second = data.find(pattern, pos + 1)
    if second >= 0:
        raise ValueError(
            f"{description} was not unique (found at {pos:#x} and {second:#x})"
        )
    return pos


def read_bank6_pointer(bank: bytes, pointer_offset: int, size: int) -> bytes:
    """Z_06 LevelInfoAddrs/CommonDataBlockAddr; copy size is independent of spacing."""
    if pointer_offset < 0 or pointer_offset + 2 > len(bank) or size <= 0:
        raise ValueError("Invalid bank-6 pointer entry or copy size")
    address = int.from_bytes(bank[pointer_offset:pointer_offset + 2], "little")
    start = address - 0x8000
    if start < 0 or start + size > len(bank):
        raise ValueError(f"Bank-6 pointer ${address:04X} exceeds record bounds")
    return bank[start:start + size]


def find_bank6_level_data(prg_data: bytes, z06_blocks: dict) -> dict:
    bank6_offset = 6 * PRG_BANK_SIZE
    bank6_data = prg_data[bank6_offset : bank6_offset + PRG_BANK_SIZE]

    isr_pos = bank6_data.find(BANK_ISR_SIGNATURE)
    if isr_pos < 0:
        raise ValueError("Could not find BANK_06_ISR signature")
    print(
        f"  Found BANK_06_ISR at bank 6 offset ${isr_pos:04X} "
        f"(ROM ${bank6_offset + isr_pos:05X})"
    )

    ff_run_start = None
    level_block_start = None
    for i in range(len(bank6_data) - 64):
        if all(byte == 0xFF for byte in bank6_data[i : i + 64]):
            end = i
            while end < len(bank6_data) and bank6_data[end] == 0xFF:
                end += 1
            ff_run_start = i
            level_block_start = end
            break

    if level_block_start is None:
        raise ValueError("Could not find padding before level block data")

    print(
        f"  Found bank 6 padding at ${ff_run_start:04X}-${level_block_start - 1:04X}; "
        f"level data starts at ${level_block_start:04X}"
    )

    level_blocks = {}
    offset = level_block_start
    for label in LEVEL_BLOCK_LABELS:
        rom_offset = bank6_offset + offset
        level_blocks[label] = prg_data[rom_offset : rom_offset + LEVEL_BLOCK_SIZE]
        print(f"    {label}: ROM ${rom_offset:05X}, {LEVEL_BLOCK_SIZE} bytes")
        offset += LEVEL_BLOCK_SIZE

    # Z_06.asm: LevelInfoAddrs starts at $8014, common pointer at $8028.
    # Records sit 252 bytes apart (LEVEL_INFO_SIZE); the runtime installs a
    # 256-byte copy (LEVEL_INFO_EMIT), exactly what the NES copies from each
    # pointer. Pointer-based read is layout-independent.
    level_infos = {}
    for level, label in enumerate(LEVEL_INFO_LABELS):
        level_infos[label] = read_bank6_pointer(bank6_data, 0x14 + level * 2,
                                                LEVEL_INFO_EMIT)
        print(f"    {label}: pointer entry ${0x8014 + level * 2:04X}, "
              f"record {LEVEL_INFO_SIZE} bytes, emit {LEVEL_INFO_EMIT}")

    common_data = read_bank6_pointer(bank6_data, 0x28, COMMON_DATA_SIZE)
    common_data_rom_offset = bank6_offset + int.from_bytes(bank6_data[0x28:0x2A], "little") - 0x8000
    print(
        f"    CommonDataBlock_Bank6: ROM ${common_data_rom_offset:05X}, "
        f"{COMMON_DATA_SIZE} bytes"
    )

    column_directory_ow = bytes(z06_blocks["ColumnDirectoryOW"]["bytes"])
    column_dir_pos = find_unique_pattern(
        bank6_data, column_directory_ow, "ColumnDirectoryOW in bank 6"
    )
    print(
        f"    ColumnDirectoryOW found at bank 6 offset ${column_dir_pos:04X} "
        f"(ROM ${bank6_offset + column_dir_pos:05X})"
    )

    q2_patch_pos = find_unique_pattern(
        bank6_data,
        bytes(z06_blocks["LevelBlockAttrsBQ2ReplacementOffsets"]["bytes"]),
        "Q2 room replacement offset table in bank 6",
    )
    print(
        f"    Q2 patch tables begin at bank 6 offset ${q2_patch_pos:04X} "
        f"(ROM ${bank6_offset + q2_patch_pos:05X})"
    )

    # LevelInfoUWQ2ReplacementAddrs (9 x .ADDR) sits immediately before
    # LevelInfoUWQ2ReplacementSizes in Z_06.asm.
    q2_sizes = bytes(z06_blocks["LevelInfoUWQ2ReplacementSizes"]["bytes"])
    q2_sizes_pos = find_unique_pattern(bank6_data, q2_sizes, "Q2 LevelInfo replacement sizes")
    q2_repl_addrs = bank6_data[q2_sizes_pos - 18:q2_sizes_pos]
    first = int.from_bytes(q2_repl_addrs[0:2], "little") - 0x8000
    if bank6_data[first:first + len(z06_blocks["LevelInfoUWQ2Replacements1"]["bytes"])] != bytes(
            z06_blocks["LevelInfoUWQ2Replacements1"]["bytes"]):
        raise ValueError("LevelInfoUWQ2ReplacementAddrs[0] does not point at Replacements1")

    return {
        "level_blocks": level_blocks,
        "level_infos": level_infos,
        "common_data": common_data,
        "q2_repl_addrs": q2_repl_addrs,
    }


def find_bank5_room_data(prg_data: bytes, z05_blocks: dict) -> dict:
    bank5_offset = 5 * PRG_BANK_SIZE
    bank5_data = prg_data[bank5_offset : bank5_offset + PRG_BANK_SIZE]

    cave0 = bytes(z05_blocks["RoomLayoutOWCave0"]["bytes"])
    cellar0 = bytes(z05_blocks["RoomLayoutUWCellar0"]["bytes"])

    cave0_pos = find_unique_pattern(bank5_data, cave0, "RoomLayoutOWCave0 in bank 5")
    cellar0_pos = find_unique_pattern(
        bank5_data, cellar0, "RoomLayoutUWCellar0 in bank 5"
    )

    room_layouts_ow_start = cave0_pos - ROOM_LAYOUT_OW_SIZE
    if room_layouts_ow_start < 0:
        raise ValueError("Invalid RoomLayoutsOW start in bank 5")

    uw_heap_total = sum(len(z05_blocks[label]["bytes"]) for label in UW_HEAP_LABELS)
    room_layouts_uw_start = cellar0_pos - uw_heap_total - ROOM_LAYOUT_UW_SIZE
    if room_layouts_uw_start < 0:
        raise ValueError("Invalid RoomLayoutsUW start in bank 5")

    print(
        f"  RoomLayoutsOW: bank 5 offset ${room_layouts_ow_start:04X} "
        f"(ROM ${bank5_offset + room_layouts_ow_start:05X})"
    )
    print(
        f"  RoomLayoutsUW: bank 5 offset ${room_layouts_uw_start:04X} "
        f"(ROM ${bank5_offset + room_layouts_uw_start:05X})"
    )

    room_layouts_ow = bank5_data[
        room_layouts_ow_start : room_layouts_ow_start + ROOM_LAYOUT_OW_SIZE
    ]
    room_layouts_uw = bank5_data[
        room_layouts_uw_start : room_layouts_uw_start + ROOM_LAYOUT_UW_SIZE
    ]

    ow_cave_layouts = {}
    offset = cave0_pos
    for label in ["RoomLayoutOWCave0", "RoomLayoutOWCave1", "RoomLayoutOWCave2"]:
        size = len(z05_blocks[label]["bytes"])
        data = bank5_data[offset : offset + size]
        expected = bytes(z05_blocks[label]["bytes"])
        if data != expected:
            raise ValueError(f"ROM mismatch for {label}")
        ow_cave_layouts[label] = data
        offset += size

    ow_heaps = {}
    ow_heap_offsets = []
    ow_heap_blob = bytearray()
    for label in OW_HEAP_LABELS:
        size = len(z05_blocks[label]["bytes"])
        data = bank5_data[offset : offset + size]
        expected = bytes(z05_blocks[label]["bytes"])
        if data != expected:
            raise ValueError(f"ROM mismatch for {label}")
        ow_heaps[label] = data
        ow_heap_offsets.append(len(ow_heap_blob))
        ow_heap_blob.extend(data)
        offset += size

    uw_heaps = {}
    offset = room_layouts_uw_start + ROOM_LAYOUT_UW_SIZE
    for label in UW_HEAP_LABELS:
        size = len(z05_blocks[label]["bytes"])
        data = bank5_data[offset : offset + size]
        expected = bytes(z05_blocks[label]["bytes"])
        if data != expected:
            raise ValueError(f"ROM mismatch for {label}")
        uw_heaps[label] = data
        offset += size

    if offset != cellar0_pos:
        raise ValueError(
            f"UW heap extraction drifted before cellar layouts: "
            f"expected ${cellar0_pos:04X}, got ${offset:04X}"
        )

    uw_cellar_layouts = {}
    for label in ["RoomLayoutUWCellar0", "RoomLayoutUWCellar1"]:
        size = len(z05_blocks[label]["bytes"])
        data = bank5_data[offset : offset + size]
        expected = bytes(z05_blocks[label]["bytes"])
        if data != expected:
            raise ValueError(f"ROM mismatch for {label}")
        uw_cellar_layouts[label] = data
        offset += size

    cellar_heap_size = len(z05_blocks["ColumnHeapUWCellar"]["bytes"])
    cellar_heap = bank5_data[offset : offset + cellar_heap_size]
    if cellar_heap != bytes(z05_blocks["ColumnHeapUWCellar"]["bytes"]):
        raise ValueError("ROM mismatch for ColumnHeapUWCellar")

    return {
        "room_layouts_ow": room_layouts_ow,
        "room_layouts_uw": room_layouts_uw,
        "ow_cave_layouts": ow_cave_layouts,
        "ow_heaps": ow_heaps,
        "ow_heap_blob": bytes(ow_heap_blob),
        "ow_heap_offsets": ow_heap_offsets,
        "uw_heaps": uw_heaps,
        "uw_cellar_layouts": uw_cellar_layouts,
        "uw_cellar_heap": cellar_heap,
    }


# ---------------------------------------------------------------------------
# C-array emission helpers (Phase B pattern)
# ---------------------------------------------------------------------------

def bytes_to_c_array(data: bytes, var_name: str, comment: str = "") -> str:
    n = len(data)
    header = "/* Auto-generated by tools/extract_rooms.py - do not edit. */"
    if comment:
        header += f"\n/* {comment} */"
    lines = [
        header,
        f"const unsigned long {var_name}_size = {n}UL;",
        f"const unsigned char {var_name}[{n}] = {{",
    ]
    for i in range(0, n, 16):
        chunk = data[i : i + 16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        comma = "" if (i + 16) >= n else ","
        lines.append(f"    {hex_vals}{comma}")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def write_c_file(path: str, data: bytes, var_name: str, comment: str = "") -> None:
    content = bytes_to_c_array(data, var_name, comment)
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(content)
    print(f"  Wrote {path} ({len(data)} bytes)")


# ---------------------------------------------------------------------------
# overworld.c: all OW data in one file
# ---------------------------------------------------------------------------

def build_overworld_blob(bank6_data: dict, bank5_data: dict) -> tuple:
    """Concatenate all overworld data into one blob; return (blob, blocks_info)."""
    blob = bytearray()
    blocks_info: list = []

    def add_block(name: str, data: bytes, note: str = "") -> None:
        entry: dict = {
            "category": "overworld",
            "name": name,
            "file": "overworld.c",
            "var_name": "rooms_overworld",
            "byte_offset": len(blob),
            "byte_size": len(data),
        }
        if note:
            entry["note"] = note
        blocks_info.append(entry)
        blob.extend(data)

    # Level block attributes (6 sub-tables x 128 rooms).
    ow_block = bank6_data["level_blocks"]["LevelBlockOW"]
    add_block("LevelBlockOW", ow_block, "6 attribute sub-tables x 128 rooms")

    # Overworld level info.
    add_block("LevelInfoOW", bank6_data["level_infos"]["LevelInfoOW"])

    # Common data block.
    add_block("CommonDataBlock_Bank6", bank6_data["common_data"])

    # Room layout descriptors (OW).
    add_block(
        "RoomLayoutsOW",
        bank5_data["room_layouts_ow"],
        f"{ROOM_LAYOUT_OW_SIZE} bytes, column-encoded format",
    )

    # Cave layouts.
    for label in ["RoomLayoutOWCave0", "RoomLayoutOWCave1", "RoomLayoutOWCave2"]:
        add_block(label, bank5_data["ow_cave_layouts"][label])

    # Overworld column heap blob (all heaps concatenated).
    add_block("ColumnHeapOWBlob", bank5_data["ow_heap_blob"])

    return bytes(blob), blocks_info


def write_overworld_c(out_dir: str, bank6_data: dict, bank5_data: dict) -> tuple:
    blob, blocks_info = build_overworld_blob(bank6_data, bank5_data)
    write_c_file(os.path.join(out_dir, "overworld.c"), blob, "rooms_overworld")
    return blob, blocks_info


# ---------------------------------------------------------------------------
# dungeons.c: all UW data in one file
# ---------------------------------------------------------------------------

def build_dungeons_blob(bank6_data: dict, bank5_data: dict, z06_blocks: dict) -> tuple:
    """Concatenate all dungeon data into one blob; return (blob, blocks_info)."""
    blob = bytearray()
    blocks_info: list = []

    def add_block(name: str, data: bytes, note: str = "") -> None:
        entry: dict = {
            "category": "dungeons",
            "name": name,
            "file": "dungeons.c",
            "var_name": "rooms_dungeons",
            "byte_offset": len(blob),
            "byte_size": len(data),
        }
        if note:
            entry["note"] = note
        blocks_info.append(entry)
        blob.extend(data)

    # UW level block attributes (Q1 + Q2, sets 1 and 2).
    for label in ["LevelBlockUW1Q1", "LevelBlockUW2Q1", "LevelBlockUW1Q2", "LevelBlockUW2Q2"]:
        add_block(label, bank6_data["level_blocks"][label])

    # UW level info blocks (dungeons 1-9).
    for label in LEVEL_INFO_LABELS[1:]:  # skip LevelInfoOW
        add_block(label, bank6_data["level_infos"][label])

    # Room layout descriptors (UW).
    add_block(
        "RoomLayoutsUW",
        bank5_data["room_layouts_uw"],
        f"{ROOM_LAYOUT_UW_SIZE} bytes, column-encoded format",
    )

    # Cellar layouts.
    for label in ["RoomLayoutUWCellar0", "RoomLayoutUWCellar1"]:
        add_block(label, bank5_data["uw_cellar_layouts"][label])

    # UW column heaps.
    for label in UW_HEAP_LABELS:
        add_block(label, bank5_data["uw_heaps"][label])

    # Cellar heap.
    add_block("ColumnHeapUWCellar", bank5_data["uw_cellar_heap"])

    # Q2 patch tables.
    add_block(
        "LevelBlockAttrsBQ2ReplacementOffsets",
        bytes(z06_blocks["LevelBlockAttrsBQ2ReplacementOffsets"]["bytes"]),
    )
    add_block(
        "LevelBlockAttrsBQ2ReplacementValues",
        bytes(z06_blocks["LevelBlockAttrsBQ2ReplacementValues"]["bytes"]),
    )
    add_block(
        "LevelInfoUWQ2ReplacementSizes",
        bytes(z06_blocks["LevelInfoUWQ2ReplacementSizes"]["bytes"]),
    )
    for label in Q2_REPLACEMENT_LABELS:
        add_block(label, bytes(z06_blocks[label]["bytes"]))
    # NES UpdateMode2Load_Full copies Sizes[L]+1 bytes (DEY/BPL from Sizes[L]
    # down to 0), i.e. one byte past each replacement array. Arrays are
    # contiguous in ROM, so that byte is the next array's first byte; for
    # level 9 it is the first byte of LevelInfoUWQ2ReplacementAddrs. Emit the
    # 18-byte pointer table (read from the ROM, it is .ADDR data) so the
    # runtime copy stays inside the blob and matches the NES byte-for-byte.
    add_block("LevelInfoUWQ2ReplacementAddrs", bank6_data["q2_repl_addrs"])

    return bytes(blob), blocks_info


def write_dungeons_c(
    out_dir: str, bank6_data: dict, bank5_data: dict, z06_blocks: dict
) -> tuple:
    blob, blocks_info = build_dungeons_blob(bank6_data, bank5_data, z06_blocks)
    write_c_file(os.path.join(out_dir, "dungeons.c"), blob, "rooms_dungeons")
    return blob, blocks_info


# ---------------------------------------------------------------------------
# MANIFEST.json
# ---------------------------------------------------------------------------

def build_manifest(
    overworld_info: list,
    dungeons_info: list,
    ow_heap_offsets: list,
) -> dict:
    manifest: dict = {
        "schema_version": 1,
        "nes_rom_sha256": NES_ROM_SHA256,
        "overworld": overworld_info,
        "dungeons": dungeons_info,
        "ow_heap_offsets": ow_heap_offsets,
    }
    return manifest


def write_manifest(path: str, manifest: dict) -> None:
    with open(path, "w", encoding="ascii", newline="\n") as f:
        json.dump(manifest, f, indent=2, sort_keys=False, ensure_ascii=True)
        f.write("\n")
    ow_count = len(manifest["overworld"])
    dg_count = len(manifest["dungeons"])
    print(f"  Wrote {path} ({ow_count} overworld + {dg_count} dungeon block entries)")


# ---------------------------------------------------------------------------
# Legacy .inc emission helpers
# ---------------------------------------------------------------------------

def data_to_inc_bytes(data: bytes, label: str, bytes_per_line: int = 16) -> str:
    lines = [f"; {label} - {len(data)} bytes", f"{label}:"]
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        lines.append("    dc.b " + ",".join(f"${byte:02X}" for byte in chunk))
    return "\n".join(lines)


def labels_to_inc_words(label_names: list, table_label: str, words_per_line: int = 4) -> str:
    lines = [f"{table_label}:"]
    for i in range(0, len(label_names), words_per_line):
        chunk = label_names[i : i + words_per_line]
        lines.append("    dc.w " + ",".join(chunk))
    return "\n".join(lines)


def values_to_inc_words(values: list, table_label: str, words_per_line: int = 8) -> str:
    lines = [f"{table_label}:"]
    for i in range(0, len(values), words_per_line):
        chunk = values[i : i + words_per_line]
        lines.append("    dc.w " + ",".join(f"${value:04X}" for value in chunk))
    return "\n".join(lines)


def write_text_file(path: str, lines: list) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"  Wrote {path}")


def write_room_attr_files_inc(data_dir: str, level_blocks: dict) -> None:
    ow_block = level_blocks["LevelBlockOW"]
    lines = [
        "; Overworld room attributes (6 tables x 128 rooms)",
        "; Auto-generated by extract_rooms.py - DO NOT EDIT",
        "; Each table is 128 bytes, indexed by room ID (row*16 + col)",
        "",
        "; Byte A: outer palette, exit X, door bits (S/N)",
        data_to_inc_bytes(ow_block[0x000:0x080], "RoomAttrsOW_A"),
        "",
        "; Byte B: inner palette, cave index, door bits (E/W)",
        data_to_inc_bytes(ow_block[0x080:0x100], "RoomAttrsOW_B"),
        "",
        "; Byte C: monster list ID (low 6 bits)",
        data_to_inc_bytes(ow_block[0x100:0x180], "RoomAttrsOW_C"),
        "",
        "; Byte D: unique room ID low 6 bits, push-block flag, specials",
        data_to_inc_bytes(ow_block[0x180:0x200], "RoomAttrsOW_D"),
        "",
        "; Byte E: sound effect / item metadata",
        data_to_inc_bytes(ow_block[0x200:0x280], "RoomAttrsOW_E"),
        "",
        "; Byte F: secret trigger, underground exit row, edge spawning",
        data_to_inc_bytes(ow_block[0x280:0x300], "RoomAttrsOW_F"),
    ]
    write_text_file(os.path.join(data_dir, "rooms_overworld.inc"), lines)

    for suffix, q1_key, q2_key in [
        ("1", "LevelBlockUW1Q1", "LevelBlockUW1Q2"),
        ("2", "LevelBlockUW2Q1", "LevelBlockUW2Q2"),
    ]:
        q1_block = level_blocks[q1_key]
        q2_block = level_blocks[q2_key]
        range_label = "1-6" if suffix == "1" else "7-9"
        lines = [
            f"; Underworld room attributes set {suffix}",
            "; Auto-generated by extract_rooms.py - DO NOT EDIT",
            "",
            f"; Quest 1 (levels {range_label})",
            data_to_inc_bytes(q1_block[0x000:0x080], f"RoomAttrsUW{suffix}Q1_A"),
            "",
            data_to_inc_bytes(q1_block[0x080:0x100], f"RoomAttrsUW{suffix}Q1_B"),
            "",
            data_to_inc_bytes(q1_block[0x100:0x180], f"RoomAttrsUW{suffix}Q1_C"),
            "",
            data_to_inc_bytes(q1_block[0x180:0x200], f"RoomAttrsUW{suffix}Q1_D"),
            "",
            data_to_inc_bytes(q1_block[0x200:0x280], f"RoomAttrsUW{suffix}Q1_E"),
            "",
            data_to_inc_bytes(q1_block[0x280:0x300], f"RoomAttrsUW{suffix}Q1_F"),
            "",
            f"; Quest 2 (levels {range_label})",
            data_to_inc_bytes(q2_block[0x000:0x080], f"RoomAttrsUW{suffix}Q2_A"),
            "",
            data_to_inc_bytes(q2_block[0x080:0x100], f"RoomAttrsUW{suffix}Q2_B"),
            "",
            data_to_inc_bytes(q2_block[0x100:0x180], f"RoomAttrsUW{suffix}Q2_C"),
            "",
            data_to_inc_bytes(q2_block[0x180:0x200], f"RoomAttrsUW{suffix}Q2_D"),
            "",
            data_to_inc_bytes(q2_block[0x200:0x280], f"RoomAttrsUW{suffix}Q2_E"),
            "",
            data_to_inc_bytes(q2_block[0x280:0x300], f"RoomAttrsUW{suffix}Q2_F"),
        ]
        write_text_file(os.path.join(data_dir, f"rooms_underworld{suffix}.inc"), lines)


def write_level_info_inc(data_dir: str, level_infos: dict) -> None:
    lines = [
        "; Level info blocks (256 bytes each)",
        "; Auto-generated by extract_rooms.py - DO NOT EDIT",
        "",
    ]
    for label in LEVEL_INFO_LABELS:
        lines.append(data_to_inc_bytes(level_infos[label], label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "level_info.inc"), lines[:-1])


def write_room_layouts_inc(data_dir: str, bank5_data: dict) -> None:
    lines = [
        "; Room layout descriptor tables",
        "; Auto-generated by extract_rooms.py - DO NOT EDIT",
        "; Overworld unique rooms use 16 column descriptors each",
        "; Underworld unique rooms use 12 column descriptors each",
        "",
        data_to_inc_bytes(bank5_data["room_layouts_ow"], "RoomLayoutsOW"),
        "",
    ]
    for label in ["RoomLayoutOWCave0", "RoomLayoutOWCave1", "RoomLayoutOWCave2"]:
        lines.append(data_to_inc_bytes(bank5_data["ow_cave_layouts"][label], label))
        lines.append("")
    lines.append(data_to_inc_bytes(bank5_data["room_layouts_uw"], "RoomLayoutsUW"))
    lines.append("")
    for label in ["RoomLayoutUWCellar0", "RoomLayoutUWCellar1"]:
        lines.append(data_to_inc_bytes(bank5_data["uw_cellar_layouts"][label], label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "room_layouts.inc"), lines[:-1])


def write_room_columns_inc(data_dir: str, bank5_data: dict, z05_blocks: dict) -> None:
    uw_directory_labels = z05_blocks["ColumnDirectoryUW"]["addr_labels"]
    ow_directory_exprs = [
        f"ColumnHeapOWBlob+${offset:04X}" for offset in bank5_data["ow_heap_offsets"]
    ]
    lines = [
        "; Column directories and compressed column heaps",
        "; Auto-generated by extract_rooms.py - DO NOT EDIT",
        "",
        "; Overworld directory preserved as offsets into one continuous heap blob",
        values_to_inc_words(bank5_data["ow_heap_offsets"], "ColumnDirectoryOWOffsets"),
        "",
        labels_to_inc_words(ow_directory_exprs, "ColumnDirectoryOW"),
        "",
        data_to_inc_bytes(bank5_data["ow_heap_blob"], "ColumnHeapOWBlob"),
        "",
        labels_to_inc_words(uw_directory_labels, "ColumnDirectoryUW"),
        "",
    ]
    for label in UW_HEAP_LABELS:
        lines.append(data_to_inc_bytes(bank5_data["uw_heaps"][label], label))
        lines.append("")
    lines.append(data_to_inc_bytes(bank5_data["uw_cellar_heap"], "ColumnHeapUWCellar"))
    write_text_file(os.path.join(data_dir, "room_columns.inc"), lines)


def write_room_common_inc(data_dir: str, common_data: bytes) -> None:
    lines = [
        "; Common room data copied by bank 6 loader",
        "; Auto-generated by extract_rooms.py - DO NOT EDIT",
        "",
        data_to_inc_bytes(common_data, "CommonDataBlock_Bank6"),
    ]
    write_text_file(os.path.join(data_dir, "room_common.inc"), lines)


def write_room_patches_inc(data_dir: str, z06_blocks: dict) -> None:
    lines = [
        "; Second quest room/data patch tables",
        "; Auto-generated by extract_rooms.py - DO NOT EDIT",
        "",
        data_to_inc_bytes(
            bytes(z06_blocks["LevelBlockAttrsBQ2ReplacementOffsets"]["bytes"]),
            "LevelBlockAttrsBQ2ReplacementOffsets",
        ),
        "",
        data_to_inc_bytes(
            bytes(z06_blocks["LevelBlockAttrsBQ2ReplacementValues"]["bytes"]),
            "LevelBlockAttrsBQ2ReplacementValues",
        ),
        "",
        labels_to_inc_words(Q2_REPLACEMENT_LABELS, "LevelInfoUWQ2ReplacementAddrs"),
        "",
        data_to_inc_bytes(
            bytes(z06_blocks["LevelInfoUWQ2ReplacementSizes"]["bytes"]),
            "LevelInfoUWQ2ReplacementSizes",
        ),
        "",
    ]
    for label in Q2_REPLACEMENT_LABELS:
        lines.append(data_to_inc_bytes(bytes(z06_blocks[label]["bytes"]), label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "room_patches.inc"), lines[:-1])


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract NES Zelda room data and emit Genesis-ready C arrays."
    )
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Output directory for data/rooms/ files. Default: <repo>/data/rooms/",
    )
    parser.add_argument(
        "--legacy-inc",
        action="store_true",
        default=False,
        help="Also emit legacy .inc files (vasm format) alongside C arrays.",
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    if args.out_dir is not None:
        out_dir = os.path.abspath(args.out_dir)
    else:
        out_dir = os.path.join(project_root, "data", "rooms")

    legacy_dir = os.path.join(project_root, "src", "data")

    rom_path_env = os.environ.get("ZELDA_NES_ROM", "")
    if rom_path_env and os.path.isfile(rom_path_env):
        rom_path = rom_path_env
    else:
        rom_path = os.path.join(project_root, "Legend of Zelda, The (USA).nes")

    z05_path = os.path.join(project_root, "reference", "aldonunez", "Z_05.asm")
    z06_path = os.path.join(project_root, "reference", "aldonunez", "Z_06.asm")

    for required_path in [rom_path, z05_path, z06_path]:
        if not os.path.exists(required_path):
            print(f"ERROR: required file not found: {required_path}")
            sys.exit(1)

    # Verify ROM SHA256.
    print(f"Reading NES ROM: {rom_path}")
    h = hashlib.sha256()
    with open(rom_path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    actual_sha256 = h.hexdigest()
    if actual_sha256.lower() != NES_ROM_SHA256.lower():
        print(f"WARNING: ROM SHA256 mismatch!")
        print(f"  Expected: {NES_ROM_SHA256}")
        print(f"  Actual:   {actual_sha256}")

    prg_data = read_ines_rom(rom_path)

    print(f"Parsing reference data: {z05_path}")
    z05_blocks = parse_asm_data_blocks(z05_path, Z05_TARGET_LABELS, INCBIN_SIZES)

    print(f"Parsing reference data: {z06_path}")
    z06_blocks = parse_asm_data_blocks(z06_path, Z06_TARGET_LABELS)

    print("\nExtracting bank 6 level/common data...")
    bank6_data = find_bank6_level_data(prg_data, z06_blocks)

    print("\nExtracting bank 5 layout/column data...")
    bank5_data = find_bank5_room_data(prg_data, z05_blocks)

    os.makedirs(out_dir, exist_ok=True)

    print("\nWriting C room data files...")
    _ow_blob, overworld_info = write_overworld_c(out_dir, bank6_data, bank5_data)
    _dg_blob, dungeons_info = write_dungeons_c(out_dir, bank6_data, bank5_data, z06_blocks)

    manifest = build_manifest(
        overworld_info,
        dungeons_info,
        bank5_data["ow_heap_offsets"],
    )
    write_manifest(os.path.join(out_dir, "MANIFEST.json"), manifest)

    if args.legacy_inc:
        print("\nEmitting legacy .inc files...")
        os.makedirs(legacy_dir, exist_ok=True)
        write_room_attr_files_inc(data_dir=legacy_dir, level_blocks=bank6_data["level_blocks"])
        write_level_info_inc(legacy_dir, bank6_data["level_infos"])
        write_room_layouts_inc(legacy_dir, bank5_data)
        write_room_columns_inc(legacy_dir, bank5_data, z05_blocks)
        write_room_common_inc(legacy_dir, bank6_data["common_data"])
        write_room_patches_inc(legacy_dir, z06_blocks)

    total_bytes = (
        sum(len(data) for data in bank6_data["level_blocks"].values())
        + sum(len(data) for data in bank6_data["level_infos"].values())
        + len(bank6_data["common_data"])
        + len(bank5_data["room_layouts_ow"])
        + len(bank5_data["room_layouts_uw"])
        + sum(len(data) for data in bank5_data["ow_cave_layouts"].values())
        + sum(len(data) for data in bank5_data["ow_heaps"].values())
        + sum(len(data) for data in bank5_data["uw_heaps"].values())
        + sum(len(data) for data in bank5_data["uw_cellar_layouts"].values())
        + len(bank5_data["uw_cellar_heap"])
        + len(z06_blocks["LevelBlockAttrsBQ2ReplacementOffsets"]["bytes"])
        + len(z06_blocks["LevelBlockAttrsBQ2ReplacementValues"]["bytes"])
        + len(z06_blocks["LevelInfoUWQ2ReplacementSizes"]["bytes"])
        + sum(len(z06_blocks[label]["bytes"]) for label in Q2_REPLACEMENT_LABELS)
    )

    print("\n=== Room extraction complete ===")
    print(f"  Total extracted room data: {total_bytes} bytes")
    print(f"  Output directory: {out_dir}")


if __name__ == "__main__":
    main()
