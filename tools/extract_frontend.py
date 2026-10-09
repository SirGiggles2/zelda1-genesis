#!/usr/bin/env python3
"""
extract_frontend.py - Extract frontend, save-menu, and ending text tables.

S2 Phase D1: ports .inc emission to Genesis-ready C-array output under
data/text/ as NES reference data.

OVERLAP NOTE (S2 D1 decision):
  extract_fs_assets.py -> data/fs/  : Genesis-customized FS layout (Redux
      palette, Genesis tile IDs, layout tuned for 320px display).
  extract_frontend.py  -> data/text/: NES-reference frontend tables (original
      NES CPU addresses, NES tile indices, original menu layouts).

They intentionally coexist. Callers needing the NES-original layout use
data/text/nes_frontend_*.c; callers needing the Genesis-tuned layout use
data/fs/*.c.  Output files are prefixed "nes_frontend_" to make this loud.

Primary outputs (C arrays, data/text/):
  nes_frontend_ui.c          -- file-select / register / elimination UI tables
  nes_frontend_palettes.c    -- raw NES palette transfer records (reference only)
  nes_frontend_save.c        -- save-slot helper address tables
  nes_frontend_text.c        -- ending and credits text-support tables
  nes_frontend_demo_text.c   -- DemoTextField blobs + DemoLineTextAddrs
  nes_frontend_credits.c     -- CreditsTextLine blobs
  nes_frontend_transfers.c   -- StoryTileAttrTransferBuf + GameTitleTransferBuf
  MANIFEST.json              -- schema_version + nes_rom_sha256 + per-block metadata

Legacy outputs (--legacy-inc flag only, vasm .inc format):
  src/data/frontend_ui.inc, frontend_palettes.inc, save_tables.inc,
  text.inc, demo_text.inc, credits_text.inc, frontend_transfers.inc
  reference/aldonunez/dat/StoryTileAttrTransferBuf.dat
  reference/aldonunez/dat/GameTitleTransferBuf.dat
"""

import argparse
import hashlib
import json
import os
import re
import sys

# Canonical NES ROM SHA256 (locked in spec Section 0 / docs/audit/baseline_rom.md).
NES_ROM_SHA256 = "8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac"

INES_HEADER_SIZE = 16
PRG_BANK_SIZE = 0x4000
BANK_CPU_BASE = 0x8000

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
VAR_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:=\s*\$([0-9A-Fa-f]+)$")
OFFSET_EXPR_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)([+-]\d+)?$")

Z02_TARGET_LABELS = {
    "TitlePaletteTransferRecord",
    "StoryPaletteTransferRecord",
    "TriforcePaletteTransferRecord",
    "TriforceGlowingColors",
    "DemoPhase0Subphase1Palettes",
    "ModeEandFSlotCursorYs",
    "ModeE_CharMap",
    "SlotToInitialNameCharTransferHeaders",
    "DeletedSlotBlankNameTransferBuf",
    "ModeEandFCursorSprites",
    "ModeE_CharBoardYOffsetsAndBounds",
    "ModeFTitleTransferBuf",
    "ModeFSaveSlotTemplatePatchRegister",
    "ModeFSaveSlotTemplateTransferBuf",
    "SlotToBlankNameTransferBufEndOffset",
    "SlotToNameOffset",
    "Mode1SlotLineTransferBuf",
    "Mode1DeathCountsTransferBuf",
    "LinkColors",
    "Mode1CursorSpriteTriplet",
    "Mode1CursorSpriteYs",
    "DemoLineAttrs",
    "SaveSlotHeartsAddrsLo",
    "SaveSlotHeartsAddrsHi",
    "ProfileNameAddrsLo",
    "ProfileNameAddrsHi",
    "PlayAreaAttr0TransferBuf",
    "ThanksText",
    "ThanksTextboxCharTransferRecTemplate",
    "ThanksTextboxLineAddrsLo",
    "EndingFlashColors",
    "PeaceTextboxCharTransferRecTemplate",
    "PeaceTextboxCharAddrsLo",
    "PeaceText",
    "CreditsLastScreenList",
    "CreditsLastVscrollList",
    "CreditLineVramAddrsHi",
    "CreditsPagesTextMasks",
    "CreditsAttrs",
}

Z06_TARGET_LABELS = {
    "Mode11PlayAreaAttrsBottomHalfTransferBuf",
}


def strip_comment(line):
    return line.split(";", 1)[0].strip()


def sha256_of_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_byte_token(token):
    token = token.strip()
    if not token:
        raise ValueError("Empty token")
    if token.startswith("$"):
        return int(token[1:], 16)
    return int(token, 10)


def read_ines_prg(path):
    with open(path, "rb") as f:
        header = f.read(INES_HEADER_SIZE)
        if header[:4] != b"NES\x1A":
            raise ValueError(f"Not a valid iNES ROM: {path}")
        prg_banks = header[4]
        prg_data = f.read(prg_banks * PRG_BANK_SIZE)
    return prg_data


def parse_variables(path):
    symbols = {}
    with open(path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = strip_comment(raw_line)
            if not line:
                continue
            match = VAR_RE.match(line)
            if match:
                symbols[match.group(1)] = int(match.group(2), 16)
    return symbols


def eval_offset_expr(expr, symbols):
    match = OFFSET_EXPR_RE.match(expr.strip())
    if not match:
        raise ValueError(f"Unsupported byte expression: {expr}")
    name = match.group(1)
    offset_text = match.group(2)
    if name not in symbols:
        raise ValueError(f"Unknown symbol in expression: {expr}")
    value = symbols[name]
    if offset_text:
        value += int(offset_text, 10)
    return value


def parse_asm_data_blocks(path, target_labels, symbols):
    blocks = {}
    current = None

    with open(path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = strip_comment(raw_line)
            if not line:
                if current is not None and blocks[current]:
                    current = None
                continue

            match = LABEL_RE.match(line)
            if match:
                label = match.group(1)
                current = label if label in target_labels else None
                if current is not None and current not in blocks:
                    blocks[current] = bytearray()
                continue

            if current is None:
                continue

            if line.startswith(".BYTE"):
                items = [item.strip() for item in line[5:].split(",") if item.strip()]
                blocks[current].extend(parse_byte_token(item) for item in items)
            elif line.startswith(".LOBYTES"):
                expr = line[len(".LOBYTES"):].strip()
                value = eval_offset_expr(expr, symbols)
                blocks[current].append(value & 0xFF)
            elif line.startswith(".HIBYTES"):
                expr = line[len(".HIBYTES"):].strip()
                value = eval_offset_expr(expr, symbols)
                blocks[current].append((value >> 8) & 0xFF)

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


def find_longest_pointer_run(bank_data, scan_end, lookback=0x100, min_count=8):
    best = None
    scan_start = max(0, scan_end - lookback)

    for start in range(scan_start, scan_end):
        pos = start
        addresses = []
        while pos + 1 < scan_end:
            addr = bank_data[pos] | (bank_data[pos + 1] << 8)
            try:
                offset = cpu_addr_to_bank_offset(addr)
            except ValueError:
                break
            if offset >= start:
                break
            addresses.append(addr)
            pos += 2

        if len(addresses) < min_count:
            continue

        candidate = {
            "start": start,
            "end": pos,
            "addresses": addresses,
        }
        if best is None or len(candidate["addresses"]) > len(best["addresses"]):
            best = candidate

    if best is None:
        raise ValueError("Could not locate a valid pointer table run")
    return best


def build_pointer_labels(addresses, prefix):
    labels_by_addr = {}
    table_labels = []
    for addr in addresses:
        if addr not in labels_by_addr:
            labels_by_addr[addr] = f"{prefix}{len(labels_by_addr):02d}"
        table_labels.append(labels_by_addr[addr])
    ordered_addrs = sorted(labels_by_addr)
    return labels_by_addr, table_labels, ordered_addrs


def extract_demo_text_data(prg_data, blocks):
    bank2_data = prg_data[2 * PRG_BANK_SIZE : 3 * PRG_BANK_SIZE]
    title_palette = bytes(blocks["TitlePaletteTransferRecord"])
    title_palette_pos = find_unique_pattern(
        bank2_data, title_palette, "TitlePaletteTransferRecord in bank 2"
    )

    pointer_run = find_longest_pointer_run(bank2_data, title_palette_pos)
    addr_table_start = pointer_run["start"]
    addresses = pointer_run["addresses"]
    fields_start = min(cpu_addr_to_bank_offset(addr) for addr in addresses)
    fields_blob = bytes(bank2_data[fields_start:addr_table_start])

    return {
        "addresses": addresses,
        "fields_blob": fields_blob,
    }


def extract_credits_text_data(prg_data, blocks):
    bank2_data = prg_data[2 * PRG_BANK_SIZE : 3 * PRG_BANK_SIZE]
    credits_masks = bytes(blocks["CreditsPagesTextMasks"])
    credits_attrs = bytes(blocks["CreditsAttrs"])

    masks_pos = find_unique_pattern(
        bank2_data, credits_masks, "CreditsPagesTextMasks in bank 2"
    )
    attrs_pos = find_unique_pattern(bank2_data, credits_attrs, "CreditsAttrs in bank 2")

    credits_line_count = 0x17
    addrs_lo_start = masks_pos + len(credits_masks)
    addrs_hi_start = addrs_lo_start + credits_line_count
    text_lines_start = addrs_hi_start + credits_line_count

    addrs_lo = bank2_data[addrs_lo_start:addrs_hi_start]
    addrs_hi = bank2_data[addrs_hi_start:text_lines_start]
    addresses = [
        addrs_lo[index] | (addrs_hi[index] << 8) for index in range(credits_line_count)
    ]
    text_lines_blob = bytes(bank2_data[text_lines_start:attrs_pos])

    return {
        "addresses": addresses,
        "text_lines_blob": text_lines_blob,
    }


def parse_transfer_buf_end(data, start):
    """Parse NES PPU transfer buffer records to find the true $FF terminator.

    Record format:
      byte 0: PPU addr hi (>= $80 = terminator, buffer ends here)
      byte 1: PPU addr lo
      byte 2: control (bits 5:0 = count [0=64], bit 6 = inc mode, bit 7 = repeat)
      If repeat (bit 7): 1 data byte (repeated count times)
      If sequential: count data bytes
    """
    pos = start
    while pos < len(data):
        first = data[pos]
        if first >= 0x80:
            return pos  # terminator byte
        if pos + 2 >= len(data):
            raise ValueError(f"Transfer buf truncated at offset {pos}")
        control = data[pos + 2]
        count = control & 0x3F
        if count == 0:
            count = 64
        pos += 3  # past header
        if control & 0x80:
            pos += 1  # repeat mode: 1 data byte
        else:
            pos += count  # sequential: count data bytes
    raise ValueError(f"Transfer buf: no terminator found starting at {start}")


def extract_frontend_transfer_blobs(prg_data, z06_blocks):
    bank6_data = prg_data[6 * PRG_BANK_SIZE : 7 * PRG_BANK_SIZE]
    mode11_bottom = bytes(z06_blocks["Mode11PlayAreaAttrsBottomHalfTransferBuf"])
    marker_pos = find_unique_pattern(
        bank6_data,
        mode11_bottom,
        "Mode11PlayAreaAttrsBottomHalfTransferBuf in bank 6",
    )

    story_start = marker_pos + len(mode11_bottom)
    story_term = parse_transfer_buf_end(bank6_data, story_start)
    story_blob = bytes(bank6_data[story_start : story_term + 1])

    game_title_start = story_term + 1
    game_title_term = parse_transfer_buf_end(bank6_data, game_title_start)
    game_title_blob = bytes(bank6_data[game_title_start : game_title_term + 1])

    return {
        "story_tile_attr_transfer_buf": story_blob,
        "game_title_transfer_buf": game_title_blob,
    }


# ---------------------------------------------------------------------------
# C-array emission helpers
# ---------------------------------------------------------------------------

def bytes_to_c_array(data, var_name, bytes_per_line=16):
    size = len(data)
    lines = ["/* Auto-generated by tools/extract_frontend.py - do not edit. */"]
    lines.append(f"const unsigned long {var_name}_size = {size}UL;")
    lines.append(f"const unsigned char {var_name}[{size}] = {{")
    for i in range(0, size, bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def write_c_file(path, content):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print(f"  Wrote {path} ({len(content)} bytes)")


def write_manifest(out_dir, all_blocks_meta, rom_sha256):
    """Merge this extractor's blocks into data/text/MANIFEST.json.

    tools/extract_demo_text.py writes the same manifest. Both used to
    clobber it wholesale, so whichever ran last won and a full builder run
    silently dropped the other's entries. Merging by (name, file) makes the
    result independent of extractor order.
    """
    path = os.path.join(out_dir, "MANIFEST.json")

    existing = []
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                prior = json.load(f)
            if isinstance(prior.get("blocks"), list):
                existing = prior["blocks"]
        except (OSError, ValueError) as exc:
            print(f"  WARN: ignoring unreadable {path}: {exc}")

    def key(block):
        # (name, file), not name alone: DemoLineTextAddrs is emitted by both
        # this extractor and extract_demo_text.py into different .c files.
        return (str(block.get("name", "")), str(block.get("file", "")))

    merged = {key(b): b for b in existing}
    for b in all_blocks_meta:
        merged[key(b)] = b          # this run's blocks win on collision

    manifest = {
        "schema_version": 1,
        "nes_rom_sha256": rom_sha256,
        "blocks": sorted(merged.values(), key=key),
    }
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"  Wrote {path} ({len(manifest['blocks'])} blocks, "
          f"{len(all_blocks_meta)} from this run)")


# ---------------------------------------------------------------------------
# C output writers
# ---------------------------------------------------------------------------

def write_frontend_ui_c(out_dir, blocks):
    ordered = [
        "ModeFTitleTransferBuf",
        "ModeFSaveSlotTemplatePatchRegister",
        "ModeFSaveSlotTemplateTransferBuf",
        "Mode1SlotLineTransferBuf",
        "Mode1DeathCountsTransferBuf",
        "LinkColors",
        "Mode1CursorSpriteTriplet",
        "Mode1CursorSpriteYs",
        "ModeEandFSlotCursorYs",
        "ModeEandFCursorSprites",
        "ModeE_CharMap",
        "ModeE_CharBoardYOffsetsAndBounds",
        "SlotToInitialNameCharTransferHeaders",
        "DeletedSlotBlankNameTransferBuf",
        "SlotToBlankNameTransferBufEndOffset",
        "SlotToNameOffset",
    ]
    chunks = []
    meta = []
    offset = 0
    for label in ordered:
        data = bytes(blocks[label])
        meta.append({"name": label, "file": "nes_frontend_ui.c",
                     "var_name": "nes_frontend_ui",
                     "byte_offset": offset, "byte_size": len(data)})
        chunks.append(data)
        offset += len(data)
    payload = b"".join(chunks)
    write_c_file(os.path.join(out_dir, "nes_frontend_ui.c"),
                 bytes_to_c_array(payload, "nes_frontend_ui"))
    return meta


def write_frontend_palettes_c(out_dir, blocks):
    ordered = [
        "TitlePaletteTransferRecord",
        "StoryPaletteTransferRecord",
        "TriforcePaletteTransferRecord",
        "TriforceGlowingColors",
        "DemoPhase0Subphase1Palettes",
    ]
    chunks = []
    meta = []
    offset = 0
    for label in ordered:
        data = bytes(blocks[label])
        meta.append({"name": label, "file": "nes_frontend_palettes.c",
                     "var_name": "nes_frontend_palettes",
                     "byte_offset": offset, "byte_size": len(data)})
        chunks.append(data)
        offset += len(data)
    payload = b"".join(chunks)
    write_c_file(os.path.join(out_dir, "nes_frontend_palettes.c"),
                 bytes_to_c_array(payload, "nes_frontend_palettes"))
    return meta


def write_save_tables_c(out_dir, blocks):
    ordered = [
        "SaveSlotHeartsAddrsLo",
        "SaveSlotHeartsAddrsHi",
        "ProfileNameAddrsLo",
        "ProfileNameAddrsHi",
    ]
    chunks = []
    meta = []
    offset = 0
    for label in ordered:
        data = bytes(blocks[label])
        meta.append({"name": label, "file": "nes_frontend_save.c",
                     "var_name": "nes_frontend_save",
                     "byte_offset": offset, "byte_size": len(data)})
        chunks.append(data)
        offset += len(data)
    payload = b"".join(chunks)
    write_c_file(os.path.join(out_dir, "nes_frontend_save.c"),
                 bytes_to_c_array(payload, "nes_frontend_save"))
    return meta


def write_text_tables_c(out_dir, blocks):
    ordered = [
        "PlayAreaAttr0TransferBuf",
        "ThanksText",
        "ThanksTextboxCharTransferRecTemplate",
        "ThanksTextboxLineAddrsLo",
        "EndingFlashColors",
        "PeaceTextboxCharTransferRecTemplate",
        "PeaceTextboxCharAddrsLo",
        "PeaceText",
        "CreditsLastScreenList",
        "CreditsLastVscrollList",
        "CreditLineVramAddrsHi",
        "CreditsPagesTextMasks",
        "CreditsAttrs",
    ]
    chunks = []
    meta = []
    offset = 0
    for label in ordered:
        data = bytes(blocks[label])
        meta.append({"name": label, "file": "nes_frontend_text.c",
                     "var_name": "nes_frontend_text",
                     "byte_offset": offset, "byte_size": len(data)})
        chunks.append(data)
        offset += len(data)
    payload = b"".join(chunks)
    write_c_file(os.path.join(out_dir, "nes_frontend_text.c"),
                 bytes_to_c_array(payload, "nes_frontend_text"))
    return meta


def write_demo_text_c(out_dir, blocks, demo_text_data):
    """Emit nes_frontend_demo_text.c: DemoLineAttrs + pointer table + field blobs."""
    demo_line_attrs = bytes(blocks["DemoLineAttrs"])

    # Pointer table: flat LE 16-bit NES CPU addresses
    addrs_raw = b"".join(
        bytes([addr & 0xFF, (addr >> 8) & 0xFF])
        for addr in demo_text_data["addresses"]
    )

    fields_blob = demo_text_data["fields_blob"]

    payload = demo_line_attrs + addrs_raw + fields_blob
    meta = [
        {"name": "DemoLineAttrs", "file": "nes_frontend_demo_text.c",
         "var_name": "nes_frontend_demo_text",
         "byte_offset": 0, "byte_size": len(demo_line_attrs)},
        {"name": "DemoLineTextAddrs", "file": "nes_frontend_demo_text.c",
         "var_name": "nes_frontend_demo_text",
         "byte_offset": len(demo_line_attrs), "byte_size": len(addrs_raw),
         "entry_count": len(demo_text_data["addresses"]),
         "format": "u16le_nes_cpu_addr"},
        {"name": "DemoTextFieldsBlob", "file": "nes_frontend_demo_text.c",
         "var_name": "nes_frontend_demo_text",
         "byte_offset": len(demo_line_attrs) + len(addrs_raw),
         "byte_size": len(fields_blob)},
    ]
    write_c_file(os.path.join(out_dir, "nes_frontend_demo_text.c"),
                 bytes_to_c_array(payload, "nes_frontend_demo_text"))
    return meta


def write_credits_text_c(out_dir, credits_text_data):
    """Emit nes_frontend_credits.c: pointer table + text line blobs."""
    addrs_raw = b"".join(
        bytes([addr & 0xFF, (addr >> 8) & 0xFF])
        for addr in credits_text_data["addresses"]
    )
    text_lines_blob = credits_text_data["text_lines_blob"]
    payload = addrs_raw + text_lines_blob

    meta = [
        {"name": "CreditsTextAddrs", "file": "nes_frontend_credits.c",
         "var_name": "nes_frontend_credits",
         "byte_offset": 0, "byte_size": len(addrs_raw),
         "entry_count": len(credits_text_data["addresses"]),
         "format": "u16le_nes_cpu_addr"},
        {"name": "CreditsTextLinesBlob", "file": "nes_frontend_credits.c",
         "var_name": "nes_frontend_credits",
         "byte_offset": len(addrs_raw), "byte_size": len(text_lines_blob)},
    ]
    write_c_file(os.path.join(out_dir, "nes_frontend_credits.c"),
                 bytes_to_c_array(payload, "nes_frontend_credits"))
    return meta


def write_frontend_transfers_c(out_dir, transfer_blobs):
    """Emit nes_frontend_transfers.c: both PPU transfer buffer blobs."""
    story = transfer_blobs["story_tile_attr_transfer_buf"]
    title = transfer_blobs["game_title_transfer_buf"]
    payload = story + title

    meta = [
        {"name": "StoryTileAttrTransferBuf", "file": "nes_frontend_transfers.c",
         "var_name": "nes_frontend_transfers",
         "byte_offset": 0, "byte_size": len(story)},
        {"name": "GameTitleTransferBuf", "file": "nes_frontend_transfers.c",
         "var_name": "nes_frontend_transfers",
         "byte_offset": len(story), "byte_size": len(title)},
    ]
    write_c_file(os.path.join(out_dir, "nes_frontend_transfers.c"),
                 bytes_to_c_array(payload, "nes_frontend_transfers"))
    return meta


# ---------------------------------------------------------------------------
# Legacy .inc writers (only called with --legacy-inc)
# ---------------------------------------------------------------------------

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


def write_legacy_inc_files(project_root, blocks, demo_text_data, credits_text_data,
                           frontend_transfer_blobs):
    data_dir = os.path.join(project_root, "src", "data")
    os.makedirs(data_dir, exist_ok=True)

    # frontend_ui.inc
    ordered_ui = [
        "ModeFTitleTransferBuf",
        "ModeFSaveSlotTemplatePatchRegister",
        "ModeFSaveSlotTemplateTransferBuf",
        "Mode1SlotLineTransferBuf",
        "Mode1DeathCountsTransferBuf",
        "LinkColors",
        "Mode1CursorSpriteTriplet",
        "Mode1CursorSpriteYs",
        "ModeEandFSlotCursorYs",
        "ModeEandFCursorSprites",
        "ModeE_CharMap",
        "ModeE_CharBoardYOffsetsAndBounds",
        "SlotToInitialNameCharTransferHeaders",
        "DeletedSlotBlankNameTransferBuf",
        "SlotToBlankNameTransferBufEndOffset",
        "SlotToNameOffset",
    ]
    lines = ["; Frontend and menu UI tables extracted from NES Zelda Z_02",
             "; Auto-generated by extract_frontend.py - DO NOT EDIT", ""]
    for label in ordered_ui:
        lines.append(data_to_inc_bytes(bytes(blocks[label]), label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "frontend_ui.inc"), lines[:-1])

    # frontend_palettes.inc
    ordered_pal = [
        "TitlePaletteTransferRecord",
        "StoryPaletteTransferRecord",
        "TriforcePaletteTransferRecord",
        "TriforceGlowingColors",
        "DemoPhase0Subphase1Palettes",
    ]
    lines = ["; Frontend palette and demo color tables extracted from NES Zelda Z_02",
             "; Auto-generated by extract_frontend.py - DO NOT EDIT", ""]
    for label in ordered_pal:
        lines.append(data_to_inc_bytes(bytes(blocks[label]), label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "frontend_palettes.inc"), lines[:-1])

    # save_tables.inc
    ordered_save = [
        "SaveSlotHeartsAddrsLo",
        "SaveSlotHeartsAddrsHi",
        "ProfileNameAddrsLo",
        "ProfileNameAddrsHi",
    ]
    lines = ["; Save/profile helper tables extracted from NES Zelda Z_02",
             "; Auto-generated by extract_frontend.py - DO NOT EDIT", ""]
    for label in ordered_save:
        lines.append(data_to_inc_bytes(bytes(blocks[label]), label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "save_tables.inc"), lines[:-1])

    # text.inc
    ordered_text = [
        "PlayAreaAttr0TransferBuf",
        "ThanksText",
        "ThanksTextboxCharTransferRecTemplate",
        "ThanksTextboxLineAddrsLo",
        "EndingFlashColors",
        "PeaceTextboxCharTransferRecTemplate",
        "PeaceTextboxCharAddrsLo",
        "PeaceText",
        "CreditsLastScreenList",
        "CreditsLastVscrollList",
        "CreditLineVramAddrsHi",
        "CreditsPagesTextMasks",
        "CreditsAttrs",
    ]
    lines = ["; Ending and credits text-support tables extracted from NES Zelda Z_02",
             "; Auto-generated by extract_frontend.py - DO NOT EDIT", ""]
    for label in ordered_text:
        lines.append(data_to_inc_bytes(bytes(blocks[label]), label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "text.inc"), lines[:-1])

    # demo_text.inc
    labels_by_addr, table_labels, ordered_addrs = build_pointer_labels(
        demo_text_data["addresses"], "DemoTextField"
    )
    lines = ["; Demo/story text field blobs extracted from NES Zelda bank 2",
             "; Auto-generated by extract_frontend.py - DO NOT EDIT", "",
             data_to_inc_bytes(bytes(blocks["DemoLineAttrs"]), "DemoLineAttrs"), "",
             labels_to_inc_words(table_labels, "DemoLineTextAddrs"), ""]
    write_labeled_records(lines, demo_text_data["fields_blob"], ordered_addrs, labels_by_addr)
    write_text_file(os.path.join(data_dir, "demo_text.inc"), lines[:-1])

    # credits_text.inc
    labels_by_addr, table_labels, ordered_addrs = build_pointer_labels(
        credits_text_data["addresses"], "CreditsTextLine"
    )
    lines = ["; Credits text line blobs extracted from NES Zelda bank 2",
             "; Auto-generated by extract_frontend.py - DO NOT EDIT", "",
             labels_to_inc_words(table_labels, "CreditsTextAddrs"), ""]
    write_labeled_records(lines, credits_text_data["text_lines_blob"], ordered_addrs,
                          labels_by_addr)
    write_text_file(os.path.join(data_dir, "credits_text.inc"), lines[:-1])

    # frontend_transfers.inc
    lines = ["; Frontend transfer buffers extracted from NES Zelda bank 6",
             "; Auto-generated by extract_frontend.py - DO NOT EDIT", "",
             data_to_inc_bytes(
                 frontend_transfer_blobs["story_tile_attr_transfer_buf"],
                 "StoryTileAttrTransferBuf"),
             "",
             data_to_inc_bytes(
                 frontend_transfer_blobs["game_title_transfer_buf"],
                 "GameTitleTransferBuf")]
    write_text_file(os.path.join(data_dir, "frontend_transfers.inc"), lines)

    # .dat binaries
    ref_dat_dir = os.path.join(project_root, "reference", "aldonunez", "dat")
    os.makedirs(ref_dat_dir, exist_ok=True)
    for filename, key in [
        ("StoryTileAttrTransferBuf.dat", "story_tile_attr_transfer_buf"),
        ("GameTitleTransferBuf.dat", "game_title_transfer_buf"),
    ]:
        path = os.path.join(ref_dat_dir, filename)
        with open(path, "wb") as f:
            f.write(frontend_transfer_blobs[key])
        print(f"  Wrote {path} ({len(frontend_transfer_blobs[key])} bytes)")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Extract NES-reference frontend tables to data/text/ (nes_frontend_*.c)"
    )
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Output directory for C arrays and MANIFEST.json (default: <repo>/data/text/)",
    )
    parser.add_argument(
        "--legacy-inc",
        action="store_true",
        help=(
            "Also emit legacy vasm .inc files to <repo>/src/data/ and "
            ".dat binaries to reference/aldonunez/dat/ for transitional callers"
        ),
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    z02_path = os.path.join(project_root, "reference", "aldonunez", "Z_02.asm")
    z06_path = os.path.join(project_root, "reference", "aldonunez", "Z_06.asm")
    vars_path = os.path.join(project_root, "reference", "aldonunez", "Variables.inc")
    rom_path_env = os.environ.get("ZELDA_NES_ROM", "")
    if rom_path_env and os.path.isfile(rom_path_env):
        rom_path = rom_path_env
    else:
        rom_path = os.path.join(project_root, "Legend of Zelda, The (USA).nes")

    for required_path in [z02_path, z06_path, vars_path, rom_path]:
        if not os.path.exists(required_path):
            print(f"ERROR: required file not found: {required_path}")
            sys.exit(1)

    rom_sha256 = sha256_of_file(rom_path)
    if rom_sha256 != NES_ROM_SHA256:
        print(f"ERROR: ROM SHA256 mismatch. Expected {NES_ROM_SHA256}, got {rom_sha256}")
        sys.exit(1)

    out_dir = args.out_dir if args.out_dir else os.path.join(project_root, "data", "text")
    os.makedirs(out_dir, exist_ok=True)

    print(f"Parsing variables: {vars_path}")
    symbols = parse_variables(vars_path)

    print(f"Parsing reference data: {z02_path}")
    blocks = parse_asm_data_blocks(z02_path, Z02_TARGET_LABELS, symbols)
    z06_blocks = parse_asm_data_blocks(z06_path, Z06_TARGET_LABELS, {})
    prg_data = read_ines_prg(rom_path)
    demo_text_data = extract_demo_text_data(prg_data, blocks)
    credits_text_data = extract_credits_text_data(prg_data, blocks)
    frontend_transfer_blobs = extract_frontend_transfer_blobs(prg_data, z06_blocks)

    print(f"\nWriting NES-reference C-array frontend data files to {out_dir}...")
    all_meta = []
    all_meta.extend(write_frontend_ui_c(out_dir, blocks))
    all_meta.extend(write_frontend_palettes_c(out_dir, blocks))
    all_meta.extend(write_save_tables_c(out_dir, blocks))
    all_meta.extend(write_text_tables_c(out_dir, blocks))
    all_meta.extend(write_demo_text_c(out_dir, blocks, demo_text_data))
    all_meta.extend(write_credits_text_c(out_dir, credits_text_data))
    all_meta.extend(write_frontend_transfers_c(out_dir, frontend_transfer_blobs))

    # MANIFEST.json for data/text/ is shared with extract_demo_text.py output.
    # We update it here; build_data.py will run extractors in order so this
    # runs after extract_demo_text.py and merges into the same MANIFEST.
    # To avoid overwriting demo_text extractor's manifest, we append a
    # separate key for the frontend blocks.
    manifest_path = os.path.join(out_dir, "MANIFEST.json")
    if os.path.exists(manifest_path):
        with open(manifest_path, "r", encoding="utf-8") as f:
            existing = json.load(f)
        existing_blocks = existing.get("blocks", [])
        # Only keep blocks not from this extractor (avoid duplicates on re-run)
        keep = [b for b in existing_blocks
                if not b.get("file", "").startswith("nes_frontend_")]
        merged_blocks = keep + all_meta
        manifest = {
            "schema_version": 1,
            "nes_rom_sha256": rom_sha256,
            "blocks": merged_blocks,
        }
    else:
        manifest = {
            "schema_version": 1,
            "nes_rom_sha256": rom_sha256,
            "blocks": all_meta,
        }
    with open(manifest_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"  Wrote {manifest_path}")

    if args.legacy_inc:
        print(f"\nWriting legacy .inc files...")
        write_legacy_inc_files(project_root, blocks, demo_text_data,
                               credits_text_data, frontend_transfer_blobs)

    total_bytes = sum(len(block) for block in blocks.values())
    total_bytes += len(demo_text_data["fields_blob"])
    total_bytes += len(demo_text_data["addresses"]) * 2
    total_bytes += len(credits_text_data["text_lines_blob"])
    total_bytes += len(credits_text_data["addresses"]) * 2
    total_bytes += len(frontend_transfer_blobs["story_tile_attr_transfer_buf"])
    total_bytes += len(frontend_transfer_blobs["game_title_transfer_buf"])

    print("\n=== Frontend extraction complete ===")
    print(f"  Total extracted frontend/text table bytes: {total_bytes}")
    print(f"  Manifest entries: {len(all_meta)}")
    print(f"  Output directory: {out_dir}")


if __name__ == "__main__":
    main()
