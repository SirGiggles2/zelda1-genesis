"""Logic behind the converter window (tools/builder/gui.py), kept free of
any GUI code so it can be tested headless.

- classify(): what a dropped/picked file is (original ROM, Redux ROM,
  Redux patch or release zip, unsupported ROM, unknown).
- assign(): sort a set of dropped files into the converter's inputs.
- command(): the tools/builder/build.py invocation for those inputs.
- run(): run it, calling back with each output line and each
  "@@step i/n label" / "@@done path sha256=..." progress record.
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import build  # noqa: E402

REQUIREMENTS = ROOT / "tools" / "builder" / "requirements.txt"
MAX_INPUT = 16 * 1024 * 1024


@dataclass
class Inputs:
    rom: Path | None = None
    redux_rom: Path | None = None
    redux_patch: Path | None = None
    output: Path | None = None
    notes: list[str] = field(default_factory=list)

    def ready(self) -> bool:
        return self.rom is not None and (self.redux_rom or self.redux_patch) is not None


def classify(path: Path) -> tuple[str, str]:
    """(kind, detail). kind: rom | redux_rom | redux_patch | bad_rom | unknown."""
    try:
        if path.stat().st_size > MAX_INPUT:
            return "unknown", "file is too large to be a NES ROM or patch"
        data = path.read_bytes()
    except OSError as e:
        return "unknown", f"cannot read: {e}"
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            if any(n.lower().endswith((".ips", ".bps")) for n in z.namelist()):
                return "redux_patch", "Redux release archive"
        return "unknown", "zip without an .ips/.bps patch"
    if data[:5] == b"PATCH" or data[:4] == b"BPS1":
        return "redux_patch", "ROM patch"
    if data[:4] == b"NES\x1a":
        h = hashlib.sha256(data).hexdigest()
        if h in build.SUPPORTED_ROMS:
            return "rom", build.SUPPORTED_ROMS[h]
        if h in build.SUPPORTED_REDUX_ROMS:
            return "redux_rom", build.SUPPORTED_REDUX_ROMS[h]
        return "bad_rom", (f"unsupported NES ROM (sha256 {h[:16]}...). Use The Legend of "
                           "Zelda (USA) PRG0, iNES")
    return "unknown", "not a NES ROM, IPS/BPS patch or Redux release zip"


def assign(paths: list[Path], into: Inputs | None = None) -> Inputs:
    inp = into or Inputs()
    for p in paths:
        kind, detail = classify(p)
        if kind == "rom":
            inp.rom = p
        elif kind == "redux_rom":
            inp.redux_rom, inp.redux_patch = p, None
        elif kind == "redux_patch":
            inp.redux_patch, inp.redux_rom = p, None
        inp.notes.append(f"{p.name}: {detail}")
    if inp.rom and inp.output is None:
        inp.output = default_output(inp.rom)
    return inp


def default_output(rom: Path) -> Path:
    return rom.with_name("Zelda.md")


def console_python() -> str:
    """The console interpreter for the build. The GUI runs under pythonw.exe,
    which has no console; a build started with it makes every console tool it
    runs (extractors, gcc, cc1, as, ld) open a window of its own."""
    exe = Path(sys.executable)
    if exe.name.lower() == "pythonw.exe" and (exe.parent / "python.exe").exists():
        return str(exe.parent / "python.exe")
    return str(exe)


def command(inp: Inputs) -> list[str]:
    argv = [console_python(), str(ROOT / "tools" / "builder" / "build.py"), str(inp.rom)]
    if inp.redux_rom:
        argv += ["--redux", str(inp.redux_rom)]
    elif inp.redux_patch:
        argv += ["--redux-patch", str(inp.redux_patch)]
    if inp.output:
        argv += ["--output", str(inp.output)]
    return argv


def missing_modules() -> list[str]:
    out = []
    for mod in build.REQUIRED_MODULES:
        try:
            __import__(mod)
        except ImportError:
            out.append(mod)
    return out


def install_command() -> list[str]:
    return [console_python(), "-m", "pip", "install", "-r", str(REQUIREMENTS)]


def run(argv: list[str], on_line: Callable[[str], None],
        on_step: Callable[[int, int, str], None] | None = None,
        on_done: Callable[[str, str], None] | None = None) -> int:
    """Run argv, streaming merged stdout/stderr lines."""
    # Windows: one hidden console for the build; every tool it starts
    # inherits it, so no console windows pop up.
    extra = {}
    if os.name == "nt":
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = 0                      # SW_HIDE
        extra = {"creationflags": subprocess.CREATE_NEW_CONSOLE, "startupinfo": si}
    proc = subprocess.Popen(argv, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", bufsize=1, **extra)
    assert proc.stdout is not None
    for line in proc.stdout:
        line = line.rstrip("\n")
        if line.startswith("@@step ") and on_step:
            frac, _, label = line[7:].partition(" ")
            i, _, n = frac.partition("/")
            on_step(int(i), int(n), label)
        elif line.startswith("@@done ") and on_done:
            path, _, sha = line[7:].rpartition(" sha256=")
            on_done(path, sha)
        on_line(line)
    return proc.wait()
