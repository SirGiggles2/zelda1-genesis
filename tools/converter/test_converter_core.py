"""Headless tests for tools/converter/converter_core.py.

Usage: python tools/converter/test_converter_core.py <zelda.nes> <Zelda1_Redux.ips>
(the two user files; nothing is written next to them).
"""
from __future__ import annotations

import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import converter_core as core  # noqa: E402


def main() -> int:
    rom_src, patch_src = Path(sys.argv[1]), Path(sys.argv[2])
    rom_bytes = rom_src.read_bytes()
    tmp = Path(tempfile.mkdtemp(prefix="conv test "))   # spaces in every path
    try:
        rom = tmp / "My ROMs" / "Legend of Zelda (U).nes"
        rom.parent.mkdir()
        rom.write_bytes(rom_bytes)
        patch = tmp / "Zelda1 Redux.ips"
        shutil.copyfile(patch_src, patch)
        rel = tmp / "Zelda Redux v3.3.3.zip"
        with zipfile.ZipFile(rel, "w") as z:
            z.write(patch, "patches/Zelda1_Redux.ips")
            z.writestr("patches/optional/Bluer Tunic.ips", b"PATCH" + b"EOF")
        bad = tmp / "other.nes"
        bad.write_bytes(rom_bytes[:-1] + b"\x00")
        junk = tmp / "notes.txt"
        junk.write_text("hello")

        checks = [
            (core.classify(rom)[0], "rom"),
            (core.classify(patch)[0], "redux_patch"),
            (core.classify(rel)[0], "redux_patch"),
            (core.classify(bad)[0], "bad_rom"),
            (core.classify(junk)[0], "unknown"),
        ]
        inp = core.assign([junk, rel, rom])
        checks += [
            (inp.ready(), True),
            (inp.rom, rom),
            (inp.redux_patch, rel),
            (inp.output, rom.with_name("Zelda.md")),
        ]
        argv = core.command(inp)
        checks += [
            (argv[2], str(rom)),
            (argv[argv.index("--redux-patch") + 1], str(rel)),
            (argv[argv.index("--output") + 1], str(inp.output)),
        ]
        script = tmp / "fake build.py"
        script.write_text('print("@@step 1/2 a", flush=True)\nprint("hello")\n'
                          'print("@@step 2/2 b", flush=True)\nprint("@@done C:/x y.md sha256=ab")\n')
        steps, done, lines = [], [], []
        rc = core.run([sys.executable, str(script)], lines.append,
                      lambda i, n, label: steps.append((i, n, label)),
                      lambda p, s: done.append((p, s)))
        checks += [(rc, 0), (steps, [(1, 2, "a"), (2, 2, "b")]),
                   (done, [("C:/x y.md", "ab")]), ("hello" in lines, True)]
        bad_n = 0
        for i, (got, want) in enumerate(checks):
            if got != want:
                bad_n += 1
                print(f"FAIL check {i}: got {got!r} want {want!r}")
        unchanged = rom.read_bytes() == rom_bytes
        print(f"converter_core: {len(checks) - bad_n}/{len(checks)} checks pass; "
              f"input ROM unchanged: {unchanged}")
        return 0 if bad_n == 0 and unchanged else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
