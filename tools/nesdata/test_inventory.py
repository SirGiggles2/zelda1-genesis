"""Tests for tools/nesdata/inventory.py (T-419 Task 1)."""
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.nesdata.inventory import derived_inputs  # noqa: E402


def _touch(path: Path, text: str, mtime: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    os.utime(path, (mtime, mtime))


def _out(ws: Path) -> Path:
    return ws / "build" / "rom_project" / "out"


def _make_dep(path: Path) -> str:
    """A path as GCC -MMD writes it: forward slashes, spaces escaped."""
    return str(path).replace("\\", "/").replace(" ", "\\ ")


def test_derived_inputs_reads_d_files(tmp_path):
    ws = tmp_path / "ws"
    _touch(ws / "data" / "x.c", "x", 2000.0)
    _touch(ws / "src" / "y.c", "y", 500.0)
    _touch(_out(ws) / "obj.d",
           f"obj.o: {_make_dep(ws / 'data' / 'x.c')} {_make_dep(ws / 'src' / 'y.c')}\n", 2000.0)
    assert derived_inputs(ws, since=1000.0) == {"data/x.c": ["obj"]}


def test_paths_with_spaces(tmp_path):
    ws = tmp_path / "a b"
    _touch(ws / "data" / "x.c", "x", 2000.0)
    escaped = str(ws / "data" / "x.c").replace("\\", "/").replace(" ", "\\ ")
    _touch(_out(ws) / "obj.d", f"obj.o: {escaped}\n", 2000.0)
    assert derived_inputs(ws, since=1000.0) == {"data/x.c": ["obj"]}


def test_compiled_source_from_cmd_file(tmp_path):
    ws = tmp_path / "ws"
    _touch(ws / "build" / "gen" / "z.c", "z", 2000.0)
    _touch(_out(ws) / "z_obj.cmd", f"gcc\n-c\n{ws / 'build' / 'gen' / 'z.c'}\n-o\nz_obj.o", 2000.0)
    assert derived_inputs(ws, since=1000.0) == {"build/gen/z.c": ["z_obj"]}
