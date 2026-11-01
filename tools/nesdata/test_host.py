"""Tests for the host bridge and xgmtool.wasm (T-419 Task 9).

The browser runs xgmtool.wasm through tools/release/builder_page/wasi_mini.js.
These tests run that same shim under Node and require output identical to
SGDK's native xgmtool on every song the Debug build converts.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools" / "converter"))
from tools.nesdata import host  # noqa: E402

PAGE = ROOT / "tools" / "release" / "builder_page"
VGMS = sorted((ROOT / "build" / "nes_music").glob("*.vgm")) + sorted((ROOT / "build" / "redux_music").glob("*.vgm"))


def _native(vgm: Path) -> bytes:
    from platform_tools import tool_command  # noqa: PLC0415
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "song.bin"
        subprocess.run(tool_command("xgmtool", ROOT) + [str(vgm), str(out), "-n", "-s"], check=True,
                       capture_output=True, stdin=subprocess.DEVNULL)
        return out.read_bytes()


def _wasm_node(vgm: Path) -> bytes:
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "song.bin"
        subprocess.run(["node", str(PAGE / "run_xgmtool.mjs"), str(PAGE / "xgmtool.wasm"), str(vgm), str(out)],
                       check=True, capture_output=True, stdin=subprocess.DEVNULL)
        return out.read_bytes()


@pytest.mark.skipif(not VGMS or shutil.which("node") is None, reason="needs a Debug build's VGMs and Node")
@pytest.mark.parametrize("vgm", VGMS, ids=[v.name for v in VGMS])
def test_xgmtool_wasm_matches_native(vgm):
    assert _wasm_node(vgm) == _native(vgm)


@pytest.mark.skipif(not VGMS, reason="needs a Debug build's VGMs")
def test_host_xgmtool_native_matches_cli():
    assert host.xgmtool(VGMS[0].read_bytes()) == _native(VGMS[0])
