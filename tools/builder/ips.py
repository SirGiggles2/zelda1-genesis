"""IPS and BPS patchers (Zelda Redux ships a BPS main patch, IPS add-ons).

Used by build.py to produce Zelda Redux from the user's original ROM and the
Redux patch the user downloaded (never bundled).
"""
from __future__ import annotations


def apply_ips(rom: bytes, patch: bytes) -> bytes:
    if patch[:5] != b"PATCH":
        raise ValueError("not an IPS patch")
    out = bytearray(rom)
    i = 5
    while True:
        if i + 3 > len(patch):
            raise ValueError("IPS patch is truncated (no EOF marker)")
        if patch[i:i + 3] == b"EOF":
            i += 3
            if len(patch) - i == 3:                       # truncate extension
                del out[int.from_bytes(patch[i:i + 3], "big"):]
            return bytes(out)
        if i + 5 > len(patch):
            raise ValueError("IPS patch is truncated (record header)")
        off = int.from_bytes(patch[i:i + 3], "big")
        size = int.from_bytes(patch[i + 3:i + 5], "big")
        i += 5
        if size:
            if i + size > len(patch):
                raise ValueError("IPS patch is truncated (record data)")
            data = patch[i:i + size]
            i += size
        else:                                             # RLE record
            if i + 3 > len(patch):
                raise ValueError("IPS patch is truncated (RLE record)")
            run = int.from_bytes(patch[i:i + 2], "big")
            data = patch[i + 2:i + 3] * run
            i += 3
        if off + len(data) > len(out):
            out.extend(bytes(off + len(data) - len(out)))
        out[off:off + len(data)] = data


def _varint(data: bytes, i: int) -> tuple[int, int]:
    value, shift = 0, 1
    while True:
        b = data[i]
        i += 1
        value += (b & 0x7F) * shift
        if b & 0x80:
            return value, i
        shift <<= 7
        value += shift


def apply_bps(rom: bytes, patch: bytes) -> bytes:
    """BPS (beat) patch; source/target/patch CRC32s are checked."""
    import zlib  # noqa: PLC0415
    if patch[:4] != b"BPS1":
        raise ValueError("not a BPS patch")
    if zlib.crc32(patch[:-4]) != int.from_bytes(patch[-4:], "little"):
        raise ValueError("BPS patch checksum mismatch")
    src_crc = int.from_bytes(patch[-12:-8], "little")
    dst_crc = int.from_bytes(patch[-8:-4], "little")
    if zlib.crc32(rom) != src_crc:
        raise ValueError("BPS patch does not apply to this ROM (source CRC32)")
    i = 4
    src_size, i = _varint(patch, i)
    dst_size, i = _varint(patch, i)
    meta_size, i = _varint(patch, i)
    i += meta_size
    out = bytearray()
    src_rel = dst_rel = 0
    end = len(patch) - 12
    while i < end:
        data, i = _varint(patch, i)
        cmd, length = data & 3, (data >> 2) + 1
        if cmd == 0:                                  # SourceRead
            out += rom[len(out):len(out) + length]
        elif cmd == 1:                                # TargetRead
            out += patch[i:i + length]
            i += length
        else:
            off, i = _varint(patch, i)
            off = -(off >> 1) if off & 1 else off >> 1
            if cmd == 2:                              # SourceCopy
                src_rel += off
                out += rom[src_rel:src_rel + length]
                src_rel += length
            else:                                     # TargetCopy (may overlap)
                dst_rel += off
                for _ in range(length):
                    out.append(out[dst_rel])
                    dst_rel += 1
    if len(out) != dst_size or zlib.crc32(out) != dst_crc:
        raise ValueError("BPS output size/CRC32 mismatch")
    return bytes(out)


def apply_patch(rom: bytes, patch: bytes) -> bytes:
    if patch[:4] == b"BPS1":
        return apply_bps(rom, patch)
    return apply_ips(rom, patch)
