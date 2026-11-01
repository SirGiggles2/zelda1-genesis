"""Install bundled source into a writable cache; bootstrap source checkouts."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def cache_directory() -> Path:
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    elif platform.system() == "Darwin":
        base = Path.home() / "Library/Caches"
    else:
        base = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return base / "ZeldaGenesisBuilder"


def install_payload(payload: Path, cache: Path) -> Path:
    manifest = json.loads((payload / ".builder-release.json").read_text(encoding="utf-8"))
    release = manifest["release"]
    if not isinstance(release, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", release):
        raise ValueError("Invalid builder release name")
    target = cache / release
    marker = target / ".builder-release.json"
    if marker.is_file() and marker.read_bytes() == (payload / marker.name).read_bytes():
        return target
    if target.exists():
        raise ValueError("The cached builder is damaged. Remove this release from the builder cache and reopen the app.")
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="install-", dir=cache) as temp:
        stage = Path(temp) / "release"
        stage.mkdir()
        for name, expected in manifest["files"].items():
            rel = Path(name)
            if rel.is_absolute() or ".." in rel.parts or ":" in name or "\\" in name:
                raise ValueError("Invalid builder payload path")
            source = payload / rel
            if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
                raise ValueError(f"The builder download is damaged: {name}. Download it again.")
            dest = stage / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
        shutil.copy2(payload / marker.name, stage / marker.name)
        try:
            stage.rename(target)
        except OSError:
            # A second launch may have finished installing the same release.
            if not marker.is_file() or marker.read_bytes() != (payload / marker.name).read_bytes():
                raise
    return target


def prepare_workspace() -> Path:
    if not getattr(sys, "frozen", False):
        return ROOT
    return install_payload(Path(sys._MEIPASS) / "payload", cache_directory())


def prepare_runtime() -> str | None:
    """Source-only convenience: install pinned packages in an isolated venv."""
    if getattr(sys, "frozen", False):
        return None
    if all(importlib.util.find_spec(name) is not None for name in ("numpy", "scipy", "py65", "certifi")):
        return None
    env = ROOT / "build/builder-venv"
    python = env / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if Path(sys.executable).resolve() == python.resolve():
        raise RuntimeError("Builder setup did not finish. Reopen the builder while connected to the internet.")
    if not python.exists():
        subprocess.run([sys.executable, "-m", "venv", str(env)], check=True)
    subprocess.run([str(python), "-m", "pip", "install", "-r",
                    str(ROOT / "tools/converter/requirements.txt")], check=True)
    return str(python)
