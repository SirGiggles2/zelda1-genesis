"""Export the public repository tree (zelda1-genesis) from this working tree.

Contents: the release package (make_package.package_files: the files a full
conversion reads + entry points), without sgdk/ (a submodule pinned at
tools/sgdk_pin.json's commit), plus the release checks and their imports
(REPO_EXTRA), plus release/README.md, LICENSE, NOTICE and gitignore (.gitignore: every
file a conversion writes, listed from a build in an exported tree) at the root.
Every file passes the same checks as the package: nothing game-derived
(package_check.is_banned) and no local paths (make_package.local_paths).

    python tools/builder/export_repo.py <empty target dir> [--copy-sgdk]
--copy-sgdk copies the local sgdk/ checkout into the target (for building
there) instead of leaving the submodule unpopulated.
Exit: 0 written; 1 a file failed a check or the target is not empty.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import make_package  # noqa: E402
from package_check import is_banned  # noqa: E402

# Release checks (not read by a conversion) and what they import.
REPO_EXTRA = (
    "tools/builder/boot_check.py", "tools/builder/from_scratch_gate.py",
    "tools/builder/make_package.py", "tools/builder/package_check.py",
    "tools/builder/package_closure.py", "tools/builder/export_repo.py",
    "tools/builder/test_converter_core.py", "tools/builder/rom_bytes_scan.py",
    "tools/builder/local_music.py",
    "tools/builder/test_nes_music.py",
    "tools/builder/test_nes_music_runtime.py",
    "tools/builder/test_uw_music_contract.py",
    "tools/emu/gpgx.py", "tools/emu/build_gpgx.sh", "tools/emu/preset_run.py",
    "tools/emu/nes_preset_run.py", "tools/emu/scenario.py",
    "tools/lockstep/presets.py", "tools/lockstep/gate.py",
    "tools/lockstep/presets/newgame.json",
)
ROOT_FILES = {"README.md": "release/README.md", "LICENSE": "release/LICENSE",
              "NOTICE": "release/NOTICE", ".gitignore": "release/gitignore"}
SGDK_URL = "https://github.com/Stephane-D/SGDK"


def repo_files() -> dict[str, Path]:
    """target path -> source file."""
    out = {f: ROOT / f for f in make_package.package_files(require_sgdk=False)
           if not f.startswith("sgdk/")}
    out.update({f: ROOT / f for f in REPO_EXTRA})
    out.update({dst: ROOT / src for dst, src in ROOT_FILES.items()})
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("target", type=Path)
    ap.add_argument("--copy-sgdk", action="store_true")
    a = ap.parse_args()
    if a.target.exists() and any(a.target.iterdir()):
        print(f"ERROR: {a.target} is not empty", file=sys.stderr)
        return 1
    files = repo_files()
    bad = []
    for dst, src in sorted(files.items()):
        if not src.is_file():
            bad.append(f"{dst} (missing source {src.relative_to(ROOT)})")
            continue
        data = src.read_bytes()
        if is_banned(dst)[0]:
            bad.append(f"{dst} (game-derived)")
        elif hits := make_package.local_paths(dst, data):
            bad.append(f"{dst} (local path {', '.join(hits)})")
    if bad:
        print(f"ERROR: {len(bad)} rejected files:", file=sys.stderr)
        for b in bad[:30]:
            print(f"  {b}", file=sys.stderr)
        return 1
    a.target.mkdir(parents=True, exist_ok=True)
    for dst, src in files.items():
        out = a.target / dst
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, out)
    pin = json.loads((ROOT / "tools" / "sgdk_pin.json").read_text(encoding="utf-8"))
    (a.target / ".gitmodules").write_text(
        f'[submodule "sgdk"]\n\tpath = sgdk\n\turl = {SGDK_URL}\n', encoding="utf-8")
    if a.copy_sgdk:
        shutil.copytree(ROOT / "sgdk", a.target / "sgdk",
                        ignore=shutil.ignore_patterns(".git"))
    print(f"exported {len(files)} files to {a.target}; sgdk submodule at "
          f"{pin['pinned_sha'][:8]} ({pin['pinned_tag']})"
          + (" (local copy)" if a.copy_sgdk else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
