"""Tests for tools/nesdata/assemble.py (T-419 Task 11)."""
from __future__ import annotations


import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.nesdata import assemble  # noqa: E402
from tools.nesdata.pieces import Piece  # noqa: E402


def _piece(name, size, align=2):
    return Piece(name=name, symbol=name, step="x", path=f"build/nesdata/pieces/{name}.bin", size=size, align=align)


def test_layout_matches_linker_padding(tmp_path):
    for name, data in (("a", b"abc"), ("b", b"defg")):
        (tmp_path / "build/nesdata/pieces").mkdir(parents=True, exist_ok=True)
        (tmp_path / f"build/nesdata/pieces/{name}.bin").write_bytes(data)
    assert assemble.section((_piece("a", 3), _piece("b", 4)), tmp_path, start=0x1000) == b"abc\0defg"


def test_layout_alignment_is_absolute(tmp_path):
    (tmp_path / "build/nesdata/pieces").mkdir(parents=True)
    (tmp_path / "build/nesdata/pieces/a.bin").write_bytes(b"x")
    (tmp_path / "build/nesdata/pieces/b.bin").write_bytes(b"y")
    # The input section is aligned to its largest piece alignment (4): a at $1004,
    # b at $1008, section end rounded up to 4 ($100C) like debug.ld's ALIGN(0x4).
    got = assemble.section((_piece("a", 1), _piece("b", 1, 4)), tmp_path, start=0x1002)
    assert got == b"\0\0x\0\0\0y\0\0\0"


def test_steps_skip_audits_and_include_build_time_generators():
    labels = [label for label, _, _ in assemble.steps(Path("rom.nes"), Path("redux.nes"))]
    assert "verify generated freshness" not in labels and "check_uw_room_coverage.py" not in labels
    assert "build_redux_music.py" in labels and "mp_link_poses.py" in labels
    assert labels.index("build_redux_music.py") > labels.index("music from your ROM")


def test_steps_cannot_start_processes(tmp_path):
    # Like the browser: a step that starts a process fails, even if natively it could.
    script = tmp_path / "step.py"
    script.write_text("import subprocess, sys\nsubprocess.run([sys.executable, '-c', 'pass'])\n", encoding="utf-8")
    with pytest.raises(assemble.StepError, match="process"):
        assemble.run_step("bad", "step.py", (), tmp_path)


def test_step_imports_neighbour_modules_like_python_script(tmp_path):
    (tmp_path / "pkg").mkdir()
    (tmp_path / "pkg" / "helper_t419.py").write_text("VALUE = 7\n", encoding="utf-8")
    (tmp_path / "pkg" / "step.py").write_text("from helper_t419 import VALUE\nassert VALUE == 7\n", encoding="utf-8")
    assemble.run_step("neighbour", "pkg/step.py", (), tmp_path)


def test_host_may_start_processes_inside_steps(tmp_path):
    script = tmp_path / "step.py"
    script.write_text("import sys\nsys.path.insert(0, %r)\nfrom tools.nesdata import host\n"
                      "with host.allow_processes():\n"
                      "    import subprocess\n    subprocess.run([sys.executable, '-c', 'pass'], check=True)\n"
                      % str(ROOT), encoding="utf-8")
    assemble.run_step("ok", "step.py", (), tmp_path)
