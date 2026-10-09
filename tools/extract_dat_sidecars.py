#!/usr/bin/env python3
"""
extract_dat_sidecars.py - Extract all missing .INCBIN data as raw .dat files
from the NES ROM into reference/aldonunez/dat/.

This lets the transpiler emit real game data instead of zero stubs.
"""

import os
import re
import sys

INES_HEADER_SIZE = 16
PRG_BANK_SIZE = 0x4000

BANK_ISR_SIGNATURE = bytes([
    0x78, 0xD8, 0xA9, 0x00, 0x8D, 0x00, 0x20, 0xA2,
    0xFF, 0x9A, 0xAD, 0x02, 0x20,
])

LEVEL_BLOCK_SIZE = 0x0300  # 768 bytes
LEVEL_INFO_SIZE  = 0x0100  # 256 bytes
ROOM_LAYOUT_OW_SIZE = 0x0390  # 912 bytes
ROOM_LAYOUT_UW_SIZE = 0x0300  # 768 bytes

LEVEL_BLOCK_LABELS = [
    "LevelBlockOW", "LevelBlockUW1Q1", "LevelBlockUW2Q1",
    "LevelBlockUW1Q2", "LevelBlockUW2Q2",
]

LEVEL_INFO_LABELS = [
    "LevelInfoOW", "LevelInfoUW1", "LevelInfoUW2", "LevelInfoUW3",
    "LevelInfoUW4", "LevelInfoUW5", "LevelInfoUW6", "LevelInfoUW7",
    "LevelInfoUW8", "LevelInfoUW9",
]


def read_nes_rom(path):
    with open(path, "rb") as f:
        header = f.read(INES_HEADER_SIZE)
    if header[:4] != b"NES\x1a":
        raise ValueError(f"Not a valid iNES ROM: {path}")
    prg_banks = header[4]
    with open(path, "rb") as f:
        f.seek(INES_HEADER_SIZE)
        return f.read(prg_banks * PRG_BANK_SIZE)


def get_bank(prg_data, bank_num):
    offset = bank_num * PRG_BANK_SIZE
    return prg_data[offset:offset + PRG_BANK_SIZE], offset


def find_pattern(data, pattern, description):
    pos = data.find(pattern)
    if pos < 0:
        raise ValueError(f"Could not find {description}")
    return pos


def parse_asm_byte_blocks(asm_path, target_labels):
    """Parse .BYTE blocks from an asm file for specific labels."""
    blocks = {}
    current = None
    label_re = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")

    with open(asm_path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.split(";", 1)[0].strip()
            if not line:
                if current and blocks[current]:
                    current = None
                continue
            m = label_re.match(line)
            if m:
                label = m.group(1)
                current = label if label in target_labels else None
                if current and current not in blocks:
                    blocks[current] = bytearray()
                continue
            if current is None:
                continue
            if line.startswith(".BYTE"):
                tokens = [t.strip() for t in line[5:].split(",") if t.strip()]
                for t in tokens:
                    if t.startswith("$"):
                        blocks[current].append(int(t[1:], 16))
                    elif t.isdigit():
                        blocks[current].append(int(t))
    return blocks


# === Bank 6: LevelBlock and LevelInfo ===

def extract_bank6(prg_data):
    bank6, bank6_rom = get_bank(prg_data, 6)

    # Find FF-padding run before level data
    level_block_start = None
    for i in range(len(bank6) - 64):
        if all(b == 0xFF for b in bank6[i:i+64]):
            end = i
            while end < len(bank6) and bank6[end] == 0xFF:
                end += 1
            level_block_start = end
            break
    if level_block_start is None:
        raise ValueError("Could not find padding before level block data in bank 6")

    results = {}
    offset = level_block_start

    for label in LEVEL_BLOCK_LABELS:
        rom_off = bank6_rom + offset
        results[label] = prg_data[rom_off:rom_off + LEVEL_BLOCK_SIZE]
        print(f"  {label}: ROM ${rom_off:05X}, {LEVEL_BLOCK_SIZE} bytes")
        offset += LEVEL_BLOCK_SIZE

    for label in LEVEL_INFO_LABELS:
        rom_off = bank6_rom + offset
        results[label] = prg_data[rom_off:rom_off + LEVEL_INFO_SIZE]
        print(f"  {label}: ROM ${rom_off:05X}, {LEVEL_INFO_SIZE} bytes")
        offset += LEVEL_INFO_SIZE

    return results


# === Bank 5: RoomLayouts + ObjLists ===

def extract_bank5(prg_data, z05_path):
    bank5, bank5_rom = get_bank(prg_data, 5)

    anchors = parse_asm_byte_blocks(z05_path, {
        "RoomLayoutOWCave0", "RoomLayoutUWCellar0",
    })

    results = {}

    # RoomLayoutsOW: right before RoomLayoutOWCave0
    cave0_bytes = bytes(anchors["RoomLayoutOWCave0"])
    cave0_pos = find_pattern(bank5, cave0_bytes, "RoomLayoutOWCave0 in bank 5")
    ow_start = cave0_pos - ROOM_LAYOUT_OW_SIZE
    results["RoomLayoutsOW"] = bank5[ow_start:ow_start + ROOM_LAYOUT_OW_SIZE]
    print(f"  RoomLayoutsOW: bank 5 offset ${ow_start:04X}, {ROOM_LAYOUT_OW_SIZE} bytes")

    # RoomLayoutsUW: before UW heaps, which are before RoomLayoutUWCellar0
    cellar0_bytes = bytes(anchors["RoomLayoutUWCellar0"])
    cellar0_pos = find_pattern(bank5, cellar0_bytes, "RoomLayoutUWCellar0 in bank 5")
    uw_heap_labels = [f"ColumnHeapUW{i}" for i in range(10)]
    uw_heaps = parse_asm_byte_blocks(z05_path, set(uw_heap_labels))
    uw_heap_total = sum(len(uw_heaps.get(l, b"")) for l in uw_heap_labels)
    uw_start = cellar0_pos - uw_heap_total - ROOM_LAYOUT_UW_SIZE
    results["RoomLayoutsUW"] = bank5[uw_start:uw_start + ROOM_LAYOUT_UW_SIZE]
    print(f"  RoomLayoutsUW: bank 5 offset ${uw_start:04X}, {ROOM_LAYOUT_UW_SIZE} bytes")

    # ObjLists: find via NES address table at bank start.
    # Bank 5 doesn't start with ObjLists. We need to find ObjLists by
    # reading the ObjListAddrs table and figuring out where the data starts/ends.
    # ObjListAddrs entries point into ObjLists. Find them by searching
    # for the ObjListAddrs anchor pattern.
    obj_anchors = parse_asm_byte_blocks(z05_path, {"ObjListAddrs"})
    if "ObjListAddrs" in obj_anchors and len(obj_anchors["ObjListAddrs"]) >= 4:
        obj_addrs_bytes = bytes(obj_anchors["ObjListAddrs"])
        # Find this table in bank 5
        obj_addrs_pos = find_pattern(bank5, obj_addrs_bytes[:8], "ObjListAddrs in bank 5")
        # ObjListAddrs entries are NES .ADDR (16-bit LE) pointers into ObjLists.
        # The first entry points to the start of ObjLists data.
        first_obj_addr = int.from_bytes(bank5[obj_addrs_pos:obj_addrs_pos+2], "little")
        obj_lists_start = first_obj_addr - 0x8000
        obj_lists_size = obj_addrs_pos - obj_lists_start
        if obj_lists_size > 0 and obj_lists_size < 0x2000:
            results["ObjLists"] = bank5[obj_lists_start:obj_lists_start + obj_lists_size]
            print(f"  ObjLists: bank 5 offset ${obj_lists_start:04X}, {obj_lists_size} bytes")
        else:
            print(f"  WARNING: ObjLists size ${obj_lists_size:04X} looks wrong, skipping")
    else:
        print("  WARNING: Could not find ObjListAddrs, skipping ObjLists")

    return results


# === Bank 3: PatternBlock data ===

def extract_bank3(prg_data, z03_path):
    """Extract PatternBlock .dat files from bank 3 using known sizes."""
    bank3, bank3_rom = get_bank(prg_data, 3)

    # Read the address tables from the start of bank 3 ROM.
    # Layout (from Z_03.asm):
    #   LevelPatternBlockSrcAddrs: 10 x .ADDR  (20 bytes, offset 0)
    #   BossPatternBlockSrcAddrs:  10 x .ADDR  (20 bytes, offset 20)
    #   PatternBlockSrcAddrsUW:     2 x .ADDR  ( 4 bytes, offset 40)
    #   PatternBlockSrcAddrsOW:     2 x .ADDR  ( 4 bytes, offset 44)

    def read_addr(off):
        return int.from_bytes(bank3[off:off+2], "little")

    # Known sizes from PatternBlockSizes in Z_03.asm:
    #   PatternBlockSizesOW: .DBYT $0820, $0720
    #   PatternBlockSizesUW: .DBYT $0820, $0100, $0220, $0400
    blocks = [
        ("PatternBlockUWBG",          read_addr(40), 0x0820),  # UW BG
        ("PatternBlockUWSP",          read_addr(42), 0x0100),  # UW SP base
        ("PatternBlockOWBG",          read_addr(44), 0x0820),  # OW BG
        ("PatternBlockOWSP",          read_addr(46), 0x0720),  # OW SP
        ("PatternBlockUWSP127",       read_addr(0),  0x0220),  # level-specific SP
        ("PatternBlockUWSP358",       read_addr(6),  0x0220),
        ("PatternBlockUWSP469",       read_addr(8),  0x0220),
        ("PatternBlockUWSPBoss1257",  read_addr(20), 0x0400),  # boss SP
        ("PatternBlockUWSPBoss3468",  read_addr(26), 0x0400),
        ("PatternBlockUWSPBoss9",     read_addr(38), 0x0400),
    ]

    results = {}
    for label, nes_addr, size in blocks:
        bank_offset = nes_addr - 0x8000
        data = bank3[bank_offset:bank_offset + size]
        results[label] = data
        print(f"  {label}: NES ${nes_addr:04X}, offset ${bank_offset:04X}, {size} bytes")

    return results


# === Bank 1: PersonText ===

def extract_bank1(prg_data, z01_path):
    bank1, _ = get_bank(prg_data, 1)

    anchors = parse_asm_byte_blocks(z01_path, {"OverworldPersonTextSelectors"})
    ow_selectors = bytes(anchors.get("OverworldPersonTextSelectors", b""))
    if len(ow_selectors) < 4:
        print("  WARNING: Could not find OverworldPersonTextSelectors, skipping")
        return {}

    sel_pos = find_pattern(bank1, ow_selectors[:16], "OverworldPersonTextSelectors in bank 1")

    # PersonTextAddrs is at bank start; first entry points to PersonText start
    first_addr = int.from_bytes(bank1[0:2], "little")
    if 0x8000 <= first_addr < 0xC000:
        start = first_addr - 0x8000
        size = sel_pos - start
        if 0 < size < 0x2000:
            print(f"  PersonText: bank 1 offset ${start:04X}, {size} bytes")
            return {"PersonText": bank1[start:start + size]}

    print("  WARNING: Could not determine PersonText boundaries, skipping")
    return {}


# === Fixed-offset blocks ===
#
# Sidecars the anchor searches above cannot locate (they are themselves
# the anchors, or nothing in the asm describes them). The input ROM is
# hash-pinned by tools/builder/build.py, so PRG offsets are exact. The
# .dat offsets agree with upstream aldonunez/zelda1-disassembly
# src/bins.xml; every block is a unique match in the pinned ROM.
# (name, PRG offset, length)
FIXED_DATS = (
    ("DemoSpritePatterns",        0x04DB4, 2304),
    ("DemoBackgroundPatterns",    0x056B4, 2080),
    ("CommonSpritePatterns",      0x0807F, 1792),
    ("CommonBackgroundPatterns",  0x0877F, 1792),
    ("CommonMiscPatterns",        0x08E7F, 224),
    ("DemoTextFields",            0x0929A, 531),
    ("CreditsTextLines",          0x0AC5C, 414),
    ("ObjLists",                  0x14676, 201),
    ("CommonDataBlock_Bank6",     0x19D00, 142),
    ("PcmSamples",                0x1C000, 9216),
)

# Address tables the reference asm .INCLUDEs as ".BYTE" text, 16 per line.
# ObjListAddrs keeps the extent the reference asm was written against
# (one byte of ObjLists before the table, code after it).
FIXED_INCS = (
    ("PersonTextAddrs",     0x04000, 76),
    ("DemoLineTextAddrs",   0x094AD, 58),
    ("CreditsTextAddrsLo",  0x0AC2E, 23),
    ("CreditsTextAddrsHi",  0x0AC45, 23),
    ("ObjListAddrs",        0x1473E, 1088),
)


def format_inc(data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(".BYTE " + ", ".join(f"${b:02X}" for b in data[i:i + 16]))
    return "\n".join(lines) + "\n"


# === Main ===

def main():
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    rom_path_env = os.environ.get("ZELDA_NES_ROM", "")
    if rom_path_env and os.path.isfile(rom_path_env):
        rom_path = rom_path_env
    else:
        rom_path = os.path.join(project_root, "Legend of Zelda, The (USA).nes")
    ref_dir = os.path.join(project_root, "reference", "aldonunez")
    dat_dir = os.path.join(ref_dir, "dat")

    z01_path = os.path.join(ref_dir, "Z_01.asm")
    z03_path = os.path.join(ref_dir, "Z_03.asm")
    z05_path = os.path.join(ref_dir, "Z_05.asm")

    for p in [rom_path, z03_path, z05_path]:
        if not os.path.exists(p):
            print(f"ERROR: required file not found: {p}")
            sys.exit(1)

    os.makedirs(dat_dir, exist_ok=True)
    print(f"Reading NES ROM: {rom_path}")
    prg_data = read_nes_rom(rom_path)

    all_dat = {}

    print("\n--- Bank 6: Level blocks and level info ---")
    all_dat.update(extract_bank6(prg_data))

    print("\n--- Bank 5: Room layouts and ObjLists ---")
    all_dat.update(extract_bank5(prg_data, z05_path))

    print("\n--- Bank 3: Pattern blocks ---")
    all_dat.update(extract_bank3(prg_data, z03_path))

    if os.path.exists(z01_path):
        print("\n--- Bank 1: PersonText ---")
        all_dat.update(extract_bank1(prg_data, z01_path))

    for label, off, size in FIXED_DATS:
        all_dat.setdefault(label, prg_data[off:off + size])

    print(f"\n--- Writing {len(FIXED_INCS)} .inc tables to {dat_dir} ---")
    for label, off, size in FIXED_INCS:
        with open(os.path.join(dat_dir, f"{label}.inc"), "w", encoding="ascii", newline="\n") as f:
            f.write(format_inc(prg_data[off:off + size]))
        print(f"  {label}.inc - {size} bytes")

    # Write all .dat files
    print(f"\n--- Writing {len(all_dat)} .dat files to {dat_dir} ---")
    for label, data in sorted(all_dat.items()):
        dat_path = os.path.join(dat_dir, f"{label}.dat")
        with open(dat_path, "wb") as f:
            f.write(data)
        print(f"  {label}.dat - {len(data)} bytes")

    total = sum(len(d) for d in all_dat.values())
    print(f"\n=== Extraction complete: {len(all_dat)} files, {total} bytes total ===")

    # Check for zero-length files
    for label, data in all_dat.items():
        if len(data) == 0:
            print(f"  WARNING: {label}.dat is empty!")


if __name__ == "__main__":
    main()
