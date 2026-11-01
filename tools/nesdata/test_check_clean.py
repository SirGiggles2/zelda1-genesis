"""Tests for tools/nesdata/check_clean.py (T-419 Task 8)."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.nesdata.check_clean import (merge_hits, outside_hits, owner, range_from_nm,  # noqa: E402
                                       residue_tables, sized_symbols)

NES_WINDOW = bytes(range(1, 17))  # 16 distinct bytes: a real-looking ROM window


def test_window_outside_range_is_a_hit():
    rom = b"\0" * 32 + NES_WINDOW + b"\0" * 32
    assert outside_hits(rom, 0, 16, [b"\xff" * 8 + NES_WINDOW]) == [(32, 16)]


def test_window_inside_range_is_ignored():
    rom = b"\0" * 32 + NES_WINDOW + b"\0" * 32
    assert outside_hits(rom, 30, 20, [NES_WINDOW]) == []


def test_low_variety_runs_ignored():
    rom = b"\0" * 64
    assert outside_hits(rom, 0, 0, [b"\0" * 64]) == []


def test_merge_overlapping_hits():
    assert merge_hits([(10, 16), (11, 16), (40, 16)]) == [(10, 17), (40, 16)]


def test_residue_tables_counts_numeric_initializers():
    text = ("const unsigned long n_size = 4UL;\n"
            "extern const unsigned char a[4];\n"
            "const S big[2] = { {1,2,3,4,5,6,7,8}, {9,10,11,12,13,14,15,16} };\n"
            "const void *ptrs[2] = { a, a };\n")
    assert residue_tables(text) == [("big", 16)]


def test_owner_of_hit():
    syms = sized_symbols("00000100 00000010 t k_table\n00000200 00000004 T main\n")
    assert owner(0x105, syms) == "k_table"
    assert owner(0x150, syms) is None


def test_unsized_labels_and_data_load_image():
    nm = ("00000100 t NoteTable\n00000180 t Next\n00000200 00000004 T _stext\n"
          "e0ff0010 00000008 d init_tab\n")
    syms = sized_symbols(nm, data_lma=0x1000)
    assert owner(0x150, syms) == "NoteTable"
    assert owner(0x1014, syms) == "init_tab"


def test_range_from_nm():
    nm = "0035b828 T __nesdata_start\n0035bc28 T __nesdata_end\n00001000 T main\n"
    assert range_from_nm(nm) == (0x35B828, 0x400)
