"""Exercise the native compiler/music/runtime and packaged script dispatch."""
from __future__ import annotations
import argparse
import json
import os
import platform
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from platform_tools import tool_path


def check_tools() -> None:
    with tempfile.TemporaryDirectory(prefix="native smoke ") as temp:
        folder = Path(temp)
        source = folder / "native.c"
        source.write_text("unsigned short native_smoke(unsigned short n) { return n + 1; }\n")
        obj = folder / "native.o"
        gcc = tool_path("gcc")
        separator = "\\" if os.name == "nt" else "/"
        subprocess.run([str(gcc), "-B", str(gcc.parent) + separator, "-m68000", "-O2", "-c", str(source), "-o", str(obj)], check=True)
        symbols = subprocess.check_output([str(tool_path("nm")), str(obj)], text=True)
        if "native_smoke" not in symbols:
            raise RuntimeError("Native compiler produced no expected function symbol")
        raw = folder / "native.bin"
        subprocess.run([str(tool_path("objcopy")), "-O", "binary", str(obj), str(raw)], check=True)
        if not raw.is_file() or not raw.read_bytes():
            raise RuntimeError("Native compiler/objcopy produced no code")
        # The pinned SGDK archive contains compressed GCC LTO objects. Merely
        # compiling a new object does not prove the toolchain can read them.
        library_source = folder / "library.c"
        library_source.write_text("extern unsigned short random(void);\nunsigned short native_smoke(void){return random();}\n")
        sdk = Path(__file__).resolve().parents[2] / "sgdk/lib"
        subprocess.run([str(gcc), "-B", str(gcc.parent) + separator,
                        "-m68000", "-O2", "-flto", "-ffunction-sections", "-fdata-sections",
                        "-nostdlib", "-Wl,--gc-sections,-e,native_smoke,-Ttext=0x200",
                        str(library_source), str(sdk / "libmd.a"), str(sdk / "libgcc.a"),
                        "-o", str(folder / "linked.elf")], check=True)
        header = bytearray(0x100)
        header[:4] = b"Vgm "
        struct.pack_into("<I", header, 8, 0x150)
        struct.pack_into("<I", header, 0x2C, 7670453)
        struct.pack_into("<I", header, 0x18, 735)
        struct.pack_into("<I", header, 0x34, 0x100 - 0x34)
        data = header + bytes([0x52, 0x28, 0, 0x62, 0x66])
        struct.pack_into("<I", data, 4, len(data) - 4)
        vgm = folder / "synthetic.vgm"
        vgm.write_bytes(data)
        xgm = folder / "synthetic.xgm"
        subprocess.run([str(tool_path("xgmtool")), str(vgm), str(xgm), "-n", "-s"], check=True)
        if not xgm.is_file() or xgm.stat().st_size == 0:
            raise RuntimeError("Native music compiler produced no XGM")
        result = subprocess.check_output([str(tool_path("node")), "-e", "console.log(JSON.stringify({native:true}))"], text=True)
        if json.loads(result) != {"native": True}:
            raise RuntimeError("Native music runtime did not start")
        print(json.dumps({"status": "ready", "host": platform.system(), "compiler": str(tool_path("gcc")), "code_bytes": raw.stat().st_size, "xgm_bytes": xgm.stat().st_size}))


def check_app(directory: Path) -> None:
    if platform.system() == "Darwin":
        executable = next(directory.glob("*.app/Contents/MacOS/Zelda Genesis Builder"))
    elif os.name == "nt":
        executable = directory / "Zelda Genesis Builder/Zelda Genesis Builder.exe"
    else:
        executable = directory / "Zelda Genesis Builder/Zelda Genesis Builder"
    result = subprocess.run([str(executable.resolve()), "--self-test"], capture_output=True, text=True, timeout=180, check=True)
    if json.loads(result.stdout)["status"] != "ready":
        raise RuntimeError("Packaged runtime self-test failed")
    with tempfile.TemporaryDirectory(prefix="bundled script with spaces ") as temp:
        script = Path(temp) / "probe script.py"
        script.write_text("import json,sys,wave,numpy,scipy,py65\nfrom scipy.signal import resample_poly\nassert len(resample_poly(numpy.ones(4),2,1)) == 8\nprint(json.dumps({'args':sys.argv[1:],'ok':True}))\n")
        result = subprocess.run([str(executable.resolve()), str(script), "path with spaces"], capture_output=True, text=True, timeout=60, check=True)
        if json.loads(result.stdout) != {"args": ["path with spaces"], "ok": True}:
            raise RuntimeError("Packaged Python script dispatch failed")
        # Real extraction uses ProcessPoolExecutor. Frozen workers must reach
        # multiprocessing.freeze_support rather than opening another UI.
        script.write_text(
            "from concurrent.futures import ProcessPoolExecutor\n"
            "import json\n"
            "def square(n): return n*n\n"
            "if __name__ == '__main__':\n"
            " with ProcessPoolExecutor(max_workers=2) as pool:\n"
            "  print(json.dumps(list(pool.map(square,[2,3,4]))))\n")
        result = subprocess.run([str(executable.resolve()), str(script)], capture_output=True, text=True, timeout=60, check=True)
        if json.loads(result.stdout) != [4, 9, 16]:
            raise RuntimeError("Packaged extraction workers did not start correctly")
    print("Packaged runtime, child scripts and multiprocessing workers: PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-dir", type=Path)
    args = parser.parse_args()
    try:
        check_app(args.app_dir) if args.app_dir else check_tools()
    except subprocess.CalledProcessError as exc:
        if exc.stdout:
            print(exc.stdout, file=sys.stderr)
        if exc.stderr:
            print(exc.stderr, file=sys.stderr)
        raise
