#!/usr/bin/env python3
"""
extract_chr.py - Extract and convert NES Zelda 1 tile data to Genesis format.

Reads the NES ROM and extracts all pattern block data, converting from
NES 2bpp (16 bytes/tile) to Genesis 4bpp (32 bytes/tile).

NES tile format (2bpp, 8x8):
  Bytes 0-7:  Bit plane 0 (one byte per row)
  Bytes 8-15: Bit plane 1 (one byte per row)
  Each pixel = 2 bits (plane1:plane0), values 0-3

Genesis tile format (4bpp, 8x8):
  32 bytes per tile, 4 bytes per row
  Each byte = 2 pixels (high nibble = left, low nibble = right)
  Each pixel = 4 bits, but we only use the low 2 for NES-sourced tiles

Primary outputs (C arrays, data/chr/):
  overworld_bg.c     -- overworld background tiles
  underworld_bg.c    -- dungeon background tiles
  sprites.c          -- shared sprite tiles
  bosses.c           -- boss sprite tiles
  common.c           -- common (always-loaded) tiles
  demo.c             -- demo/title screen tiles
  MANIFEST.json      -- NES tile id -> Genesis VRAM tile index mapping

Legacy outputs (--legacy-inc flag only, vasm .inc format):
  tiles_overworld.inc, tiles_underworld.inc, tiles_bosses.inc,
  tiles_common.inc, tiles_demo.inc, tiles_sprites.inc,
  tiles_overworld_bg.inc, tiles_underworld_bg.inc
"""

import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

# Canonical NES ROM SHA256 (locked in spec Section 0 / docs/audit/baseline_rom.md).
NES_ROM_SHA256 = "8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac"

# iNES header constants
INES_HEADER_SIZE = 16
PRG_BANK_SIZE = 0x4000  # 16 KB

# Pattern block sizes (from Z_03.asm)
PATTERN_BLOCKS = {
    # Bank 3 blocks (level patterns) - order matches INCBIN order in Z_03.asm
    'PatternBlockUWBG':         0x0820,  # Underworld background
    'PatternBlockOWBG':         0x0820,  # Overworld background
    'PatternBlockOWSP':         0x0720,  # Overworld sprites
    'PatternBlockUWSP358':      0x0220,  # Underworld sprites levels 3,5,8
    'PatternBlockUWSP469':      0x0220,  # Underworld sprites levels 4,6,9
    'PatternBlockUWSP':         0x0100,  # Underworld base sprites
    'PatternBlockUWSP127':      0x0220,  # Underworld sprites levels 1,2,7
    'PatternBlockUWSPBoss1257': 0x0400,  # Boss sprites levels 1,2,5,7
    'PatternBlockUWSPBoss3468': 0x0400,  # Boss sprites levels 3,4,6,8
    'PatternBlockUWSPBoss9':    0x0400,  # Boss sprites level 9
}

# Bank 1 blocks (demo/title patterns) - from Z_01.asm
DEMO_BLOCKS = {
    'DemoSpritePatterns':     0x0900,
    'DemoBackgroundPatterns': 0x0820,
}

# Zelda 1 bank-1 demo/title pattern seam verified from retail USA PRG-ROM:
#   DemoSpritePatterns      $0DB4-$16B3
#   DemoBackgroundPatterns  $16B4-$1ED3
#
# The old ISR-backtracking heuristic drifted into unrelated bytes and yielded
# "close but wrong" title-screen art on Genesis.
BANK1_PATTERN_DATA_START = 0x0DB4

# Bank 2 blocks (common patterns) - from Z_02.asm
COMMON_BLOCKS = {
    'CommonSpritePatterns':     0x0700,
    'CommonBackgroundPatterns': 0x0700,
    'CommonMiscPatterns':       0x00E0,
}

# Z_02.asm places an explicit 161-byte $FF padding block immediately after
# CommonMiscPatterns and before the following code block. We use that run as
# the bank-2 anchor instead of trying to infer the pattern data from the ISR.
COMMON_POST_PADDING_BYTES = 0x00A1

# Known byte signature at start of BANK_03_ISR (reset vector code)
BANK3_ISR_SIGNATURE = bytes([0x78, 0xD8, 0xA9, 0x00, 0x8D, 0x00, 0x20, 0xA2,
                              0xFF, 0x9A, 0xAD, 0x02, 0x20])

# Zelda 1's bank-3 pattern data is not packed flush against BANK_03_ISR.
# The actual verified seam for the USA PRG-ROM is bank-3 offset $011B:
#   PatternBlockUWBG      $011B-$093A
#   PatternBlockOWBG      $093B-$115A
#   PatternBlockOWSP      $115B-$187A
#   PatternBlockUWSP358   $187B-$1A9A
#   PatternBlockUWSP469   $1A9B-$1CBA
#   PatternBlockUWSP      $1CBB-$1DBA
#   PatternBlockUWSP127   $1DBB-$1FDA
#   PatternBlockUWSPBoss1257 $1FDB-$23DA
#   PatternBlockUWSPBoss3468 $23DB-$27DA
#   PatternBlockUWSPBoss9    $27DB-$2BDA
#
# This seam was proven by matching the live NES CHR-RAM dump for the real
# room-$77 start screen against PRG-ROM bytes. The old ISR-backtracking
# heuristic landed $138F bytes too late and produced "close but wrong" OW art.
BANK3_PATTERN_DATA_START = 0x011B

# Genesis VRAM tile assignments for each named block.
# Each block starts at a tile index; tiles are contiguous within the block.
# Convention (consistent with future overworld renderer in S3):
#   overworld_bg : tile   0  (VRAM $0000-$1FFF)  -- 130 tiles
#   underworld_bg: tile 256  (VRAM $2000-$3FFF)  -- 130 tiles
#   sprites      : tile 512  (VRAM $4000-$5FFF)  -- aggregated sprite tiles
#   bosses       : tile 640  (after sprites block)
#   common       : tile 704  (after bosses block)
#   demo         : tile 800  (after common block)
#
# The exact starts for sprites/bosses/common/demo are computed dynamically
# from tile counts, but the anchor is overworld_bg=0.
VRAM_TILE_OVERWORLD_BG   = 0
VRAM_TILE_UNDERWORLD_BG  = 256
VRAM_TILE_SPRITES_START  = 512


def read_ines_rom(path: str) -> tuple:
    """Read an iNES ROM and return PRG-ROM data with header info."""
    with open(path, 'rb') as f:
        header = f.read(INES_HEADER_SIZE)

    if header[:4] != b'NES\x1a':
        raise ValueError(f"Not a valid iNES ROM: {path}")

    prg_banks = header[4]
    chr_banks = header[5]
    flags6 = header[6]
    mapper = (flags6 >> 4) | (header[7] & 0xF0)

    print(f"  iNES header:")
    print(f"    PRG-ROM: {prg_banks} x 16KB = {prg_banks * 16}KB")
    print(f"    CHR-ROM: {chr_banks} x 8KB = {chr_banks * 8}KB")
    print(f"    Mapper:  {mapper} ({'MMC1' if mapper == 1 else 'unknown'})")

    with open(path, 'rb') as f:
        f.seek(INES_HEADER_SIZE)
        prg_data = f.read(prg_banks * PRG_BANK_SIZE)

    return prg_data, prg_banks


def find_bank3_pattern_data(prg_data: bytes) -> dict:
    """Find the start of pattern block data in bank 3."""
    bank3_offset = 3 * PRG_BANK_SIZE
    bank3_data = prg_data[bank3_offset:bank3_offset + PRG_BANK_SIZE]

    # BANK_03_ISR is still useful as a sanity-check, but the pattern block data
    # is not adjacent to it in the retail ROM.
    isr_pos = bank3_data.find(BANK3_ISR_SIGNATURE)
    if isr_pos < 0:
        raise ValueError("Could not find BANK_03_ISR signature in bank 3")

    print(f"  Found BANK_03_ISR at bank 3 offset ${isr_pos:04X} (ROM ${bank3_offset + isr_pos:05X})")
    total_data_size = sum(PATTERN_BLOCKS.values())
    data_start = BANK3_PATTERN_DATA_START
    data_end = data_start + total_data_size
    if data_end > isr_pos:
        raise ValueError(
            f"Verified bank-3 pattern seam ${data_start:04X}-${data_end - 1:04X} overlaps ISR at ${isr_pos:04X}"
        )

    print(f"  Total bank 3 pattern data: ${total_data_size:04X} ({total_data_size} bytes)")
    print(f"  Using verified bank 3 pattern-data seam at offset ${data_start:04X}")

    blocks = {}
    offset = data_start
    for name in [
        'PatternBlockUWBG',
        'PatternBlockOWBG',
        'PatternBlockOWSP',
        'PatternBlockUWSP358',
        'PatternBlockUWSP469',
        'PatternBlockUWSP',
        'PatternBlockUWSP127',
        'PatternBlockUWSPBoss1257',
        'PatternBlockUWSPBoss3468',
        'PatternBlockUWSPBoss9',
    ]:
        size = PATTERN_BLOCKS[name]
        rom_offset = bank3_offset + offset
        blocks[name] = prg_data[rom_offset:rom_offset + size]
        print(f"    {name}: ROM ${rom_offset:05X}, {size} bytes, {size // 16} tiles")
        offset += size

    return blocks


def find_demo_pattern_data(prg_data: bytes) -> dict:
    """Find demo/title pattern data in bank 1."""
    bank1_offset = 1 * PRG_BANK_SIZE
    bank1_data = prg_data[bank1_offset:bank1_offset + PRG_BANK_SIZE]

    # Keep ISR lookup as a guardrail only; the seam itself is explicit.
    isr_pos = bank1_data.find(BANK3_ISR_SIGNATURE)
    if isr_pos < 0:
        raise ValueError("Could not find BANK_01_ISR signature in bank 1")

    total_size = sum(DEMO_BLOCKS.values())
    data_start = BANK1_PATTERN_DATA_START
    data_end = data_start + total_size
    if data_end > isr_pos:
        raise ValueError(
            f"Verified bank-1 demo seam ${data_start:04X}-${data_end - 1:04X} overlaps ISR at ${isr_pos:04X}"
        )

    print(f"  Found BANK_01_ISR at bank 1 offset ${isr_pos:04X} (ROM ${bank1_offset + isr_pos:05X})")
    print(f"  Total bank 1 demo data: ${total_size:04X} ({total_size} bytes)")
    print(f"  Using verified bank 1 demo seam at offset ${data_start:04X}")

    blocks = {}
    offset = data_start
    for name, size in DEMO_BLOCKS.items():
        rom_offset = bank1_offset + offset
        blocks[name] = prg_data[rom_offset:rom_offset + size]
        print(f"    {name}: ROM ${rom_offset:05X}, {size} bytes, {size // 16} tiles")
        offset += size

    return blocks


def find_common_pattern_data(prg_data: bytes) -> dict:
    """Find common pattern data in bank 2."""
    bank2_offset = 2 * PRG_BANK_SIZE
    bank2_data = prg_data[bank2_offset:bank2_offset + PRG_BANK_SIZE]
    total_size = sum(COMMON_BLOCKS.values())
    padding_run = b'\xFF' * COMMON_POST_PADDING_BYTES
    data_start = None
    search_pos = 0
    while True:
        run_pos = bank2_data.find(padding_run, search_pos)
        if run_pos < 0:
            break
        candidate_start = run_pos - total_size
        candidate_end = run_pos + COMMON_POST_PADDING_BYTES
        if candidate_start >= 0 and candidate_end < len(bank2_data) and bank2_data[candidate_end] != 0xFF:
            data_start = candidate_start
            print(f"  Found bank 2 common-pattern padding at offset ${run_pos:04X}; data starts at ${candidate_start:04X}")
            break
        search_pos = run_pos + 1

    if data_start is None:
        raise ValueError("Could not locate bank 2 common pattern data from the post-data FF padding run")

    blocks = {}
    offset = data_start
    for name, size in COMMON_BLOCKS.items():
        rom_offset = bank2_offset + offset
        blocks[name] = prg_data[rom_offset:rom_offset + size]
        print(f"    {name}: ROM ${rom_offset:05X}, {size} bytes, {size // 16} tiles")
        offset += size

    return blocks


def nes_tile_to_genesis(nes_tile_data: bytes) -> bytes:
    """Convert a single NES 2bpp tile (16 bytes) to Genesis 4bpp tile (32 bytes).

    NES 2bpp format:
      Bytes 0-7:  bit plane 0 (MSB = leftmost pixel)
      Bytes 8-15: bit plane 1

    Genesis 4bpp format:
      4 bytes per row, 8 rows = 32 bytes
      Each byte = 2 pixels: high nibble = left pixel, low nibble = right pixel
    """
    genesis_tile = bytearray(32)

    for row in range(8):
        plane0 = nes_tile_data[row]
        plane1 = nes_tile_data[row + 8]

        # Convert 8 pixels for this row
        for px in range(8):
            bit_pos = 7 - px  # MSB = leftmost pixel
            bit0 = (plane0 >> bit_pos) & 1
            bit1 = (plane1 >> bit_pos) & 1
            pixel_val = (bit1 << 1) | bit0  # 2-bit color value (0-3)

            # Pack into Genesis format: 2 pixels per byte, high nibble first
            byte_idx = row * 4 + (px // 2)
            if px % 2 == 0:
                genesis_tile[byte_idx] |= (pixel_val << 4)
            else:
                genesis_tile[byte_idx] |= pixel_val

    return bytes(genesis_tile)


def convert_pattern_block(nes_data: bytes) -> bytes:
    """Convert an entire pattern block from NES 2bpp to Genesis 4bpp."""
    num_tiles = len(nes_data) // 16
    genesis_data = bytearray()
    for i in range(num_tiles):
        nes_tile = nes_data[i * 16:(i + 1) * 16]
        genesis_data.extend(nes_tile_to_genesis(nes_tile))
    return bytes(genesis_data)


def genesis_data_to_c_array(genesis_data: bytes, var_name: str) -> str:
    """Convert Genesis tile data to a C const-array declaration.

    Emits:
        const unsigned long <var_name>_size = <N>;
        const unsigned char <var_name>[<N>] = { ... };
    """
    n = len(genesis_data)
    lines = [
        f"/* Auto-generated by tools/extract_chr.py - do not edit. */",
        f"const unsigned long {var_name}_size = {n}UL;",
        f"const unsigned char {var_name}[{n}] = {{",
    ]

    # 16 bytes per output row, matching intro CHR style
    for i in range(0, n, 16):
        chunk = genesis_data[i:i + 16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        comma = "" if (i + 16) >= n else ","
        lines.append(f"    {hex_vals}{comma}")

    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def write_c_file(path: str, genesis_data: bytes, var_name: str) -> None:
    """Write a single Genesis CHR block as a C source file."""
    content = genesis_data_to_c_array(genesis_data, var_name)
    with open(path, 'w', encoding='ascii', newline='\n') as f:
        f.write(content)
    num_tiles = len(genesis_data) // 32
    print(f"  Wrote {path} ({len(genesis_data)} bytes, {num_tiles} tiles)")


# ---------------------------------------------------------------------------
# Legacy .inc emission (--legacy-inc flag only)
# ---------------------------------------------------------------------------

def genesis_data_to_inc(genesis_data: bytes, label: str) -> str:
    """Convert Genesis tile data to a vasm-compatible .inc file with dc.w statements."""
    lines = [f"; {label} - {len(genesis_data)} bytes, {len(genesis_data) // 32} tiles"]
    lines.append(f"{label}:")

    for tile_idx in range(len(genesis_data) // 32):
        tile_start = tile_idx * 32
        lines.append(f"; tile {tile_idx}")
        for row in range(8):
            row_start = tile_start + row * 4
            word1 = (genesis_data[row_start] << 8) | genesis_data[row_start + 1]
            word2 = (genesis_data[row_start + 2] << 8) | genesis_data[row_start + 3]
            lines.append(f"    dc.w ${word1:04X},${word2:04X}")

    return '\n'.join(lines)


def write_inc_file(path: str, blocks_data: list, header_comment: str) -> None:
    """Write a .inc file containing multiple converted pattern blocks."""
    lines = [
        f"; {header_comment}",
        f"; Auto-generated by extract_chr.py - DO NOT EDIT",
        f"; NES 2bpp tiles converted to Genesis 4bpp format",
        "",
    ]

    for label, genesis_data in blocks_data:
        lines.append(genesis_data_to_inc(genesis_data, label))
        lines.append("")

    with open(path, 'w') as f:
        f.write('\n'.join(lines))

    total_bytes = sum(len(d) for _, d in blocks_data)
    total_tiles = total_bytes // 32
    print(f"  Wrote {path} ({total_bytes} bytes, {total_tiles} tiles)")


# ---------------------------------------------------------------------------
# MANIFEST.json builder
# ---------------------------------------------------------------------------

def build_manifest(blocks_info: list) -> dict:
    """Build the MANIFEST.json structure from the ordered list of emitted blocks.

    blocks_info: list of dicts with keys:
        name            -- block name (e.g. "overworld_bg")
        file            -- output filename (e.g. "overworld_bg.c")
        nes_tile_id_start -- first NES tile id in this block (0..N)
        tile_count      -- number of tiles
        genesis_vram_tile_start -- first Genesis VRAM tile index
        byte_size       -- Genesis byte count (tile_count * 32)
    """
    manifest = {
        "schema_version": 1,
        "nes_rom_sha256": NES_ROM_SHA256,
        "blocks": {},
        "tile_index_map": {},
    }

    for info in blocks_info:
        name = info["name"]
        manifest["blocks"][name] = {
            "file": info["file"],
            "nes_tile_id_start": info["nes_tile_id_start"],
            "tile_count": info["tile_count"],
            "genesis_vram_tile_start": info["genesis_vram_tile_start"],
            "byte_size": info["byte_size"],
        }
        # Populate tile_index_map for each tile in this block
        for i in range(info["tile_count"]):
            gen_tile_idx = info["genesis_vram_tile_start"] + i
            nes_tile_id = info["nes_tile_id_start"] + i
            manifest["tile_index_map"][str(gen_tile_idx)] = {
                "block": name,
                "nes_tile_id": nes_tile_id,
            }

    return manifest


def write_manifest(path: str, manifest: dict) -> None:
    """Write MANIFEST.json with stable, deterministic formatting."""
    with open(path, 'w', encoding='ascii', newline='\n') as f:
        json.dump(manifest, f, indent=2, sort_keys=False, ensure_ascii=True)
        f.write('\n')
    tile_count = len(manifest["tile_index_map"])
    print(f"  Wrote {path} ({tile_count} tile_index_map entries)")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract NES Zelda CHR data and emit Genesis-ready C arrays."
    )
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Output directory for data/chr/ files. Default: <repo>/data/chr/",
    )
    parser.add_argument(
        "--legacy-inc",
        action="store_true",
        default=False,
        help="Also emit legacy .inc files (vasm format) alongside C arrays.",
    )
    args = parser.parse_args()

    # Resolve project root and paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    if args.out_dir is not None:
        out_dir = os.path.abspath(args.out_dir)
    else:
        out_dir = os.path.join(project_root, "data", "chr")

    # Legacy .inc output goes to the old location regardless of --out-dir
    legacy_dir = os.path.join(project_root, "src", "data")

    # Locate NES ROM (try ZELDA_NES_ROM env var first, then fallback to repo root)
    rom_path_env = os.environ.get("ZELDA_NES_ROM", "")
    if rom_path_env and os.path.isfile(rom_path_env):
        rom_path = rom_path_env
    else:
        rom_path = os.path.join(project_root, "Legend of Zelda, The (USA).nes")

    if not os.path.exists(rom_path):
        print(f"ERROR: NES ROM not found: {rom_path}")
        print(f"  Set ZELDA_NES_ROM env var or place the ROM at the repo root.")
        sys.exit(1)

    # Verify ROM SHA256
    print(f"Reading NES ROM: {rom_path}")
    h = hashlib.sha256()
    with open(rom_path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    actual_sha256 = h.hexdigest()
    if actual_sha256.lower() != NES_ROM_SHA256.lower():
        print(f"WARNING: ROM SHA256 mismatch!")
        print(f"  Expected: {NES_ROM_SHA256}")
        print(f"  Actual:   {actual_sha256}")
        # Continue anyway -- wrong ROM produces wrong tiles but is not fatal
        # for tool development. Production CI should treat this as fatal.

    prg_data, prg_banks = read_ines_rom(rom_path)

    os.makedirs(out_dir, exist_ok=True)
    if args.legacy_inc:
        os.makedirs(legacy_dir, exist_ok=True)

    # === Extract bank 3 pattern blocks (level-specific tiles) ===
    print("\nExtracting bank 3 pattern blocks (level tiles)...")
    bank3_blocks = find_bank3_pattern_data(prg_data)

    owbg_data = convert_pattern_block(bank3_blocks['PatternBlockOWBG'])
    uwbg_data = convert_pattern_block(bank3_blocks['PatternBlockUWBG'])

    # Overworld BG
    write_c_file(
        os.path.join(out_dir, "overworld_bg.c"),
        owbg_data,
        "overworld_bg_chr",
    )

    # Underworld BG
    write_c_file(
        os.path.join(out_dir, "underworld_bg.c"),
        uwbg_data,
        "underworld_bg_chr",
    )

    # Sprites block: overworld sprites + all UW sprite variants
    owsp_data   = convert_pattern_block(bank3_blocks['PatternBlockOWSP'])
    uwsp358_data = convert_pattern_block(bank3_blocks['PatternBlockUWSP358'])
    uwsp469_data = convert_pattern_block(bank3_blocks['PatternBlockUWSP469'])
    uwsp_data   = convert_pattern_block(bank3_blocks['PatternBlockUWSP'])
    uwsp127_data = convert_pattern_block(bank3_blocks['PatternBlockUWSP127'])

    sprites_data = (owsp_data + uwsp358_data + uwsp469_data +
                    uwsp_data + uwsp127_data)
    write_c_file(
        os.path.join(out_dir, "sprites.c"),
        sprites_data,
        "sprites_chr",
    )

    # Bosses block
    boss1257_data = convert_pattern_block(bank3_blocks['PatternBlockUWSPBoss1257'])
    boss3468_data = convert_pattern_block(bank3_blocks['PatternBlockUWSPBoss3468'])
    boss9_data    = convert_pattern_block(bank3_blocks['PatternBlockUWSPBoss9'])
    bosses_data = boss1257_data + boss3468_data + boss9_data
    write_c_file(
        os.path.join(out_dir, "bosses.c"),
        bosses_data,
        "bosses_chr",
    )

    # === Extract bank 2 pattern blocks (common tiles) ===
    print("\nExtracting bank 2 pattern blocks (common tiles)...")
    common_blocks = find_common_pattern_data(prg_data)

    common_sp_data   = convert_pattern_block(common_blocks['CommonSpritePatterns'])
    common_bg_data   = convert_pattern_block(common_blocks['CommonBackgroundPatterns'])
    common_misc_data = convert_pattern_block(common_blocks['CommonMiscPatterns'])
    common_data = common_sp_data + common_bg_data + common_misc_data
    write_c_file(
        os.path.join(out_dir, "common.c"),
        common_data,
        "common_chr",
    )

    # === Extract bank 1 pattern blocks (demo/title tiles) ===
    print("\nExtracting bank 1 pattern blocks (demo/title tiles)...")
    demo_blocks = find_demo_pattern_data(prg_data)

    demo_sp_data = convert_pattern_block(demo_blocks['DemoSpritePatterns'])
    demo_bg_data = convert_pattern_block(demo_blocks['DemoBackgroundPatterns'])
    demo_data = demo_sp_data + demo_bg_data
    write_c_file(
        os.path.join(out_dir, "demo.c"),
        demo_data,
        "demo_chr",
    )

    # === Build MANIFEST.json ===
    # Assign Genesis VRAM tile indices.  The sprite / bosses / common / demo
    # blocks start at VRAM_TILE_SPRITES_START and are laid out contiguously.
    owbg_tiles   = len(owbg_data)   // 32
    uwbg_tiles   = len(uwbg_data)   // 32
    sprites_tiles = len(sprites_data) // 32
    bosses_tiles  = len(bosses_data)  // 32
    common_tiles  = len(common_data)  // 32
    demo_tiles    = len(demo_data)    // 32

    vram_sprites_start = VRAM_TILE_SPRITES_START
    vram_bosses_start  = vram_sprites_start + sprites_tiles
    vram_common_start  = vram_bosses_start  + bosses_tiles
    vram_demo_start    = vram_common_start  + common_tiles

    blocks_info = [
        {
            "name": "overworld_bg",
            "file": "overworld_bg.c",
            "nes_tile_id_start": 0,
            "tile_count": owbg_tiles,
            "genesis_vram_tile_start": VRAM_TILE_OVERWORLD_BG,
            "byte_size": len(owbg_data),
        },
        {
            "name": "underworld_bg",
            "file": "underworld_bg.c",
            "nes_tile_id_start": 0,
            "tile_count": uwbg_tiles,
            "genesis_vram_tile_start": VRAM_TILE_UNDERWORLD_BG,
            "byte_size": len(uwbg_data),
        },
        {
            "name": "sprites",
            "file": "sprites.c",
            "nes_tile_id_start": 0,
            "tile_count": sprites_tiles,
            "genesis_vram_tile_start": vram_sprites_start,
            "byte_size": len(sprites_data),
        },
        {
            "name": "bosses",
            "file": "bosses.c",
            "nes_tile_id_start": 0,
            "tile_count": bosses_tiles,
            "genesis_vram_tile_start": vram_bosses_start,
            "byte_size": len(bosses_data),
        },
        {
            "name": "common",
            "file": "common.c",
            "nes_tile_id_start": 0,
            "tile_count": common_tiles,
            "genesis_vram_tile_start": vram_common_start,
            "byte_size": len(common_data),
        },
        {
            "name": "demo",
            "file": "demo.c",
            "nes_tile_id_start": 0,
            "tile_count": demo_tiles,
            "genesis_vram_tile_start": vram_demo_start,
            "byte_size": len(demo_data),
        },
    ]

    manifest = build_manifest(blocks_info)
    write_manifest(os.path.join(out_dir, "MANIFEST.json"), manifest)

    # === Legacy .inc emission ===
    if args.legacy_inc:
        print("\nEmitting legacy .inc files...")

        ow_blocks_inc = [
            ("TilesOverworldBG", owbg_data),
            ("TilesOverworldSP", owsp_data),
        ]
        write_inc_file(
            os.path.join(legacy_dir, "tiles_overworld.inc"),
            ow_blocks_inc,
            "Overworld tile data (background + sprites)"
        )
        write_inc_file(
            os.path.join(legacy_dir, "tiles_overworld_bg.inc"),
            [("TilesOverworldBG", owbg_data)],
            "Overworld background tile data"
        )

        uw_blocks_inc = [
            ("TilesUnderworldBG", uwbg_data),
            ("TilesUnderworldSP", uwsp_data),
            ("TilesUnderworldSP127", uwsp127_data),
            ("TilesUnderworldSP358", uwsp358_data),
            ("TilesUnderworldSP469", uwsp469_data),
        ]
        write_inc_file(
            os.path.join(legacy_dir, "tiles_underworld.inc"),
            uw_blocks_inc,
            "Underworld tile data (background + sprite variants)"
        )
        write_inc_file(
            os.path.join(legacy_dir, "tiles_underworld_bg.inc"),
            [("TilesUnderworldBG", uwbg_data)],
            "Underworld background tile data"
        )

        boss_blocks_inc = [
            ("TilesBoss1257", boss1257_data),
            ("TilesBoss3468", boss3468_data),
            ("TilesBoss9", boss9_data),
        ]
        write_inc_file(
            os.path.join(legacy_dir, "tiles_bosses.inc"),
            boss_blocks_inc,
            "Boss sprite tile data"
        )

        common_converted_inc = [
            ("TilesCommonSprites", common_sp_data),
            ("TilesCommonBG", common_bg_data),
            ("TilesCommonMisc", common_misc_data),
        ]
        write_inc_file(
            os.path.join(legacy_dir, "tiles_common.inc"),
            common_converted_inc,
            "Common tile data (always loaded)"
        )

        demo_converted_inc = [
            ("TilesDemoSprites", demo_sp_data),
            ("TilesDemoBG", demo_bg_data),
        ]
        write_inc_file(
            os.path.join(legacy_dir, "tiles_demo.inc"),
            demo_converted_inc,
            "Demo/title screen tile data"
        )

        sprite_bundle = [
            ("TilesCommonSprites", common_sp_data),
            ("TilesCommonBG", common_bg_data),
            ("TilesCommonMisc", common_misc_data),
            ("TilesOverworldSP", owsp_data),
            ("TilesUnderworldSP", uwsp_data),
            ("TilesUnderworldSP127", uwsp127_data),
            ("TilesUnderworldSP358", uwsp358_data),
            ("TilesUnderworldSP469", uwsp469_data),
            ("TilesBoss1257", boss1257_data),
            ("TilesBoss3468", boss3468_data),
            ("TilesBoss9", boss9_data),
            ("TilesDemoSprites", demo_sp_data),
            ("TilesDemoBG", demo_bg_data),
        ]
        write_inc_file(
            os.path.join(legacy_dir, "tiles_sprites.inc"),
            sprite_bundle,
            "Sprite, UI, common, and title tile data"
        )

    # === Summary ===
    all_nes_bytes = sum(len(b) for b in bank3_blocks.values())
    all_nes_bytes += sum(len(b) for b in common_blocks.values())
    all_nes_bytes += sum(len(b) for b in demo_blocks.values())
    total_tiles = all_nes_bytes // 16

    total_gen_bytes = (len(owbg_data) + len(uwbg_data) + len(sprites_data) +
                       len(bosses_data) + len(common_data) + len(demo_data))
    total_manifest_entries = len(manifest["tile_index_map"])

    print(f"\n=== Extraction complete ===")
    print(f"  Total NES tile data: {all_nes_bytes} bytes ({total_tiles} tiles)")
    print(f"  Total Genesis CHR output: {total_gen_bytes} bytes across 6 blocks")
    print(f"  MANIFEST tile_index_map: {total_manifest_entries} entries")
    print(f"  Output directory: {out_dir}")
    print(f"\nBlock inventory:")
    for info in blocks_info:
        print(f"  {info['name']:20s}  {info['tile_count']:4d} tiles  {info['byte_size']:6d} bytes  "
              f"VRAM tile {info['genesis_vram_tile_start']}")


if __name__ == '__main__':
    main()
