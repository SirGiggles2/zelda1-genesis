"""Resolve native builder tools without changing the pinned SGDK libraries.

Windows uses SGDK's own executables. Linux/macOS packages supply the complete
m68k-elf toolchain and xgmtool under toolchains/native/bin. Developers can use
ZELDA_TOOLCHAIN_BIN or target-prefixed tools on PATH. Resolution is lazy so
extractors and tooling that only import build descriptions need no compiler.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
_NATIVE_NAMES = {
    "gcc": "m68k-elf-gcc",
    "objcopy": "m68k-elf-objcopy",
    "nm": "m68k-elf-nm",
    "xgmtool": "xgmtool",
    "node": "node",
}


class ToolNotFoundError(RuntimeError):
    """A required native tool is missing or unusable on this host."""


def _usable(path: Path, windows: bool) -> bool:
    if not path.is_file():
        return False
    if windows:
        return True
    # Reject PE files even when renamed or reached through a symlink. The
    # executable bit alone is not evidence of a native Unix executable.
    if path.resolve().suffix.lower() == ".exe" or not os.access(path, os.X_OK):
        return False
    try:
        with path.open("rb") as stream:
            return stream.read(2) != b"MZ"
    except OSError:
        return False


def tool_path(name: str, root: Path = ROOT) -> Path:
    """Return a usable native executable or raise an actionable error.

    ZELDA_TOOLCHAIN_BIN is authoritative for cross tools: a broken override
    cannot silently select another compiler. Node is an independent runtime
    and may still come from the packaged native directory or PATH.
    """
    if name not in _NATIVE_NAMES:
        raise ValueError(f"Unsupported builder tool: {name}")
    windows = sys.platform == "win32"
    if not windows and not (sys.platform == "darwin" or sys.platform.startswith("linux")):
        raise ToolNotFoundError(f"Unsupported builder platform: {sys.platform}")
    filename = f"{name}.exe" if windows else _NATIVE_NAMES[name]
    override = os.environ.get("ZELDA_TOOLCHAIN_BIN")
    if override and name != "node":
        folder = Path(override).expanduser().resolve()
        candidates = [folder / filename]
        # Native Windows distributions may preserve the m68k-elf prefix.
        if windows and name in ("gcc", "objcopy", "nm"):
            candidates.append(folder / f"{_NATIVE_NAMES[name]}.exe")
    else:
        candidates = []
        if windows and name != "node":
            candidates.append(Path(root) / "sgdk" / "bin" / filename)
        candidates.append(Path(root) / "toolchains" / "native" / "bin" / filename)
        path_filename = (f"{_NATIVE_NAMES[name]}.exe"
                         if windows and name in ("gcc", "objcopy", "nm") else filename)
        for folder in os.get_exec_path():
            # An empty PATH entry means cwd, which must not silently select
            # an unrelated tool from a user's working directory.
            if folder:
                candidates.append(Path(folder) / path_filename)
    for candidate in candidates:
        if _usable(candidate, windows):
            return candidate.resolve()
    searched = ", ".join(str(path) for path in candidates) or "PATH (empty)"
    setting = (f"Check ZELDA_TOOLCHAIN_BIN={override!r}. "
               if override and name != "node" else "")
    raise ToolNotFoundError(
        f"Missing native {filename} for {sys.platform}. {setting}"
        "Extract the complete builder package for your platform, or install the "
        "native tools and set ZELDA_TOOLCHAIN_BIN to their bin directory "
        f"(Node may use PATH). Searched: {searched}")


def tool_command(name: str, root: Path = ROOT) -> list[str]:
    """Executable argv prefix; callers append arguments without a shell."""
    return [str(tool_path(name, root))]
