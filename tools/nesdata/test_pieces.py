"""Tests for the .nesdata piece registry and link-file writers (T-419 Task 2)."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.nesdata.link import check_sizes, write_asm, write_layout  # noqa: E402
from tools.nesdata.pieces import PIECES, Piece, by_name, load_registry, write_piece  # noqa: E402


def _piece(name: str, size: int) -> Piece:
    return Piece(name=name, symbol=f"sym_{name}", step="misc", path=f"build/nesdata/pieces/{name}.bin",
                 size=size)


def test_layout_header(tmp_path):
    out = tmp_path / "nesdata_layout.h"
    write_layout((_piece("gray", 64),), out)
    assert "#define NESDATA_GRAY_SIZE 64" in out.read_text(encoding="utf-8")


def test_check_sizes_wrong_size(tmp_path):
    p = _piece("gray", 64)
    (tmp_path / p.path).parent.mkdir(parents=True)
    (tmp_path / p.path).write_bytes(b"\0" * 63)
    errors = check_sizes((p,), tmp_path)
    assert len(errors) == 1 and "gray" in errors[0] and "63" in errors[0]


def test_check_sizes_missing(tmp_path):
    errors = check_sizes((_piece("gray", 64),), tmp_path)
    assert len(errors) == 1 and "missing" in errors[0]


def test_asm_order_and_align(tmp_path):
    out = tmp_path / "nesdata.s"
    write_asm((_piece("b", 3), _piece("a", 4)), out, tmp_path)
    lines = [l.strip() for l in out.read_text(encoding="utf-8").splitlines() if l.strip()]
    assert lines[0] == '.section .nesdata,"a"'
    b_at, a_at = lines.index("sym_b:"), lines.index("sym_a:")
    assert b_at < a_at
    assert lines[b_at - 2] == ".balign 2" and lines[a_at - 2] == ".balign 2"
    assert lines[b_at - 1] == ".global sym_b"
    assert ".type sym_b, @object" in lines and ".size sym_b, 3" in lines


def test_write_piece_writes_registered_path(tmp_path):
    p = PIECES[0]
    write_piece(p.name, b"\x12" * p.size, root=tmp_path)
    assert (tmp_path / p.path).read_bytes() == b"\x12" * p.size


def test_write_piece_rejects_wrong_size(tmp_path):
    p = PIECES[0]
    try:
        write_piece(p.name, b"\0" * (p.size + 1), root=tmp_path)
    except ValueError as exc:
        assert p.name in str(exc)
    else:
        raise AssertionError("wrong-size piece accepted")


def test_load_registry_appends_json_entries(tmp_path):
    reg = tmp_path / "pieces.json"
    reg.write_text('{"pieces": [{"name": "t_chr", "symbol": "t_chr", "source": "a.c", '
                   '"size": 2, "align": 4}]}', encoding="utf-8")
    (p,) = load_registry(reg)
    assert p == Piece(name="t_chr", symbol="t_chr", step="source:a.c",
                      path="build/nesdata/pieces/t_chr.bin", size=2, align=4)
    assert load_registry(tmp_path / "absent.json") == ()


def test_registry_names_unique():
    names = [p.name for p in PIECES]
    assert len(names) == len(set(names))
    assert by_name(PIECES[0].name) is PIECES[0]
