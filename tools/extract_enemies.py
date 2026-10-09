#!/usr/bin/env python3
"""
extract_enemies.py - Extract enemy/object dispatch and drop tables to Genesis C arrays.

S2 Phase C3: ports the old .inc emission to Genesis-ready C-array output under
data/enemies/.

This stage pulls the NES game's object-type metadata out of Z_04/Z_07 and
emits Genesis-friendly C arrays:
  - tables.c: object attributes, HP, behavior-dispatch IDs, drop tables,
              and room object-list templates
  - MANIFEST.json: schema_version + nes_rom_sha256 + per-block metadata

The behavior tables are emitted as local numeric IDs rather than raw NES
routine pointers so the Genesis engine can switch on compact self-contained
tables without needing unresolved 6502 labels.

Legacy outputs (--legacy-inc flag only):
  src/data/enemy_tables.inc, drop_tables.inc, object_lists.inc
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
BANK_CPU_BASE = 0x8000

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")

Z07_TARGET_LABELS = {
    "SpecialBossPaletteObjTypes",
    "ObjectTypeToAttributes",
    "ObjectTypeToHpPairs",
    "UpdateObject_JumpTable",
    "InitObject_JumpTable",
}

Z04_TARGET_LABELS = {
    "NoDropMonsterTypes",
    "DropItemMonsterTypes0",
    "DropItemMonsterTypes1",
    "DropItemMonsterTypes2",
    "DropItemSetBaseOffsets",
    "DropItemRates",
    "DropItemTable",
}

Z05_TARGET_LABELS = {
    "EnteringRoomRelativePositions",
}

INITMODE4_SIGNATURE = bytes([0xA6, 0x13, 0xF0, 0x47, 0xCA])


# ---------------------------------------------------------------------------
# ROM / ASM parsing
# ---------------------------------------------------------------------------

def strip_comment(line: str) -> str:
    return line.split(";", 1)[0].strip()


def parse_byte_token(token: str) -> int:
    token = token.strip()
    if not token:
        raise ValueError("Empty .BYTE token")
    if token.startswith("$"):
        return int(token[1:], 16)
    return int(token, 10)


def read_ines_prg(path: str) -> bytes:
    with open(path, "rb") as f:
        header = f.read(INES_HEADER_SIZE)
        if header[:4] != b"NES\x1A":
            raise ValueError(f"Not a valid iNES ROM: {path}")
        prg_banks = header[4]
        prg_data = f.read(prg_banks * PRG_BANK_SIZE)
    return prg_data


def parse_asm_data_blocks(path: str, target_labels: set) -> dict:
    blocks: dict = {}
    current = None

    with open(path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = strip_comment(raw_line)
            if not line:
                if current is not None:
                    block = blocks[current]
                    if block["bytes"] or block["addr_labels"]:
                        current = None
                continue

            match = LABEL_RE.match(line)
            if match:
                label = match.group(1)
                current = label if label in target_labels else None
                if current is not None and current not in blocks:
                    blocks[current] = {"bytes": bytearray(), "addr_labels": []}
                continue

            if current is None:
                continue

            if line.startswith(".BYTE"):
                items = [item.strip() for item in line[5:].split(",") if item.strip()]
                blocks[current]["bytes"].extend(parse_byte_token(item) for item in items)
            elif line.startswith(".ADDR"):
                items = [item.strip() for item in line[5:].split(",") if item.strip()]
                blocks[current]["addr_labels"].extend(items)

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


def cpu_addr_to_bank_offset(cpu_addr: int) -> int:
    if cpu_addr < BANK_CPU_BASE or cpu_addr >= BANK_CPU_BASE + PRG_BANK_SIZE:
        raise ValueError(f"CPU address ${cpu_addr:04X} is outside bank range")
    return cpu_addr - BANK_CPU_BASE


def parse_le_words(data: bytes) -> list:
    if len(data) & 1:
        raise ValueError("Word table has odd length")
    return [data[i] | (data[i + 1] << 8) for i in range(0, len(data), 2)]


def find_pointer_table_start(bank_data: bytes, table_end: int, minimum_offset: int) -> int:
    start = table_end
    previous = None
    while start - 2 >= minimum_offset:
        word = bank_data[start - 2] | (bank_data[start - 1] << 8)
        if word < BANK_CPU_BASE or word >= BANK_CPU_BASE + PRG_BANK_SIZE:
            break
        offset = cpu_addr_to_bank_offset(word)
        if offset < minimum_offset or offset >= start - 2:
            break
        if previous is not None and word > previous:
            break
        previous = word
        start -= 2
    if start == table_end:
        raise ValueError("Could not locate object list pointer table")
    return start


def build_pointer_labels(addresses: list, prefix: str) -> tuple:
    labels_by_addr: dict = {}
    table_labels = []
    for addr in addresses:
        if addr not in labels_by_addr:
            labels_by_addr[addr] = f"{prefix}{len(labels_by_addr):02d}"
        table_labels.append(labels_by_addr[addr])
    ordered_addrs = sorted(labels_by_addr)
    return labels_by_addr, table_labels, ordered_addrs


def extract_object_lists(prg_data: bytes, z05_blocks: dict) -> dict:
    bank5_data = prg_data[5 * PRG_BANK_SIZE : 6 * PRG_BANK_SIZE]
    entering_positions = bytes(z05_blocks["EnteringRoomRelativePositions"]["bytes"])
    marker_pos = find_unique_pattern(
        bank5_data, entering_positions, "EnteringRoomRelativePositions in bank 5"
    )
    obj_lists_start = marker_pos + len(entering_positions)

    init_mode4_pos = (
        find_unique_pattern(
            bank5_data[obj_lists_start:],
            INITMODE4_SIGNATURE,
            "InitMode4 signature in bank 5",
        )
        + obj_lists_start
    )

    obj_list_addrs_start = find_pointer_table_start(
        bank5_data, init_mode4_pos, obj_lists_start
    )

    return {
        "obj_lists": bytes(bank5_data[obj_lists_start:obj_list_addrs_start]),
        "obj_list_addrs": parse_le_words(bank5_data[obj_list_addrs_start:init_mode4_pos]),
    }


def ordered_unique(items: list) -> list:
    result = []
    seen: set = set()
    for item in items:
        if item not in seen:
            seen.add(item)
            result.append(item)
    return result


def unpack_hp_pairs(hp_pairs: bytes, object_count: int) -> bytes:
    unpacked = []
    for obj_type in range(object_count):
        pair_value = hp_pairs[obj_type // 2]
        if obj_type & 1:
            unpacked.append((pair_value & 0x0F) << 4)
        else:
            unpacked.append(pair_value & 0xF0)
    return bytes(unpacked)


# ---------------------------------------------------------------------------
# C-array emission helpers (Phase B pattern)
# ---------------------------------------------------------------------------

def bytes_to_c_array(data: bytes, var_name: str) -> str:
    n = len(data)
    lines = [
        f"/* Auto-generated by tools/extract_enemies.py - do not edit. */",
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


def write_c_file(path: str, data: bytes, var_name: str) -> None:
    content = bytes_to_c_array(data, var_name)
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(content)
    print(f"  Wrote {path} ({len(data)} bytes)")


# ---------------------------------------------------------------------------
# tables.c: all enemy/drop/object-list data concatenated
# ---------------------------------------------------------------------------

def build_tables_blob(
    z07_blocks: dict,
    z04_blocks: dict,
    object_list_data: dict,
) -> tuple:
    """Concatenate all enemy and drop tables; return (blob, blocks_info)."""
    blob = bytearray()
    blocks_info: list = []

    def add_block(name: str, data: bytes, note: str = "") -> None:
        entry: dict = {
            "category": "tables",
            "name": name,
            "file": "tables.c",
            "var_name": "enemies_tables",
            "byte_offset": len(blob),
            "byte_size": len(data),
        }
        if note:
            entry["note"] = note
        blocks_info.append(entry)
        blob.extend(data)

    attrs = bytes(z07_blocks["ObjectTypeToAttributes"]["bytes"])
    hp_pairs = bytes(z07_blocks["ObjectTypeToHpPairs"]["bytes"])
    boss_palette_types = bytes(z07_blocks["SpecialBossPaletteObjTypes"]["bytes"])
    update_handlers = z07_blocks["UpdateObject_JumpTable"]["addr_labels"]
    init_handlers = z07_blocks["InitObject_JumpTable"]["addr_labels"]

    hp_object_count = len(hp_pairs) * 2
    init_object_count = len(init_handlers)
    update_object_count = len(update_handlers)

    if len(attrs) != init_object_count:
        raise ValueError("Attribute table length does not match init table length")

    unpacked_hp = unpack_hp_pairs(hp_pairs, hp_object_count)
    unique_init = ordered_unique(init_handlers)
    unique_update = ordered_unique(update_handlers)

    add_block("ObjectTypeToAttributes", attrs)
    add_block("ObjectTypeToHpPairs", hp_pairs)
    add_block("ObjectTypeToHP", unpacked_hp, "unpacked from HP pairs")
    add_block("SpecialBossPaletteObjTypes", boss_palette_types)

    # Emit init-handler IDs as a byte table (index = object type, value = handler id).
    init_id_map = {h: i for i, h in enumerate(unique_init)}
    init_id_table = bytes(init_id_map[h] for h in init_handlers)
    add_block(
        "ObjectTypeToInitHandlerId",
        init_id_table,
        f"{len(unique_init)} unique init handlers",
    )

    # Emit update-handler IDs as a byte table.
    update_id_map = {h: i for i, h in enumerate(unique_update)}
    update_id_table = bytes(update_id_map[h] for h in update_handlers)
    add_block(
        "ObjectTypeToUpdateHandlerId",
        update_id_table,
        f"{len(unique_update)} unique update handlers",
    )

    # Drop tables.
    for label in [
        "NoDropMonsterTypes",
        "DropItemMonsterTypes0",
        "DropItemMonsterTypes1",
        "DropItemMonsterTypes2",
        "DropItemSetBaseOffsets",
        "DropItemRates",
        "DropItemTable",
    ]:
        add_block(label, bytes(z04_blocks[label]["bytes"]))

    # Object lists blob.
    add_block(
        "ObjListBlob",
        object_list_data["obj_lists"],
        f"{len(object_list_data['obj_list_addrs'])} room object lists",
    )

    # Object list addresses as little-endian word pairs stored as raw bytes.
    obj_list_addr_bytes = bytearray()
    for addr in object_list_data["obj_list_addrs"]:
        obj_list_addr_bytes.append(addr & 0xFF)
        obj_list_addr_bytes.append((addr >> 8) & 0xFF)
    add_block(
        "ObjListAddrs",
        bytes(obj_list_addr_bytes),
        "NES CPU addresses, LE word pairs",
    )

    # Attach derived counts to blocks_info for MANIFEST.
    for entry in blocks_info:
        if entry["name"] == "ObjectTypeToAttributes":
            entry["init_object_count"] = init_object_count
            entry["update_object_count"] = update_object_count
            entry["hp_object_count"] = hp_object_count

    return bytes(blob), blocks_info, unique_init, unique_update


def write_tables_c(
    out_dir: str,
    z07_blocks: dict,
    z04_blocks: dict,
    object_list_data: dict,
) -> tuple:
    blob, blocks_info, unique_init, unique_update = build_tables_blob(
        z07_blocks, z04_blocks, object_list_data
    )
    write_c_file(os.path.join(out_dir, "tables.c"), blob, "enemies_tables")
    return blob, blocks_info, unique_init, unique_update


# ---------------------------------------------------------------------------
# MANIFEST.json
# ---------------------------------------------------------------------------

def build_manifest(blocks_info: list, unique_init: list, unique_update: list) -> dict:
    manifest: dict = {
        "schema_version": 1,
        "nes_rom_sha256": NES_ROM_SHA256,
        "tables": blocks_info,
        "init_handler_names": unique_init,
        "update_handler_names": unique_update,
    }
    return manifest


def write_manifest(path: str, manifest: dict) -> None:
    with open(path, "w", encoding="ascii", newline="\n") as f:
        json.dump(manifest, f, indent=2, sort_keys=False, ensure_ascii=True)
        f.write("\n")
    count = len(manifest["tables"])
    print(f"  Wrote {path} ({count} table block entries)")


# ---------------------------------------------------------------------------
# Legacy .inc emission helpers
# ---------------------------------------------------------------------------

def data_to_inc_bytes(data: bytes, label: str, bytes_per_line: int = 16) -> str:
    lines = [f"; {label} - {len(data)} bytes", f"{label}:"]
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        lines.append("    dc.b " + ",".join(f"${byte:02X}" for byte in chunk))
    return "\n".join(lines)


def labels_to_inc_words_legacy(label_names: list, table_label: str, words_per_line: int = 4) -> str:
    lines = [f"; {table_label} - {len(label_names)} entries", f"{table_label}:"]
    for i in range(0, len(label_names), words_per_line):
        chunk = label_names[i : i + words_per_line]
        lines.append("    dc.w " + ",".join(chunk))
    return "\n".join(lines)


def enum_table_to_inc(
    enum_prefix: str,
    ids_label: str,
    handler_labels: list,
    table_entries: list,
    bytes_per_line: int = 8,
) -> str:
    lines = [f"; {ids_label}"]
    for index, handler in enumerate(handler_labels):
        lines.append(f"{enum_prefix}{handler} equ {index}")
    lines.append("")
    lines.append(f"{ids_label}:")
    for i in range(0, len(table_entries), bytes_per_line):
        chunk = table_entries[i : i + bytes_per_line]
        lines.append(
            "    dc.b " + ",".join(f"{enum_prefix}{handler}" for handler in chunk)
        )
    return "\n".join(lines)


def write_labeled_records(
    lines: list,
    blob: bytes,
    ordered_addrs: list,
    labels_by_addr: dict,
    label_prefix: str,
) -> None:
    offsets = [cpu_addr_to_bank_offset(addr) for addr in ordered_addrs]
    for index, addr in enumerate(ordered_addrs):
        start = offsets[index] - cpu_addr_to_bank_offset(ordered_addrs[0])
        if index + 1 < len(offsets):
            end = offsets[index + 1] - cpu_addr_to_bank_offset(ordered_addrs[0])
        else:
            end = len(blob)
        label = labels_by_addr[addr]
        lines.append(f"; {label} - {end - start} bytes")
        lines.append(f"{label}:")
        data = blob[start:end]
        for i in range(0, len(data), 16):
            chunk = data[i : i + 16]
            lines.append("    dc.b " + ",".join(f"${byte:02X}" for byte in chunk))
        lines.append("")


def write_text_file(path: str, lines: list) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"  Wrote {path}")


def write_enemy_tables_inc(data_dir: str, z07_blocks: dict) -> None:
    attrs = bytes(z07_blocks["ObjectTypeToAttributes"]["bytes"])
    hp_pairs = bytes(z07_blocks["ObjectTypeToHpPairs"]["bytes"])
    update_handlers = z07_blocks["UpdateObject_JumpTable"]["addr_labels"]
    init_handlers = z07_blocks["InitObject_JumpTable"]["addr_labels"]
    boss_palette_types = bytes(z07_blocks["SpecialBossPaletteObjTypes"]["bytes"])

    init_object_count = len(init_handlers)
    update_object_count = len(update_handlers)
    hp_object_count = len(hp_pairs) * 2

    unpacked_hp = unpack_hp_pairs(hp_pairs, hp_object_count)
    unique_init = ordered_unique(init_handlers)
    unique_update = ordered_unique(update_handlers)

    lines = [
        "; Enemy/object metadata extracted from NES Zelda",
        "; Auto-generated by extract_enemies.py - DO NOT EDIT",
        "",
        f"ObjectTypeCount_Hp equ {hp_object_count}",
        f"ObjectTypeCount_Init equ {init_object_count}",
        f"ObjectTypeCount_Update equ {update_object_count}",
        "",
        data_to_inc_bytes(attrs, "ObjectTypeToAttributes"),
        "",
        data_to_inc_bytes(hp_pairs, "ObjectTypeToHpPairs"),
        "",
        data_to_inc_bytes(unpacked_hp, "ObjectTypeToHP"),
        "",
        data_to_inc_bytes(boss_palette_types, "SpecialBossPaletteObjTypes"),
        "",
        enum_table_to_inc(
            "EnemyInitHandler_",
            "ObjectTypeToInitHandlerId",
            unique_init,
            init_handlers,
        ),
        "",
        enum_table_to_inc(
            "EnemyUpdateHandler_",
            "ObjectTypeToUpdateHandlerId",
            unique_update,
            update_handlers,
        ),
    ]
    write_text_file(os.path.join(data_dir, "enemy_tables.inc"), lines)


def write_drop_tables_inc(data_dir: str, z04_blocks: dict) -> None:
    lines = [
        "; Monster drop tables extracted from NES Zelda",
        "; Auto-generated by extract_enemies.py - DO NOT EDIT",
        "",
        data_to_inc_bytes(
            bytes(z04_blocks["NoDropMonsterTypes"]["bytes"]), "NoDropMonsterTypes"
        ),
        "",
        data_to_inc_bytes(
            bytes(z04_blocks["DropItemMonsterTypes0"]["bytes"]), "DropItemMonsterTypes0"
        ),
        "",
        data_to_inc_bytes(
            bytes(z04_blocks["DropItemMonsterTypes1"]["bytes"]), "DropItemMonsterTypes1"
        ),
        "",
        data_to_inc_bytes(
            bytes(z04_blocks["DropItemMonsterTypes2"]["bytes"]), "DropItemMonsterTypes2"
        ),
        "",
        data_to_inc_bytes(
            bytes(z04_blocks["DropItemSetBaseOffsets"]["bytes"]), "DropItemSetBaseOffsets"
        ),
        "",
        data_to_inc_bytes(bytes(z04_blocks["DropItemRates"]["bytes"]), "DropItemRates"),
        "",
        data_to_inc_bytes(bytes(z04_blocks["DropItemTable"]["bytes"]), "DropItemTable"),
    ]
    write_text_file(os.path.join(data_dir, "drop_tables.inc"), lines)


def write_object_lists_inc(data_dir: str, object_list_data: dict) -> None:
    labels_by_addr, table_labels, ordered_addrs = build_pointer_labels(
        object_list_data["obj_list_addrs"], "ObjList"
    )

    lines = [
        "; Room object list templates extracted from NES Zelda bank 5",
        "; Auto-generated by extract_enemies.py - DO NOT EDIT",
        "",
        labels_to_inc_words_legacy(table_labels, "ObjListAddrs"),
        "",
    ]

    write_labeled_records(
        lines,
        object_list_data["obj_lists"],
        ordered_addrs,
        labels_by_addr,
        "ObjList",
    )

    write_text_file(os.path.join(data_dir, "object_lists.inc"), lines[:-1])


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract NES Zelda enemy/drop tables and emit Genesis-ready C arrays."
    )
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Output directory for data/enemies/ files. Default: <repo>/data/enemies/",
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
        out_dir = os.path.join(project_root, "data", "enemies")

    legacy_dir = os.path.join(project_root, "src", "data")

    z04_path = os.path.join(project_root, "reference", "aldonunez", "Z_04.asm")
    z05_path = os.path.join(project_root, "reference", "aldonunez", "Z_05.asm")
    z07_path = os.path.join(project_root, "reference", "aldonunez", "Z_07.asm")

    rom_path_env = os.environ.get("ZELDA_NES_ROM", "")
    if rom_path_env and os.path.isfile(rom_path_env):
        rom_path = rom_path_env
    else:
        rom_path = os.path.join(project_root, "Legend of Zelda, The (USA).nes")

    for required_path in [z04_path, z05_path, z07_path, rom_path]:
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

    prg_data = read_ines_prg(rom_path)

    print(f"Parsing reference data: {z04_path}")
    z04_blocks = parse_asm_data_blocks(z04_path, Z04_TARGET_LABELS)

    print(f"Parsing reference data: {z05_path}")
    z05_blocks = parse_asm_data_blocks(z05_path, Z05_TARGET_LABELS)

    print(f"Parsing reference data: {z07_path}")
    z07_blocks = parse_asm_data_blocks(z07_path, Z07_TARGET_LABELS)
    object_list_data = extract_object_lists(prg_data, z05_blocks)

    os.makedirs(out_dir, exist_ok=True)

    print("\nWriting C enemy/drop data files...")
    _blob, blocks_info, unique_init, unique_update = write_tables_c(
        out_dir, z07_blocks, z04_blocks, object_list_data
    )

    manifest = build_manifest(blocks_info, unique_init, unique_update)
    write_manifest(os.path.join(out_dir, "MANIFEST.json"), manifest)

    if args.legacy_inc:
        print("\nEmitting legacy .inc files...")
        os.makedirs(legacy_dir, exist_ok=True)
        write_enemy_tables_inc(legacy_dir, z07_blocks)
        write_drop_tables_inc(legacy_dir, z04_blocks)
        write_object_lists_inc(legacy_dir, object_list_data)

    total_bytes = (
        len(z07_blocks["ObjectTypeToAttributes"]["bytes"])
        + len(z07_blocks["ObjectTypeToHpPairs"]["bytes"])
        + len(z07_blocks["SpecialBossPaletteObjTypes"]["bytes"])
        + len(z07_blocks["UpdateObject_JumpTable"]["addr_labels"])
        + len(z07_blocks["InitObject_JumpTable"]["addr_labels"])
        + sum(len(z04_blocks[label]["bytes"]) for label in Z04_TARGET_LABELS)
        + len(object_list_data["obj_lists"])
        + len(object_list_data["obj_list_addrs"]) * 2
    )

    print("\n=== Enemy extraction complete ===")
    print(f"  Total extracted enemy/drop table bytes: {total_bytes}")
    print(f"  Output directory: {out_dir}")


if __name__ == "__main__":
    main()
