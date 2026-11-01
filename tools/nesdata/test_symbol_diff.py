"""Tests for tools/nesdata/symbol_diff.py (T-419 Task 2)."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.nesdata.symbol_diff import compare, locate_added, parse_nm, select  # noqa: E402

NM = """\
00001000 00000004 R k_table.lto_priv.0
00002000 00000002 r s_small
00003000 00000010 T main
e0ff0008 00000008 B ram_thing
e0ff0000 00000008 D init_data
"""


def test_parse_nm_keeps_rom_symbols_strips_lto_suffix():
    # md.ld places .rodata inside .text, so LTO data tables show as t/T too.
    syms = parse_nm(NM, data_lma=0x5000)
    assert syms == {"k_table": (0x1000, 4), "s_small": (0x2000, 2), "main": (0x3000, 0x10),
                    "init_data": (0x5000, 8)}


def test_select_reports_missing_piece_symbol():
    a = {"k_gray": b"ab", "code": b"1"}
    b = {"k_gray": b"ab", "code": b"2"}
    assert select(a, ["k_gray", "k_absent"]) == {"k_gray": b"ab"}
    assert compare(select(a, ["k_gray"]), select(b, ["k_gray"])) == ([], [], [])


def test_locate_added_in_old_image():
    assert locate_added(["a", "b"], {"a": b"xyz", "b": b"qq"}, b"..xyz..") == {"a": True, "b": False}


def test_compare_counts():
    a = {"same": b"ab", "changed": b"xy", "gone": b"q"}
    b = {"same": b"ab", "changed": b"xz", "new": b"r"}
    assert compare(a, b) == (["changed"], ["gone"], ["new"])
