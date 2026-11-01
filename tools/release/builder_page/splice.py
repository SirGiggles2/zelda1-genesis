"""Finish the player's ROM: insert .nesdata into the blanked release image, fix the
Genesis checksum (tools/fix_checksum.py rule: big-endian word sum from $200,
stored at $18E; size/ROM-end are already in the base image) and require the
release SHA-256. Runs in the builder page (Pyodide) and natively (tests).
T-419 Task 13.
"""
from __future__ import annotations

import array
import hashlib
import sys


class MismatchError(ValueError):
    def __init__(self, got: str, want: str):
        super().__init__(f"built ROM sha256 {got} != release {want}")
        self.got, self.want = got, want


def finish(base: bytes, nesdata: bytes, recipe: dict) -> bytes:
    start, size = recipe["nesdata"]["offset"], recipe["nesdata"]["size"]
    if len(nesdata) != size or len(base) != recipe["rom_size"]:
        raise MismatchError(f"size {len(nesdata)}/{len(base)}", f"size {size}/{recipe['rom_size']}")
    rom = bytearray(base)
    rom[start:start + size] = nesdata
    words = array.array("H", bytes(rom[0x200:]))
    if sys.byteorder == "little":
        words.byteswap()  # the Genesis sums big-endian words
    total = sum(words) & 0xFFFF
    rom[0x18E] = total >> 8
    rom[0x18F] = total & 0xFF
    got = hashlib.sha256(rom).hexdigest()
    if got != recipe["rom_sha256"]:
        raise MismatchError(got, recipe["rom_sha256"])
    return bytes(rom)
