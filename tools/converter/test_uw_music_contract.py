"""Compile the options runtime with host GCC and verify UW music persistence.

This supplements the headless Genesis test of the actual two XGM blobs.
No SGDK fake or replacement options implementation is used.
"""
from __future__ import annotations
import ctypes as C
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix="uw music contract ") as temp:
        libpath = Path(temp)/("options.dll" if os.name == "nt" else "options.so")
        subprocess.run(["gcc","-shared","-fPIC","-Wall","-Werror",
                        str(ROOT/"src/game/options/options_runtime.c"),"-o",str(libpath)],check=True)
        lib = C.CDLL(str(libpath))
        lib.options_runtime_init()
        assert lib.options_get(15) == 0
        lib.options_set(15,1)
        assert lib.options_get(15) == 1
        for bad in (2,3,255):
            lib.options_set(15,bad)
            assert lib.options_get(15) == 1
        Buf = C.c_ubyte*32
        buf = Buf()
        assert lib.options_runtime_serialize(buf,32) == 32
        assert buf[2] == 3 and buf[12] == 1
        lib.options_runtime_init()
        assert lib.options_runtime_apply(buf,32) == 1 and lib.options_get(15) == 1
        for version in (1,2):
            old = Buf(*buf)
            old[2],old[12] = version,0
            checksum = sum(old[:30])&65535
            old[30],old[31] = checksum>>8,checksum&255
            assert lib.options_runtime_apply(old,32) == 1
            assert lib.options_get(15) == 0
            assert lib.options_runtime_serialize(old,32) == 32 and old[2] == 3
        buf[31] ^= 1
        assert lib.options_runtime_apply(buf,32) == 0
        print("UW music options contract: PASS (default, two choices, range, v3 roundtrip, v1/v2 migration, checksum)")


if __name__ == "__main__":
    main()
