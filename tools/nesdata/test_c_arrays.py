"""Tests for tools/nesdata/c_arrays.py: generated C arrays -> exact ROM bytes + residue."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.nesdata.c_arrays import parse, residue, structs_in  # noqa: E402


def test_u8_array_bytes_and_size_scalar_kept():
    src = ("const unsigned long common_chr_size = 4UL;\n"
           "const unsigned char common_chr[4] = {\n  0x01, 0xFF, 2, 'A'\n};\n")
    p = parse(src, {})
    (a,) = p.arrays
    # No explicit attribute: 2, the m68k ABI's largest natural alignment (DMA-safe for bytes).
    assert (a.name, a.data, a.align, a.static) == ("common_chr", b"\x01\xff\x02A", 2, False)
    out = residue(src, p)
    assert "const unsigned long common_chr_size = 4UL;" in out
    assert "extern const unsigned char common_chr[4];" in out
    assert "0xFF" not in out


def test_u16_big_endian_2d_and_attribute_before_name():
    src = ("const unsigned short __attribute__((aligned(4))) lut[2][2] = {\n"
           "  {0x1234u, 1}, {0xABCD, 0xFFFF}\n};\n")
    (a,) = parse(src, {}).arrays
    assert a.data == bytes.fromhex("1234" "0001" "abcd" "ffff")
    assert a.align == 4


def test_attribute_after_dims_macro_dim_and_zero_fill():
    src = "const u8 sfx[SFX_LEN] __attribute__((aligned(256))) = { 7, 8 };\n"
    (a,) = parse(src, {"SFX_LEN": 4}).arrays
    assert (a.data, a.align) == (b"\x07\x08\x00\x00", 256)


def test_static_array_becomes_global_extern_and_negative_values():
    src = "static const signed char d[3] = { -1, -128, 5 };\nconst void *t[1] = { d };\n"
    p = parse(src, {})
    (a,) = p.arrays
    assert a.static and a.data == b"\xff\x80\x05"
    out = residue(src, p)
    assert "extern const signed char d[3];" in out and "static" not in out
    assert "const void *t[1] = { d };" in out


def test_unsized_first_dim_from_initializer():
    src = "const unsigned char x[][2] = { {1,2}, {3,4}, {5,6} };\n"
    (a,) = parse(src, {}).arrays
    assert a.data == bytes([1, 2, 3, 4, 5, 6])
    assert "extern const unsigned char x[3][2];" in residue(src, parse(src, {}))


def test_designated_row_initializers():
    src = ("const unsigned char t[3][2] = {\n"
           "    [0x02] = { 5, 6 },\n    [0] = { 1, 2 },\n};\n")
    (a,) = parse(src, {}).arrays
    assert a.data == bytes([1, 2, 0, 0, 5, 6])


def test_struct_fields_parsed_from_header_text():
    hdr = ("struct row { unsigned char id; unsigned char door[4]; unsigned char r; };\n"
           "struct pad { unsigned char a; unsigned short w; unsigned char b; };\n")
    assert structs_in(hdr) == {"row": [(1, 1), (1, 4), (1, 1)], "pad": [(1, 1), (2, 1), (1, 1)]}


def test_struct_array_bytes_byte_fields():
    structs = {"row": [(1, 1), (1, 4), (1, 1)]}
    src = "const struct row rows[] = {\n  { 0x22u, { 5u, 1u, 1u, 1u }, 1u },\n  { 0x23u, {1,5,5,1}, 0 },\n};\n"
    (a,) = parse(src, {}, structs).arrays
    assert a.data == bytes([0x22, 5, 1, 1, 1, 1, 0x23, 1, 5, 5, 1, 0]) and a.dims == (2,)
    assert "extern const struct row rows[2];" in residue(src, parse(src, {}, structs))


def test_struct_padding_matches_m68k_gcc():
    # Measured with sgdk/bin/gcc -m68000: {u8; u16; u8} -> size 6, u16 at offset 2.
    structs = {"pad": [(1, 1), (2, 1), (1, 1)]}
    (a,) = parse("const struct pad p[1] = { { 1, 0x0203, 4 } };\n", {}, structs).arrays
    assert a.data == bytes([1, 0, 2, 3, 4, 0])


def test_comments_ignored_and_u32():
    src = "/* a { b } */\nconst unsigned long w[1] = { 0x01020304UL }; // tail\n"
    (a,) = parse(src, {}).arrays
    assert a.data == b"\x01\x02\x03\x04" and a.align == 2
