#!/usr/bin/env python3
"""
extract_misc.py - Extract foundational item, UI, and player constants.

S2 Phase D1: ports .inc emission to Genesis-ready C-array output under
data/misc/.  A --legacy-inc flag preserves the old src/data/ .inc files for
transitional callers.

This script focuses on the first high-value slice of "misc" data needed by the
Genesis engine:
  - item metadata tables from Z_01
  - submenu / cave UI placement tables from Z_01 + Z_05
  - player movement helper constants from Z_01 + Z_05
  - palette helpers and Genesis CRAM conversion tables from Z_01 + Z_02 + Z_05

Primary outputs (C arrays, data/misc/):
  item_tables.c      -- ItemIdToSlot, ItemIdToDescriptor, ItemSlotToPaletteOffsetsOrValues
  ui_layout.c        -- PriceList, LifeOrMoney, StatusBar, Submenu, Triforce tables
  palette_tables.c   -- PaletteRow7, GanonColorTriples, LinkColors, RoomPaletteSelector
  palettes.c         -- NesColorToGenesisCRAM lookup + all scene Genesis palettes
  person_text.c      -- PersonText blobs with selector tables
  player_constants.c -- LinkQSpeed constants + LinkToSquareOffsets
  MANIFEST.json      -- schema_version + nes_rom_sha256 + per-block metadata

Legacy outputs (--legacy-inc flag only, vasm .inc format):
  src/data/item_tables.inc, ui_layout.inc, palette_tables.inc, palettes.inc,
  person_text.inc, player_constants.inc
"""

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

# Canonical NES ROM SHA256 (locked in spec Section 0 / docs/audit/baseline_rom.md).
NES_ROM_SHA256 = "8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac"

INES_HEADER_SIZE = 16
PRG_BANK_SIZE = 0x4000
BANK_CPU_BASE = 0x8000

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
Z01_TARGET_LABELS = {
    "PriceListTemplateTransferBuf",
    "LifeOrMoneyItemXs",
    "LifeOrMoneyItemTypes",
    "LinkToSquareOffsetsX",
    "LinkToSquareOffsetsY",
    "ItemIdToSlot",
    "ItemIdToDescriptor",
    "ItemSlotToPaletteOffsetsOrValues",
    "PaletteRow7TransferRecord",
    "GanonColorTriples",
    "StatusBarTransferBufTemplate",
    "LinkColors_CommonCode",
    "OverworldPersonTextSelectors",
    "HintCaveTextSelectors0",
    "HintCaveTextSelectors1",
    "UnderworldPersonTextSelectorsA",
    "UnderworldPersonTextSelectorsB",
    "UnderworldPersonTextSelectorsC",
}

Z05_TARGET_LABELS = {
    "SubmenuItemXs",
    "SubmenuCursorXs",
    "TriforceTransferBufOffsets",
    "TriforceTriforceBufReplacements",
    "TriforceTransferBufTiles",
    "RoomPaletteSelectorToNTAttr",
}

Z02_PALETTE_LABELS = {
    "TitlePaletteTransferRecord",
    "StoryPaletteTransferRecord",
    "TriforcePaletteTransferRecord",
    "TriforceGlowingColors",
    "DemoPhase0Subphase1Palettes",
}


def strip_comment(line):
    return line.split(";", 1)[0].strip()


def parse_byte_token(token):
    token = token.strip()
    if not token:
        raise ValueError("Empty .BYTE token")
    if token.startswith("$"):
        return int(token[1:], 16)
    return int(token, 10)


def clamp(value, low, high):
    return max(low, min(high, value))


def read_ines_prg(path):
    with open(path, "rb") as f:
        header = f.read(INES_HEADER_SIZE)
        if header[:4] != b"NES\x1A":
            raise ValueError(f"Not a valid iNES ROM: {path}")
        prg_banks = header[4]
        prg_data = f.read(prg_banks * PRG_BANK_SIZE)
    return prg_data


def sha256_of_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_asm_data_blocks(path, target_labels):
    blocks = {}
    current = None

    with open(path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = strip_comment(raw_line)
            if not line:
                if current is not None and blocks[current]["bytes"]:
                    current = None
                continue

            match = LABEL_RE.match(line)
            if match:
                label = match.group(1)
                current = label if label in target_labels else None
                if current is not None and current not in blocks:
                    blocks[current] = {"bytes": bytearray()}
                continue

            if current is None:
                continue

            if line.startswith(".BYTE"):
                items = [item.strip() for item in line[5:].split(",") if item.strip()]
                blocks[current]["bytes"].extend(parse_byte_token(item) for item in items)

    missing = sorted(label for label in target_labels if label not in blocks)
    if missing:
        raise ValueError(f"Missing expected labels in {path}: {', '.join(missing)}")

    return blocks


def find_unique_pattern(data, pattern, description):
    pos = data.find(pattern)
    if pos < 0:
        raise ValueError(f"Could not find {description}")
    second = data.find(pattern, pos + 1)
    if second >= 0:
        raise ValueError(
            f"{description} was not unique (found at {pos:#x} and {second:#x})"
        )
    return pos


def parse_le_words(data):
    if len(data) & 1:
        raise ValueError("Word table has odd length")
    return [data[i] | (data[i + 1] << 8) for i in range(0, len(data), 2)]


def cpu_addr_to_bank_offset(cpu_addr):
    if cpu_addr < BANK_CPU_BASE or cpu_addr >= BANK_CPU_BASE + PRG_BANK_SIZE:
        raise ValueError(f"CPU address ${cpu_addr:04X} is outside bank range")
    return cpu_addr - BANK_CPU_BASE


def build_pointer_labels(addresses, prefix):
    labels_by_addr = {}
    table_labels = []
    for addr in addresses:
        if addr not in labels_by_addr:
            labels_by_addr[addr] = f"{prefix}{len(labels_by_addr):02d}"
        table_labels.append(labels_by_addr[addr])
    ordered_addrs = sorted(labels_by_addr)
    return labels_by_addr, table_labels, ordered_addrs


def write_labeled_records_bytes(lines, blob, ordered_addrs, labels_by_addr):
    first_offset = cpu_addr_to_bank_offset(ordered_addrs[0])
    ordered_offsets = [cpu_addr_to_bank_offset(addr) for addr in ordered_addrs]
    for index, addr in enumerate(ordered_addrs):
        start = ordered_offsets[index] - first_offset
        if index + 1 < len(ordered_offsets):
            end = ordered_offsets[index + 1] - first_offset
        else:
            end = len(blob)
        label = labels_by_addr[addr]
        data = blob[start:end]
        for i in range(0, len(data), 16):
            chunk = data[i : i + 16]
            lines.append("    " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")


def max_person_text_selector(z01_blocks):
    selector_values = []
    selector_values.extend(
        byte & 0x3F for byte in z01_blocks["OverworldPersonTextSelectors"]["bytes"]
    )
    for label in [
        "HintCaveTextSelectors0",
        "HintCaveTextSelectors1",
        "UnderworldPersonTextSelectorsA",
        "UnderworldPersonTextSelectorsB",
        "UnderworldPersonTextSelectorsC",
    ]:
        selector_values.extend(z01_blocks[label]["bytes"])
    return max(selector_values)


def extract_person_text(prg_data, z01_blocks):
    bank1_data = prg_data[1 * PRG_BANK_SIZE : 2 * PRG_BANK_SIZE]
    person_addr_table_size = max_person_text_selector(z01_blocks) + 2
    overworld_selector_bytes = bytes(z01_blocks["OverworldPersonTextSelectors"]["bytes"])
    overworld_selector_pos = find_unique_pattern(
        bank1_data,
        overworld_selector_bytes,
        "OverworldPersonTextSelectors in bank 1",
    )

    addr_table = bytes(bank1_data[:person_addr_table_size])
    text_blob = bytes(bank1_data[person_addr_table_size:overworld_selector_pos])
    addresses = parse_le_words(addr_table)

    first_text_offset = cpu_addr_to_bank_offset(addresses[0])
    if first_text_offset != person_addr_table_size:
        raise ValueError(
            "PersonTextAddrs did not point at the expected start of PersonText"
        )

    return {
        "addresses": addresses,
        "text_blob": text_blob,
    }


def extract_init_link_speed_constants(z05_path):
    with open(z05_path, "r", encoding="utf-8") as f:
        text = f.read()

    start = text.find("InitLinkSpeed:")
    end = text.find("Link_ModifyDirOnGridLine:", start)
    if start < 0 or end < 0:
        raise ValueError("Could not locate InitLinkSpeed block")

    block = text[start:end]
    values = []
    for raw_line in block.splitlines():
        line = strip_comment(raw_line)
        if line.startswith("LDA #$"):
            values.append(int(line.split("#$", 1)[1], 16))

    if len(values) < 3:
        raise ValueError("Could not extract Link speed constants from InitLinkSpeed")

    return {
        "LinkQSpeedDefault": values[0],
        "LinkQSpeedMountainStairs": values[1],
    }


# Frozen 64-color RGB table from the locked BizHawk 2.11 NesHawk `Palette`
# setting (config.ini). The former generic RGB table made Original colors
# visibly wrong even though the extracted NES palette indices were correct.
# Keep this embedded so the drag-and-drop builder needs only the user's ROM.
NES_REFERENCE_RGB = [
    (102, 102, 102), (0, 42, 136), (20, 18, 168), (59, 0, 164),
    (92, 0, 126), (110, 0, 64), (108, 7, 0), (87, 29, 0),
    (52, 53, 0), (12, 73, 0), (0, 82, 0), (0, 79, 8),
    (0, 64, 78), (0, 0, 0), (0, 0, 0), (0, 0, 0),
    (174, 174, 174), (21, 95, 218), (66, 64, 254), (118, 39, 255),
    (161, 27, 205), (184, 30, 124), (181, 50, 32), (153, 79, 0),
    (108, 110, 0), (56, 135, 0), (13, 148, 0), (0, 144, 50),
    (0, 124, 142), (0, 0, 0), (0, 0, 0), (0, 0, 0),
    (254, 254, 254), (100, 176, 254), (147, 144, 254), (199, 119, 254),
    (243, 106, 254), (254, 110, 205), (254, 130, 112), (235, 159, 35),
    (189, 191, 0), (137, 217, 0), (93, 229, 48), (69, 225, 130),
    (72, 206, 223), (79, 79, 79), (0, 0, 0), (0, 0, 0),
    (254, 254, 254), (193, 224, 254), (212, 211, 254), (233, 200, 254),
    (251, 195, 254), (254, 197, 235), (254, 205, 198), (247, 217, 166),
    (229, 230, 149), (208, 240, 151), (190, 245, 171), (180, 243, 205),
    (181, 236, 243), (184, 184, 184), (0, 0, 0), (0, 0, 0),
]


def nes_level_to_genesis(channel):
    # Genplus-gx displays the eight Genesis channel levels as 0,34,...,238.
    return int(clamp(round(channel / 34.0), 0, 7)) * 2


def nes_color_index_to_genesis(color_index):
    color_index &= 0x3F
    red, green, blue = NES_REFERENCE_RGB[color_index]
    red_level = nes_level_to_genesis(red)
    green_level = nes_level_to_genesis(green)
    blue_level = nes_level_to_genesis(blue)
    return (blue_level << 8) | (green_level << 4) | red_level


def palette_record_to_nes_colors(record_bytes):
    if len(record_bytes) < 4 or record_bytes[-1] != 0xFF:
        raise ValueError("Palette transfer record was not terminated with $FF")
    color_count = record_bytes[2]
    color_start = 3
    color_end = color_start + color_count
    if color_end != len(record_bytes) - 1:
        raise ValueError("Palette transfer record length did not match payload")
    return list(record_bytes[color_start:color_end])


def nes_colors_to_genesis_words(color_bytes):
    return [nes_color_index_to_genesis(color) for color in color_bytes]


def tuned_link_colors_genesis():
    # The extracted NES source bytes for gameplay Link are correct, but the
    # generic lookup-table conversion leaves the opening-screen sprite looking
    # noticeably off compared to the NES reference capture.
    # The placeholder Link art uses the third sprite color for the larger
    # shaded regions and the second sprite color for the smaller highlights,
    # so keep the green base and tune/swap the two warm colors to match the
    # live NES opening-screen capture more closely.
    return [0x0C8, 0x28E, 0x04A]


# ---------------------------------------------------------------------------
# C-array emission helpers
# ---------------------------------------------------------------------------

def bytes_to_c_array(data, var_name, bytes_per_line=16):
    """Return a C-array string for the given raw bytes."""
    size = len(data)
    lines = [f"/* Auto-generated by tools/extract_misc.py - do not edit. */"]
    lines.append(f"const unsigned long {var_name}_size = {size}UL;")
    lines.append(f"const unsigned char {var_name}[{size}] = {{")
    for i in range(0, size, bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def words_to_c_array(words, var_name, words_per_line=8):
    """Return a C-array string for the given 16-bit words (stored as unsigned short)."""
    size = len(words)
    lines = [f"/* Auto-generated by tools/extract_misc.py - do not edit. */"]
    lines.append(f"const unsigned long {var_name}_count = {size}UL;")
    lines.append(f"const unsigned short {var_name}[{size}] = {{")
    for i in range(0, size, words_per_line):
        chunk = words[i : i + words_per_line]
        lines.append("    " + ", ".join(f"0x{w:04X}" for w in chunk) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def write_c_file(path, content):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print(f"  Wrote {path} ({len(content)} bytes)")


# ---------------------------------------------------------------------------
# Legacy .inc emission helpers (only used with --legacy-inc)
# ---------------------------------------------------------------------------

def bytes_to_words_inc(words, label, words_per_line=8):
    lines = [f"; {label} - {len(words)} words", f"{label}:"]
    for i in range(0, len(words), words_per_line):
        chunk = words[i : i + words_per_line]
        lines.append("    dc.w " + ",".join(f"${word:04X}" for word in chunk))
    return "\n".join(lines)


def data_to_inc_bytes(data, label, bytes_per_line=16):
    lines = [f"; {label} - {len(data)} bytes", f"{label}:"]
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        lines.append("    dc.b " + ",".join(f"${byte:02X}" for byte in chunk))
    return "\n".join(lines)


def labels_to_inc_words(label_names, table_label, words_per_line=4):
    lines = [f"; {table_label} - {len(label_names)} entries", f"{table_label}:"]
    for i in range(0, len(label_names), words_per_line):
        chunk = label_names[i : i + words_per_line]
        lines.append("    dc.w " + ",".join(chunk))
    return "\n".join(lines)


def write_labeled_records(lines, blob, ordered_addrs, labels_by_addr):
    first_offset = cpu_addr_to_bank_offset(ordered_addrs[0])
    ordered_offsets = [cpu_addr_to_bank_offset(addr) for addr in ordered_addrs]
    for index, addr in enumerate(ordered_addrs):
        start = ordered_offsets[index] - first_offset
        if index + 1 < len(ordered_offsets):
            end = ordered_offsets[index + 1] - first_offset
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


def write_text_file(path, lines):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"  Wrote {path}")


# ---------------------------------------------------------------------------
# C-array output writers
# ---------------------------------------------------------------------------

def write_item_tables_c(out_dir, z01_blocks):
    """Emit item_tables.c with three concatenated arrays packed into one var."""
    item_id_to_slot = bytes(z01_blocks["ItemIdToSlot"]["bytes"])
    item_id_to_desc = bytes(z01_blocks["ItemIdToDescriptor"]["bytes"])
    item_slot_to_pal = bytes(z01_blocks["ItemSlotToPaletteOffsetsOrValues"]["bytes"])

    # Pack into a single array; callers use known offsets (documented in MANIFEST)
    payload = item_id_to_slot + item_id_to_desc + item_slot_to_pal
    content = bytes_to_c_array(payload, "misc_item_tables")
    write_c_file(os.path.join(out_dir, "item_tables.c"), content)

    return [
        {"name": "ItemIdToSlot", "file": "item_tables.c", "var_name": "misc_item_tables",
         "byte_offset": 0, "byte_size": len(item_id_to_slot)},
        {"name": "ItemIdToDescriptor", "file": "item_tables.c", "var_name": "misc_item_tables",
         "byte_offset": len(item_id_to_slot), "byte_size": len(item_id_to_desc)},
        {"name": "ItemSlotToPaletteOffsetsOrValues", "file": "item_tables.c",
         "var_name": "misc_item_tables",
         "byte_offset": len(item_id_to_slot) + len(item_id_to_desc),
         "byte_size": len(item_slot_to_pal)},
    ]


def write_ui_layout_c(out_dir, z01_blocks, z05_blocks):
    """Emit ui_layout.c with all submenu/cave/Triforce UI tables."""
    labels_z01 = [
        "PriceListTemplateTransferBuf",
        "LifeOrMoneyItemXs",
        "LifeOrMoneyItemTypes",
        "StatusBarTransferBufTemplate",
    ]
    labels_z05 = [
        "SubmenuItemXs",
        "SubmenuCursorXs",
        "TriforceTransferBufOffsets",
        "TriforceTriforceBufReplacements",
        "TriforceTransferBufTiles",
    ]

    chunks = []
    meta = []
    offset = 0
    for label in labels_z01:
        data = bytes(z01_blocks[label]["bytes"])
        meta.append({"name": label, "file": "ui_layout.c", "var_name": "misc_ui_layout",
                     "byte_offset": offset, "byte_size": len(data)})
        chunks.append(data)
        offset += len(data)
    for label in labels_z05:
        data = bytes(z05_blocks[label]["bytes"])
        meta.append({"name": label, "file": "ui_layout.c", "var_name": "misc_ui_layout",
                     "byte_offset": offset, "byte_size": len(data)})
        chunks.append(data)
        offset += len(data)

    payload = b"".join(chunks)
    content = bytes_to_c_array(payload, "misc_ui_layout")
    write_c_file(os.path.join(out_dir, "ui_layout.c"), content)
    return meta


def write_palette_tables_c(out_dir, z01_blocks, z05_blocks):
    """Emit palette_tables.c with attribute and color-selector tables."""
    labels = [
        ("z01", "PaletteRow7TransferRecord"),
        ("z01", "GanonColorTriples"),
        ("z01", "LinkColors_CommonCode"),
        ("z05", "RoomPaletteSelectorToNTAttr"),
    ]

    chunks = []
    meta = []
    offset = 0
    for src, label in labels:
        blocks = z01_blocks if src == "z01" else z05_blocks
        data = bytes(blocks[label]["bytes"])
        meta.append({"name": label, "file": "palette_tables.c",
                     "var_name": "misc_palette_tables",
                     "byte_offset": offset, "byte_size": len(data)})
        chunks.append(data)
        offset += len(data)

    payload = b"".join(chunks)
    content = bytes_to_c_array(payload, "misc_palette_tables")
    write_c_file(os.path.join(out_dir, "palette_tables.c"), content)
    return meta


def write_palettes_c(out_dir, z01_blocks, z02_blocks):
    """Emit palettes.c with NES->Genesis CRAM lookup + all scene palettes."""
    title_colors = palette_record_to_nes_colors(
        bytes(z02_blocks["TitlePaletteTransferRecord"]["bytes"])
    )
    story_colors = palette_record_to_nes_colors(
        bytes(z02_blocks["StoryPaletteTransferRecord"]["bytes"])
    )
    triforce_colors = palette_record_to_nes_colors(
        bytes(z02_blocks["TriforcePaletteTransferRecord"]["bytes"])
    )
    row7_colors = palette_record_to_nes_colors(
        bytes(z01_blocks["PaletteRow7TransferRecord"]["bytes"])
    )
    demo_colors = bytes(z02_blocks["DemoPhase0Subphase1Palettes"]["bytes"])
    triforce_glow_colors = bytes(z02_blocks["TriforceGlowingColors"]["bytes"])
    link_colors_raw = bytes(z01_blocks["LinkColors_CommonCode"]["bytes"])
    ganon_colors = bytes(z01_blocks["GanonColorTriples"]["bytes"])

    lookup_words = [nes_color_index_to_genesis(index) for index in range(0x40)]

    named_word_tables = [
        ("NesColorToGenesisCRAM", lookup_words),
        ("TitlePaletteGenesis", nes_colors_to_genesis_words(title_colors)),
        ("StoryPaletteGenesis", nes_colors_to_genesis_words(story_colors)),
        ("TriforcePaletteGenesis", nes_colors_to_genesis_words(triforce_colors)),
        ("PaletteRow7Genesis", nes_colors_to_genesis_words(row7_colors)),
        ("TriforceGlowingColorsGenesis", nes_colors_to_genesis_words(list(triforce_glow_colors))),
        ("LinkColorsGenesis", tuned_link_colors_genesis()),
        ("GanonColorTriplesGenesis", nes_colors_to_genesis_words(list(ganon_colors))),
        ("DemoPhase0Subphase1PalettesGenesis", nes_colors_to_genesis_words(list(demo_colors))),
    ]

    # Pack all word tables end-to-end as raw bytes (16-bit unsigned LE)
    import struct
    chunks = []
    meta = []
    offset = 0
    for name, words in named_word_tables:
        raw = struct.pack(f"<{len(words)}H", *words)
        meta.append({"name": name, "file": "palettes.c", "var_name": "misc_palettes",
                     "byte_offset": offset, "byte_size": len(raw),
                     "entry_count": len(words), "format": "u16le_genesis_cram"})
        chunks.append(raw)
        offset += len(raw)

    payload = b"".join(chunks)
    content = bytes_to_c_array(payload, "misc_palettes")
    write_c_file(os.path.join(out_dir, "palettes.c"), content)
    return meta


def write_person_text_c(out_dir, z01_blocks, person_text_data):
    """Emit person_text.c with selector tables + PersonText blobs."""
    selector_labels = [
        "OverworldPersonTextSelectors",
        "HintCaveTextSelectors0",
        "HintCaveTextSelectors1",
        "UnderworldPersonTextSelectorsA",
        "UnderworldPersonTextSelectorsB",
        "UnderworldPersonTextSelectorsC",
    ]

    chunks = []
    meta = []
    offset = 0
    for label in selector_labels:
        data = bytes(z01_blocks[label]["bytes"])
        meta.append({"name": label, "file": "person_text.c", "var_name": "misc_person_text",
                     "byte_offset": offset, "byte_size": len(data)})
        chunks.append(data)
        offset += len(data)

    # PersonTextAddrs: flat array of LE 16-bit NES CPU addresses
    addrs_raw = b"".join(
        bytes([addr & 0xFF, (addr >> 8) & 0xFF])
        for addr in person_text_data["addresses"]
    )
    meta.append({"name": "PersonTextAddrs", "file": "person_text.c",
                 "var_name": "misc_person_text", "byte_offset": offset,
                 "byte_size": len(addrs_raw), "entry_count": len(person_text_data["addresses"]),
                 "format": "u16le_nes_cpu_addr"})
    chunks.append(addrs_raw)
    offset += len(addrs_raw)

    # PersonText blob
    text_blob = person_text_data["text_blob"]
    meta.append({"name": "PersonTextBlob", "file": "person_text.c",
                 "var_name": "misc_person_text", "byte_offset": offset,
                 "byte_size": len(text_blob)})
    chunks.append(text_blob)

    payload = b"".join(chunks)
    content = bytes_to_c_array(payload, "misc_person_text")
    write_c_file(os.path.join(out_dir, "person_text.c"), content)
    return meta


def write_player_constants_c(out_dir, z01_blocks, link_speed_constants):
    """Emit player_constants.c with speed constants + offset tables."""
    offset_x = bytes(z01_blocks["LinkToSquareOffsetsX"]["bytes"])
    offset_y = bytes(z01_blocks["LinkToSquareOffsetsY"]["bytes"])

    # Speed constants stored as a 2-byte prefix before the table data
    speed_bytes = bytes([
        link_speed_constants["LinkQSpeedDefault"],
        link_speed_constants["LinkQSpeedMountainStairs"],
    ])
    payload = speed_bytes + offset_x + offset_y

    meta = [
        {"name": "LinkQSpeedDefault", "file": "player_constants.c",
         "var_name": "misc_player_constants",
         "byte_offset": 0, "byte_size": 1},
        {"name": "LinkQSpeedMountainStairs", "file": "player_constants.c",
         "var_name": "misc_player_constants",
         "byte_offset": 1, "byte_size": 1},
        {"name": "LinkToSquareOffsetsX", "file": "player_constants.c",
         "var_name": "misc_player_constants",
         "byte_offset": 2, "byte_size": len(offset_x)},
        {"name": "LinkToSquareOffsetsY", "file": "player_constants.c",
         "var_name": "misc_player_constants",
         "byte_offset": 2 + len(offset_x), "byte_size": len(offset_y)},
    ]

    content = bytes_to_c_array(payload, "misc_player_constants")
    write_c_file(os.path.join(out_dir, "player_constants.c"), content)
    return meta


def write_manifest(out_dir, all_blocks_meta, rom_sha256):
    """Write MANIFEST.json for data/misc/."""
    manifest = {
        "schema_version": 1,
        "nes_rom_sha256": rom_sha256,
        "blocks": all_blocks_meta,
    }
    path = os.path.join(out_dir, "MANIFEST.json")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"  Wrote {path}")


# ---------------------------------------------------------------------------
# Legacy .inc writers (only called with --legacy-inc)
# ---------------------------------------------------------------------------

def write_item_tables_inc(data_dir, z01_blocks):
    lines = [
        "; Item metadata tables extracted from NES Zelda",
        "; Auto-generated by extract_misc.py - DO NOT EDIT",
        "",
        data_to_inc_bytes(bytes(z01_blocks["ItemIdToSlot"]["bytes"]), "ItemIdToSlot"),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["ItemIdToDescriptor"]["bytes"]),
            "ItemIdToDescriptor",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["ItemSlotToPaletteOffsetsOrValues"]["bytes"]),
            "ItemSlotToPaletteOffsetsOrValues",
        ),
    ]
    write_text_file(os.path.join(data_dir, "item_tables.inc"), lines)


def write_ui_layout_inc(data_dir, z01_blocks, z05_blocks):
    lines = [
        "; UI and submenu placement tables extracted from NES Zelda",
        "; Auto-generated by extract_misc.py - DO NOT EDIT",
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["PriceListTemplateTransferBuf"]["bytes"]),
            "PriceListTemplateTransferBuf",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["LifeOrMoneyItemXs"]["bytes"]),
            "LifeOrMoneyItemXs",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["LifeOrMoneyItemTypes"]["bytes"]),
            "LifeOrMoneyItemTypes",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["StatusBarTransferBufTemplate"]["bytes"]),
            "StatusBarTransferBufTemplate",
        ),
        "",
        data_to_inc_bytes(bytes(z05_blocks["SubmenuItemXs"]["bytes"]), "SubmenuItemXs"),
        "",
        data_to_inc_bytes(
            bytes(z05_blocks["SubmenuCursorXs"]["bytes"]),
            "SubmenuCursorXs",
        ),
        "",
        data_to_inc_bytes(
            bytes(z05_blocks["TriforceTransferBufOffsets"]["bytes"]),
            "TriforceTransferBufOffsets",
        ),
        "",
        data_to_inc_bytes(
            bytes(z05_blocks["TriforceTriforceBufReplacements"]["bytes"]),
            "TriforceTriforceBufReplacements",
        ),
        "",
        data_to_inc_bytes(
            bytes(z05_blocks["TriforceTransferBufTiles"]["bytes"]),
            "TriforceTransferBufTiles",
        ),
    ]
    write_text_file(os.path.join(data_dir, "ui_layout.inc"), lines)


def write_palette_tables_inc(data_dir, z01_blocks, z05_blocks):
    lines = [
        "; Gameplay palette and attribute helper tables extracted from NES Zelda",
        "; Auto-generated by extract_misc.py - DO NOT EDIT",
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["PaletteRow7TransferRecord"]["bytes"]),
            "PaletteRow7TransferRecord",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["GanonColorTriples"]["bytes"]),
            "GanonColorTriples",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["LinkColors_CommonCode"]["bytes"]),
            "LinkColors_CommonCode",
        ),
        "",
        data_to_inc_bytes(
            bytes(z05_blocks["RoomPaletteSelectorToNTAttr"]["bytes"]),
            "RoomPaletteSelectorToNTAttr",
        ),
    ]
    write_text_file(os.path.join(data_dir, "palette_tables.inc"), lines)


def write_palettes_inc(data_dir, z01_blocks, z02_blocks):
    title_colors = palette_record_to_nes_colors(
        bytes(z02_blocks["TitlePaletteTransferRecord"]["bytes"])
    )
    story_colors = palette_record_to_nes_colors(
        bytes(z02_blocks["StoryPaletteTransferRecord"]["bytes"])
    )
    triforce_colors = palette_record_to_nes_colors(
        bytes(z02_blocks["TriforcePaletteTransferRecord"]["bytes"])
    )
    row7_colors = palette_record_to_nes_colors(
        bytes(z01_blocks["PaletteRow7TransferRecord"]["bytes"])
    )
    demo_colors = bytes(z02_blocks["DemoPhase0Subphase1Palettes"]["bytes"])
    triforce_glow_colors = bytes(z02_blocks["TriforceGlowingColors"]["bytes"])
    link_colors = bytes(z01_blocks["LinkColors_CommonCode"]["bytes"])
    ganon_colors = bytes(z01_blocks["GanonColorTriples"]["bytes"])

    lookup_words = [nes_color_index_to_genesis(index) for index in range(0x40)]

    lines = [
        "; Unified Phase 1 palette output extracted from NES Zelda",
        "; Auto-generated by extract_misc.py - DO NOT EDIT",
        "; Genesis CRAM words are stored in 0BGR format.",
        "",
        bytes_to_words_inc(lookup_words, "NesColorToGenesisCRAM"),
        "",
        bytes_to_words_inc(
            nes_colors_to_genesis_words(title_colors),
            "TitlePaletteGenesis",
        ),
        "",
        bytes_to_words_inc(
            nes_colors_to_genesis_words(story_colors),
            "StoryPaletteGenesis",
        ),
        "",
        bytes_to_words_inc(
            nes_colors_to_genesis_words(triforce_colors),
            "TriforcePaletteGenesis",
        ),
        "",
        bytes_to_words_inc(
            nes_colors_to_genesis_words(row7_colors),
            "PaletteRow7Genesis",
        ),
        "",
        bytes_to_words_inc(
            nes_colors_to_genesis_words(list(triforce_glow_colors)),
            "TriforceGlowingColorsGenesis",
        ),
        "",
        bytes_to_words_inc(
            tuned_link_colors_genesis(),
            "LinkColorsGenesis",
        ),
        "",
        bytes_to_words_inc(
            nes_colors_to_genesis_words(list(ganon_colors)),
            "GanonColorTriplesGenesis",
        ),
        "",
        bytes_to_words_inc(
            nes_colors_to_genesis_words(list(demo_colors)),
            "DemoPhase0Subphase1PalettesGenesis",
        ),
    ]
    write_text_file(os.path.join(data_dir, "palettes.inc"), lines)


def write_person_text_inc(data_dir, z01_blocks, person_text_data):
    labels_by_addr, table_labels, ordered_addrs = build_pointer_labels(
        person_text_data["addresses"], "PersonText"
    )

    lines = [
        "; Person/cave text tables extracted from NES Zelda bank 1",
        "; Auto-generated by extract_misc.py - DO NOT EDIT",
        "",
        "; Overworld selector bytes keep the original high-bit cave flags",
        data_to_inc_bytes(
            bytes(z01_blocks["OverworldPersonTextSelectors"]["bytes"]),
            "OverworldPersonTextSelectors",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["HintCaveTextSelectors0"]["bytes"]),
            "HintCaveTextSelectors0",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["HintCaveTextSelectors1"]["bytes"]),
            "HintCaveTextSelectors1",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["UnderworldPersonTextSelectorsA"]["bytes"]),
            "UnderworldPersonTextSelectorsA",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["UnderworldPersonTextSelectorsB"]["bytes"]),
            "UnderworldPersonTextSelectorsB",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["UnderworldPersonTextSelectorsC"]["bytes"]),
            "UnderworldPersonTextSelectorsC",
        ),
        "",
        labels_to_inc_words(table_labels, "PersonTextAddrs"),
        "",
    ]

    write_labeled_records(lines, person_text_data["text_blob"], ordered_addrs, labels_by_addr)
    write_text_file(os.path.join(data_dir, "person_text.inc"), lines[:-1])


def write_player_constants_inc(data_dir, z01_blocks, link_speed_constants):
    lines = [
        "; Player constants extracted from NES Zelda",
        "; Auto-generated by extract_misc.py - DO NOT EDIT",
        "",
        f"LinkQSpeedDefault equ ${link_speed_constants['LinkQSpeedDefault']:02X}",
        f"LinkQSpeedMountainStairs equ ${link_speed_constants['LinkQSpeedMountainStairs']:02X}",
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["LinkToSquareOffsetsX"]["bytes"]),
            "LinkToSquareOffsetsX",
        ),
        "",
        data_to_inc_bytes(
            bytes(z01_blocks["LinkToSquareOffsetsY"]["bytes"]),
            "LinkToSquareOffsetsY",
        ),
    ]
    write_text_file(os.path.join(data_dir, "player_constants.inc"), lines)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def write_person_text_data_c(data_dir):
    """src/data/person_text_data.{c,h}: the C linkage of person_text.inc.

    The gameplay build links the cave NPC text as C. Parses the .inc this
    run just wrote, so both always carry the same records."""
    inc = os.path.join(data_dir, "person_text.inc")
    records = {}
    order = []
    label = None
    in_addrs = False
    with open(inc, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.split(";", 1)[0].rstrip()
            m = re.match(r"^(\w+):$", line)
            if m:
                label = m.group(1)
                in_addrs = label == "PersonTextAddrs"
                if label.startswith("PersonText") and not in_addrs:
                    records[label] = []
                continue
            body = line.strip()
            if in_addrs and body.startswith("dc.w"):
                order.extend(x.strip() for x in body[4:].split(","))
            elif label in records and body.startswith("dc.b"):
                records[label].extend(int(x.strip()[1:], 16) for x in body[4:].split(","))
    if not order or any(name not in records for name in order):
        raise SystemExit("person_text.inc: PersonTextAddrs references a missing record")

    c = ["/* Auto-generated from src/data/person_text.inc */",
         '#include "person_text_data.h"', ""]
    for name in sorted(records):
        data = records[name]
        c.append(f"static const unsigned char {name}[{len(data)}] = {{")
        for i in range(0, len(data), 8):
            c.append("    " + " ".join(f"0x{v:02X}u," for v in data[i:i + 8]))
        c += ["};", ""]
    c.append(f"const unsigned char * const PersonTextAddrs[{len(order)}] = {{")
    for i in range(0, len(order), 4):
        c.append("    " + " ".join(f"{n}," for n in order[i:i + 4]))
    c += ["};", "", f"/* {len(records)} text blobs */", ""]
    h = [f"/* PersonTextAddrs[{len(order)}] — cave NPC text blob pointer table.",
         " * Generated from src/data/person_text.inc (NES Z_01.asm:50 PersonText). */",
         "#ifndef SRC_DATA_PERSON_TEXT_DATA_H",
         "#define SRC_DATA_PERSON_TEXT_DATA_H",
         "",
         "#ifdef __cplusplus",
         'extern "C" {',
         "#endif",
         "",
         f"extern const unsigned char * const PersonTextAddrs[{len(order)}];",
         "",
         "#ifdef __cplusplus",
         "}",
         "#endif",
         "",
         "#endif",
         ""]
    for name, lines in (("person_text_data.c", c), ("person_text_data.h", h)):
        with open(os.path.join(data_dir, name), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
        print(f"  Wrote {os.path.join(data_dir, name)}")


def main():
    parser = argparse.ArgumentParser(
        description="Extract misc tables from NES Zelda to C arrays under data/misc/"
    )
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Output directory for C arrays and MANIFEST.json (default: <repo>/data/misc/)",
    )
    parser.add_argument(
        "--legacy-inc",
        action="store_true",
        help="Also emit legacy vasm .inc files to <repo>/src/data/ for transitional callers",
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    z01_path = os.path.join(project_root, "reference", "aldonunez", "Z_01.asm")
    z02_path = os.path.join(project_root, "reference", "aldonunez", "Z_02.asm")
    z05_path = os.path.join(project_root, "reference", "aldonunez", "Z_05.asm")
    rom_path_env = os.environ.get("ZELDA_NES_ROM", "")
    if rom_path_env and os.path.isfile(rom_path_env):
        rom_path = rom_path_env
    else:
        rom_path = os.path.join(project_root, "Legend of Zelda, The (USA).nes")

    for required_path in [z01_path, z02_path, z05_path, rom_path]:
        if not os.path.exists(required_path):
            print(f"ERROR: required file not found: {required_path}")
            sys.exit(1)

    rom_sha256 = sha256_of_file(rom_path)
    if rom_sha256 != NES_ROM_SHA256:
        print(f"ERROR: ROM SHA256 mismatch. Expected {NES_ROM_SHA256}, got {rom_sha256}")
        sys.exit(1)

    out_dir = args.out_dir if args.out_dir else os.path.join(project_root, "data", "misc")
    os.makedirs(out_dir, exist_ok=True)

    print(f"Parsing reference data: {z01_path}")
    z01_blocks = parse_asm_data_blocks(z01_path, Z01_TARGET_LABELS)

    print(f"Parsing reference data: {z02_path}")
    z02_blocks = parse_asm_data_blocks(z02_path, Z02_PALETTE_LABELS)

    print(f"Parsing reference data: {z05_path}")
    z05_blocks = parse_asm_data_blocks(z05_path, Z05_TARGET_LABELS)
    link_speed_constants = extract_init_link_speed_constants(z05_path)
    prg_data = read_ines_prg(rom_path)
    person_text_data = extract_person_text(prg_data, z01_blocks)

    print(f"\nWriting C-array misc data files to {out_dir}...")
    all_meta = []
    all_meta.extend(write_item_tables_c(out_dir, z01_blocks))
    all_meta.extend(write_ui_layout_c(out_dir, z01_blocks, z05_blocks))
    all_meta.extend(write_palette_tables_c(out_dir, z01_blocks, z05_blocks))
    all_meta.extend(write_palettes_c(out_dir, z01_blocks, z02_blocks))
    all_meta.extend(write_person_text_c(out_dir, z01_blocks, person_text_data))
    all_meta.extend(write_player_constants_c(out_dir, z01_blocks, link_speed_constants))
    write_manifest(out_dir, all_meta, rom_sha256)

    if args.legacy_inc:
        legacy_dir = os.path.join(project_root, "src", "data")
        os.makedirs(legacy_dir, exist_ok=True)
        print(f"\nWriting legacy .inc files to {legacy_dir}...")
        write_item_tables_inc(legacy_dir, z01_blocks)
        write_ui_layout_inc(legacy_dir, z01_blocks, z05_blocks)
        write_palette_tables_inc(legacy_dir, z01_blocks, z05_blocks)
        write_palettes_inc(legacy_dir, z01_blocks, z02_blocks)
        write_player_constants_inc(legacy_dir, z01_blocks, link_speed_constants)
        write_person_text_inc(legacy_dir, z01_blocks, person_text_data)
        write_person_text_data_c(legacy_dir)

    total_bytes = sum(len(block["bytes"]) for block in z01_blocks.values())
    total_bytes += sum(len(block["bytes"]) for block in z02_blocks.values())
    total_bytes += sum(len(block["bytes"]) for block in z05_blocks.values())
    total_bytes += len(person_text_data["text_blob"])
    total_bytes += len(person_text_data["addresses"]) * 2

    print("\n=== Misc extraction complete ===")
    print(f"  Total extracted misc table bytes: {total_bytes}")
    print(f"  Manifest entries: {len(all_meta)}")
    print(f"  Output directory: {out_dir}")


if __name__ == "__main__":
    main()
