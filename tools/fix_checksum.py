#!/usr/bin/env python3
"""
fix_checksum.py - Pad and fix checksum in a Genesis ROM header.

Pads to next power-of-two (min 128KB / $20000) for flash-cart compatibility,
writes the ROM-end header field at $01A4, and recomputes the checksum at $018E.

Usage: python fix_checksum.py input.md output.md
"""

import sys


def next_power_of_two(n, minimum=0x20000):
    """Return the smallest power of two >= max(n, minimum)."""
    n = max(n, minimum)
    p = 1
    while p < n:
        p <<= 1
    return p


def fix_checksum(input_path, output_path):
    with open(input_path, "rb") as f:
        data = bytearray(f.read())

    # Pad to next power-of-two, minimum 128KB
    target_size = next_power_of_two(len(data))
    data.extend(b'\x00' * (target_size - len(data)))

    # Write ROM-end address at $01A4 (4 bytes, big-endian)
    rom_end = len(data) - 1
    data[0x01A4] = (rom_end >> 24) & 0xFF
    data[0x01A5] = (rom_end >> 16) & 0xFF
    data[0x01A6] = (rom_end >> 8) & 0xFF
    data[0x01A7] = rom_end & 0xFF

    # Recompute checksum from $0200 onward
    checksum = 0
    for i in range(0x200, len(data), 2):
        word = (data[i] << 8) | data[i + 1]
        checksum = (checksum + word) & 0xFFFF

    data[0x018E] = (checksum >> 8) & 0xFF
    data[0x018F] = checksum & 0xFF

    with open(output_path, "wb") as f:
        f.write(data)

    print(f"  Checksum:  ${checksum:04X}")
    print(f"  ROM size:  {len(data)} bytes ({len(data) // 1024} KB)")
    print(f"  ROM end:   ${rom_end:08X}")
    print(f"  Written:   {output_path}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python fix_checksum.py input.md output.md")
        sys.exit(1)
    fix_checksum(sys.argv[1], sys.argv[2])
