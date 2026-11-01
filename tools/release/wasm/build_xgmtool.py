"""Rebuild tools/release/builder_page/xgmtool.wasm from the pinned SGDK source.

Release-machine tool only (players never compile). Uses Zig's C compiler for
wasm32-wasi; the page runs the result through builder_page/wasi_mini.js.
tools/nesdata/test_host.py proves wasm output == native xgmtool on every song.

    python tools/release/wasm/build_xgmtool.py [--zig <zig.exe>]

T-419 Task 9. Zig 0.17.0 (ziglang.org, sha256 of the Windows zip
b5663f69581dcf391293fbf16c06cb80d81d806545ce618b4d0bab7f0eb8c428).
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "sgdk" / "tools" / "xgmtool"
OUT = ROOT / "tools" / "release" / "builder_page" / "xgmtool.wasm"
DEFAULT_ZIG = ROOT / "build" / "zig" / "zig-x86_64-windows-0.17.0" / "zig.exe"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--zig", type=Path, default=DEFAULT_ZIG if DEFAULT_ZIG.is_file() else None)
    a = ap.parse_args()
    zig = a.zig or shutil.which("zig")
    if not zig:
        print("ERROR: Zig not found; pass --zig <path to zig>")
        return 1
    sources = sorted(str(p) for p in (SRC / "src").glob("*.c"))
    subprocess.run([str(zig), "cc", "-target", "wasm32-wasi", "-O2", "-s", *sources,
                    f"-I{SRC / 'inc'}", "-o", str(OUT)], check=True, stderr=subprocess.DEVNULL)
    digest = hashlib.sha256(OUT.read_bytes()).hexdigest()
    print(f"xgmtool.wasm {OUT.stat().st_size} bytes sha256={digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
