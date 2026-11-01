"""Tests for the builder page's Python side: inputs.py and splice.py (T-419 Task 13).

Both modules run unchanged inside Pyodide; these tests run them natively with
the real local ROMs and Redux files.
"""
from __future__ import annotations

import hashlib
import io
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
PAGE = ROOT / "tools" / "release" / "builder_page"
sys.path.insert(0, str(PAGE))
sys.path.insert(0, str(ROOT / "tools" / "converter"))
sys.path.insert(0, str(ROOT))
import inputs  # noqa: E402
import splice  # noqa: E402
from tools.release.make_builder import known_inputs  # noqa: E402

NES = ROOT / "roms" / "Legend of Zelda, The (USA).nes"
REDUX = ROOT / "roms" / "Zelda Redux.nes"
IPS = ROOT / "reference" / "Zelda1-Redux" / "patches" / "Zelda1_Redux.ips"
ZIP = ROOT / "reference" / "Zelda1-Redux" / "The-Legend-of-Zelda-Redux-3.3.3.zip"
pytestmark = pytest.mark.skipif(not all(p.is_file() for p in (NES, REDUX, IPS)), reason="needs local ROMs + patch")
KNOWN = known_inputs()


def _prepare(*files):
    return inputs.prepare([(Path(p).name if isinstance(p, Path) else p[0],
                            p.read_bytes() if isinstance(p, Path) else p[1]) for p in files], KNOWN)


def test_rom_and_ips():
    nes, redux = _prepare(NES, IPS)
    assert nes == NES.read_bytes() and redux == REDUX.read_bytes()


def test_headerless_prg0_accepted():
    nes, _ = _prepare(("zelda.nes", NES.read_bytes()[16:]), IPS)
    assert nes == NES.read_bytes()


def test_patched_redux_rom_accepted():
    nes, redux = _prepare(NES, REDUX)
    assert redux == REDUX.read_bytes()


@pytest.mark.skipif(not ZIP.is_file(), reason="needs the Redux release zip")
def test_zip_with_ips_accepted():
    _, redux = _prepare(NES, ZIP)
    assert redux == REDUX.read_bytes()


def test_wrong_revision_message():
    bad = bytearray(NES.read_bytes())
    bad[0x100] ^= 0xFF
    with pytest.raises(inputs.InputError, match=r"^This is not the USA PRG0 ROM\.$"):
        _prepare(("zelda.nes", bytes(bad)), IPS)


def test_wrong_patch_message():
    junk = b"PATCH" + b"\x00\x10\x00\x00\x01\x42" + b"EOF"
    with pytest.raises(inputs.InputError, match=r"^This is not the Zelda Redux v3\.3\.3 patch\.$"):
        _prepare(NES, ("Zelda1_Redux.ips", junk))


def test_missing_inputs_messages():
    with pytest.raises(inputs.InputError, match="Add your Zelda NES ROM"):
        _prepare(IPS)
    with pytest.raises(inputs.InputError, match="Add the Zelda Redux patch"):
        _prepare(NES)


def test_finish_inserts_fixes_checksum_and_verifies():
    rom = bytearray(b"\x00" * 0x400)
    rom[0x300:0x304] = b"\x12\x34\x56\x78"
    base = bytearray(rom)
    base[0x300:0x304] = b"\0\0\0\0"
    base[0x18E:0x190] = b"\0\0"
    good = bytearray(rom)
    s = sum((good[i] << 8 | good[i + 1]) for i in range(0x200, len(good), 2)) & 0xFFFF
    good[0x18E:0x190] = s.to_bytes(2, "big")
    recipe = {"nesdata": {"offset": 0x300, "size": 4}, "rom_sha256": hashlib.sha256(bytes(good)).hexdigest(),
              "rom_size": len(good)}
    assert splice.finish(bytes(base), b"\x12\x34\x56\x78", recipe) == bytes(good)
    with pytest.raises(splice.MismatchError):
        splice.finish(bytes(base), b"\x12\x34\x56\x79", recipe)
