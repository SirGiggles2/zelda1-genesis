"""Tests for tools/nesdata/sources.py: generated C -> pieces + build-tree residue."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.nesdata.sources import RESIDUE_DIR, convert_file, register, residue_path  # noqa: E402

FULL = ('#include "t.h"\n#include <types.h>\n'
        "const unsigned long t_size = 2UL;\nconst unsigned char t_chr[2] = { 0xAA, 0xBB };\n")


def _source(tmp_path: Path) -> Path:
    src = tmp_path / "data" / "t.c"
    src.parent.mkdir(parents=True, exist_ok=True)
    src.write_text(FULL, encoding="utf-8")
    (src.parent / "t.h").write_text("/* decls */\n", encoding="utf-8")
    return src


def test_convert_writes_piece_and_build_residue_leaves_source(tmp_path):
    src = _source(tmp_path)
    arrays = convert_file(src, tmp_path)
    assert [a.name for a in arrays] == ["t_chr"]
    assert (tmp_path / "build/nesdata/pieces/t_chr.bin").read_bytes() == b"\xaa\xbb"
    assert src.read_text(encoding="utf-8") == FULL
    res = residue_path(src, tmp_path)
    assert res == tmp_path / RESIDUE_DIR / "data" / "t.c"
    text = res.read_text(encoding="utf-8")
    assert "extern const unsigned char t_chr[2];" in text and "0xAA" not in text
    assert f'#include "{(src.parent / "t.h").resolve().as_posix()}"' in text
    assert "#include <types.h>" in text


def test_unchanged_outputs_keep_mtime(tmp_path):
    import os
    src = _source(tmp_path)
    convert_file(src, tmp_path)
    outs = [residue_path(src, tmp_path), tmp_path / "build/nesdata/pieces/t_chr.bin"]
    for o in outs:
        os.utime(o, (1000.0, 1000.0))
    convert_file(src, tmp_path)
    assert [o.stat().st_mtime for o in outs] == [1000.0, 1000.0]


def test_register_entries_and_duplicate_symbol(tmp_path):
    a = _source(tmp_path)
    entries = register([a], tmp_path)
    assert entries == [{"name": "t_chr", "symbol": "t_chr", "source": "data/t.c", "size": 2, "align": 2}]
    b = tmp_path / "data" / "b.c"
    b.write_text(FULL, encoding="utf-8")
    with pytest.raises(ValueError, match="t_chr"):
        register([a, b], tmp_path)
