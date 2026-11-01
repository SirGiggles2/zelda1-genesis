"""List every NES/Redux-derived file that feeds an object linked into Zelda.md.

A file is "derived" when the extract + Debug build pipeline writes it. It
"feeds" the ROM when a compiler dependency file (-MMD .d) or compile command
(.cmd) under build/rom_project/out names it. The run happens in a staged
copy of the working tree, never in the checkout itself.

    python tools/nesdata/inventory.py --rom <zelda.nes> --redux <redux.nes>          write
    python tools/nesdata/inventory.py --rom <zelda.nes> --redux <redux.nes> --check  compare

T-419 Task 1. Spec: docs/superpowers/specs/2026-10-31-T419-one-file-rom-maker-design.md
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "tools" / "nesdata" / "inventory.json"
BACKSLASH = chr(92)


def stage(root: Path, ws: Path) -> None:
    """Copy tracked + untracked (not ignored) files, SGDK and the local ROMs."""
    listing = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "-z"], cwd=root,
                             capture_output=True, check=True).stdout.decode("utf-8")
    for rel in filter(None, listing.split(chr(0))):
        src = root / rel
        if not src.is_file() or rel.startswith(("builds/", "sgdk/")):
            continue
        dst = ws / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    shutil.copytree(root / "sgdk", ws / "sgdk", dirs_exist_ok=True)
    # check_sgdk_pin.py runs git inside the sgdk submodule.
    shutil.copytree(root / ".git" / "modules" / "sgdk", ws / ".git" / "modules" / "sgdk",
                    dirs_exist_ok=True)
    (ws / "roms").mkdir(exist_ok=True)
    for rom in (root / "roms").glob("*.nes"):
        shutil.copy2(rom, ws / "roms" / rom.name)


def _relative(ws: Path, token: str, bases: tuple[Path, ...]) -> str | None:
    path = Path(token)
    if not path.is_absolute():
        for base in bases:
            if (base / path).exists():
                path = base / path
                break
    try:
        return path.resolve().relative_to(ws.resolve()).as_posix()
    except (ValueError, OSError):
        return None


def derived_inputs(ws: Path, since: float) -> dict[str, list[str]]:
    """File (relative to ws) -> sorted object stems, for files written at or after `since`."""
    out = ws / "build" / "rom_project" / "out"
    bases = (ws / "build" / "rom_project", ws)
    feeds: dict[str, set[str]] = {}

    def note(rel: str | None, obj: str) -> None:
        if rel and (ws / rel).is_file() and (ws / rel).stat().st_mtime >= since:
            feeds.setdefault(rel, set()).add(obj)

    for dep in out.glob("*.d"):
        text = dep.read_text(encoding="utf-8", errors="replace").replace(BACKSLASH + "\n", " ")
        for token in re.split(r"(?<!\\)\s+", text):
            token = token.replace(BACKSLASH + " ", " ").rstrip(":")
            if token and not token.endswith(".o"):
                note(_relative(ws, token, bases), dep.stem)
    for cmd in out.glob("*.cmd"):
        for line in cmd.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.endswith((".c", ".s", ".asm")):
                note(_relative(ws, line, bases), cmd.stem)
    return {rel: sorted(objs) for rel, objs in sorted(feeds.items())}


def run(rom: Path, redux: Path) -> dict[str, list[str]]:
    with tempfile.TemporaryDirectory(prefix="nesdata-inventory-") as tmp:
        ws = Path(tmp) / "ws"
        stage(ROOT, ws)
        time.sleep(2)  # mtime granularity: staged copies must predate the pipeline
        since = time.time()
        log = Path(tmp) / "build.log"
        with log.open("w", encoding="utf-8") as stream:
            rc = subprocess.run([sys.executable, str(ws / "tools" / "converter" / "build.py"), str(rom),
                                 "--redux", str(redux)], cwd=ws, stdout=stream,
                                stderr=subprocess.STDOUT).returncode
        if rc:
            tail = log.read_text(encoding="utf-8", errors="replace")[-2000:]
            raise SystemExit(f"staged build failed (exit {rc}):\n{tail}")
        return derived_inputs(ws, since)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--redux", type=Path, required=True)
    ap.add_argument("--check", action="store_true", help="compare with the committed inventory")
    a = ap.parse_args()
    files = run(a.rom.resolve(), a.redux.resolve())
    if a.check:
        committed = json.loads(INVENTORY.read_text(encoding="utf-8"))["files"]
        added = sorted(set(files) - set(committed))
        removed = sorted(set(committed) - set(files))
        print(f"INVENTORY files={len(files)} added={len(added)} removed={len(removed)}")
        for rel in added:
            print(f"  + {rel}")
        for rel in removed:
            print(f"  - {rel}")
        return 1 if added or removed else 0
    INVENTORY.write_text(json.dumps({"files": files}, indent=1) + "\n", encoding="utf-8")
    print(f"INVENTORY files={len(files)} -> {INVENTORY.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
