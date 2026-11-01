"""Tests for tools/release/make_builder.py (T-419 Task 12)."""
from __future__ import annotations

import base64
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.release import make_builder as mb  # noqa: E402


def test_blank_zeroes_only_range():
    rom = bytes(range(1, 33))
    out = mb.blank(rom, 8, 4)
    assert out[:8] == rom[:8] and out[8:12] == b"\0" * 4 and out[12:] == rom[12:]


def test_recipe_hashes(tmp_path):
    (tmp_path / "build/nesdata/pieces").mkdir(parents=True)
    (tmp_path / "build/nesdata/pieces/p.bin").write_bytes(b"abc")
    from tools.nesdata.pieces import Piece  # noqa: PLC0415
    rom = b"\x11" * 16
    r = mb.recipe(rom, start=4, size=8, pieces=(Piece("p", "p", "x", "build/nesdata/pieces/p.bin", 3),),
                  root=tmp_path, commit="abc123")
    assert r["rom_sha256"] == hashlib.sha256(rom).hexdigest() and r["rom_size"] == 16
    assert r["nesdata"] == {"offset": 4, "size": 8} and r["commit"] == "abc123"
    assert r["pieces"] == [{"name": "p", "size": 3, "sha256": hashlib.sha256(b"abc").hexdigest()}]


def test_bundle_keeps_only_pre_existing_non_derived_files(tmp_path):
    (tmp_path / "tools").mkdir()
    (tmp_path / "tools/a.py").write_bytes(b"print(1)\n")
    (tmp_path / "data").mkdir()
    (tmp_path / "data/derived.c").write_text("x\n", encoding="utf-8")
    reads = ["tools/a.py", "data/derived.c", "build/generated_later.bin"]
    bundle = mb.python_bundle(tmp_path, reads, existed={"tools/a.py", "data/derived.c"},
                              derived={"data/derived.c"})
    assert list(bundle) == ["tools/a.py"]
    assert base64.b64decode(bundle["tools/a.py"]) == b"print(1)\n"


def test_bundle_skips_files_the_run_rewrote(tmp_path):
    (tmp_path / "dat").mkdir()
    (tmp_path / "dat/bank.bin").write_bytes(b"from rom")
    bundle = mb.python_bundle(tmp_path, ["dat/bank.bin"], existed={"dat/bank.bin"}, derived=set(),
                              written={"dat/bank.bin"})
    assert bundle == {}


def test_worker_source_inlines_libraries_without_modules():
    import re  # noqa: PLC0415
    src = mb.worker_source()
    assert "\nexport " not in src and not re.search(r"^import .* from [\"'].*[\"'];?$", src, re.M)
    for name in ("function runWasi", "class Nes", "function dumpJobs", "function renderSong",
                 "function studioRender", "async function build"):
        assert name in src
    assert "__PYODIDE_URL__" not in src and "cdn.jsdelivr.net/pyodide/v0.27.7/full/" in src
    # Python embedded in worker.js keeps its import lines (only JS module syntax is stripped).
    assert "\nimport hashlib, json, sys\n" in src and "\nimport splice\n" in src


def test_pack_fills_every_placeholder(tmp_path):
    out = tmp_path / "page.html"
    mb.pack(base=b"\x01\x02", recipe={"commit": "abc", "inputs": {}}, bundle={"a.py": "eA=="}, out=out)
    html = out.read_text(encoding="utf-8")
    assert "__" + "BASE__" not in html and "__" + "WORKER__" not in html and "__" + "APP__" not in html
    assert '<script type="text/plain" id="base">AQI=</script>' in html
    assert html.count("</script>") == 6


def test_bundle_rejects_rom_bytes(tmp_path):
    window = bytes(range(40, 72))
    (tmp_path / "x.bin").write_bytes(b"\0" + window)
    assert mb.rom_hits({"x.bin": base64.b64encode(b"\0" + window).decode()}, [b"\xff" + window]) == ["x.bin"]
