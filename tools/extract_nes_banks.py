#!/usr/bin/env python3
"""
Extract NES PRG banks 00..06 (7 x 16KB) from Zelda 1 iNES ROM.
"""

import argparse
import os
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# ZELDA_NES_ROM first, then repo-root fallback — same convention as every
# other extractor. The previous default was an absolute path to one
# developer's machine, which cannot work in a shipped builder.
DEFAULT_ROM = os.environ.get("ZELDA_NES_ROM") or os.path.join(
    ROOT, "Legend of Zelda, The (USA).nes"
)
DEFAULT_OUT_DIR = os.path.join(ROOT, "reference", "aldonunez", "dat")

INES_HEADER_SIZE = 16
PRG_BANK_SIZE = 0x4000
EXPECTED_PRG_BANKS = 8
OUT_BANK_COUNT = 7


def fail(msg):
    print(f"ERROR: {msg}")
    sys.exit(1)


def parse_args():
    p = argparse.ArgumentParser(description="Extract NES PRG banks 00..06 from Zelda ROM")
    p.add_argument("--rom", default=DEFAULT_ROM, help="Path to .nes ROM")
    p.add_argument("--out-dir", default=DEFAULT_OUT_DIR, help="Output directory for nes_bank_XX.bin")
    return p.parse_args()


def read_rom(path):
    if not os.path.exists(path):
        fail(f"ROM not found: {path}")
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < INES_HEADER_SIZE:
        fail(f"ROM too small: {len(data)} bytes")
    if data[0:4] != b"NES\x1A":
        fail("Invalid iNES header signature (expected NES<EOF>)")
    return data


def validate_rom_layout(data):
    prg_banks = data[4]
    chr_banks = data[5]
    flags6 = data[6]
    has_trainer = bool(flags6 & 0x04)
    trainer_size = 512 if has_trainer else 0

    if prg_banks != EXPECTED_PRG_BANKS:
        fail(f"Unexpected PRG bank count: {prg_banks} (expected {EXPECTED_PRG_BANKS})")

    expected_size = INES_HEADER_SIZE + trainer_size + prg_banks * PRG_BANK_SIZE + chr_banks * 0x2000
    if len(data) != expected_size:
        fail(
            f"ROM size mismatch: got {len(data)} bytes, expected {expected_size} bytes "
            f"(header+trainer+PRG+CHR)"
        )
    return INES_HEADER_SIZE + trainer_size


def write_banks(data, prg_start, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    outputs = []
    for bank in range(OUT_BANK_COUNT):
        off = prg_start + bank * PRG_BANK_SIZE
        blob = data[off:off + PRG_BANK_SIZE]
        if len(blob) != PRG_BANK_SIZE:
            fail(f"Bank {bank:02d} extraction length is {len(blob)} (expected {PRG_BANK_SIZE})")
        out_path = os.path.join(out_dir, f"nes_bank_{bank:02d}.bin")
        with open(out_path, "wb") as f:
            f.write(blob)
        actual = os.path.getsize(out_path)
        if actual != PRG_BANK_SIZE:
            fail(f"Wrote invalid bank size for {out_path}: {actual} bytes")
        outputs.append(out_path)
    return outputs


def main():
    args = parse_args()
    rom_data = read_rom(args.rom)
    prg_start = validate_rom_layout(rom_data)
    outputs = write_banks(rom_data, prg_start, args.out_dir)
    print(f"Extracted {len(outputs)} banks to {args.out_dir}")
    for path in outputs:
        print(f"  {os.path.basename(path)} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
