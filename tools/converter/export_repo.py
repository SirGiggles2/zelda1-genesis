"""Export the public repository tree (zelda1-genesis) from this working tree.

Contents: the release package (make_package.package_files: the files a full
conversion reads + entry points), without sgdk/ (a submodule pinned at
tools/sgdk_pin.json's commit), plus the release checks and their imports
(REPO_EXTRA), plus release/README.md, LICENSE, NOTICE and gitignore (.gitignore: every
file a conversion writes, listed from a build in an exported tree) at the root.
Every file passes the same checks as the package: nothing game-derived
(package_check.is_banned) and no local paths (make_package.local_paths).

    python tools/converter/export_repo.py <empty target dir> [--copy-sgdk] [--source-ref HEAD]
--copy-sgdk copies the local sgdk/ checkout into the target (for building
there) instead of leaving the submodule unpopulated.
Exit: 0 written; 1 a file failed a check or the target is not empty.
"""
from __future__ import annotations

import argparse
import json
import io
import tarfile
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "converter"))
import make_package  # noqa: E402
import public_layout  # noqa: E402
from package_check import is_banned  # noqa: E402

# Release checks (not read by a conversion) and what they import.
REPO_EXTRA = (
    "tools/converter/boot_check.py", "tools/converter/from_scratch_gate.py",
    "tools/converter/make_package.py", "tools/converter/package_check.py",
    "tools/converter/package_manifest.txt",
    "tools/converter/package_closure.py", "tools/converter/export_repo.py",
    "tools/converter/test_converter_core.py", "tools/converter/rom_bytes_scan.py",
    "tools/converter/local_music.py", "tools/converter/public_layout.py",
    "tools/converter/test_public_layout.py", "tools/build/test_title_controls.py",
    "tools/converter/test_nes_music.py",
    "tools/converter/test_nes_music_runtime.py",
    "tools/converter/test_uw_music_contract.py",
    "tools/converter/test_package_closure.py",
    "tools/emu/gpgx.py", "tools/emu/build_gpgx.sh", "tools/emu/preset_run.py",
    "tools/emu/nes_preset_run.py", "tools/emu/scenario.py",
    "tools/lockstep/presets.py", "tools/lockstep/gate.py",
    "tools/lockstep/presets/newgame.json",
)
ROOT_FILES = {"README.md": "release/README.md", "LICENSE": "release/LICENSE",
              "NOTICE": "release/NOTICE", ".gitignore": "release/gitignore",
              ".gitattributes": "release/gitattributes"}
_SNAPSHOT: dict[str, bytes] | None = None
SGDK_URL = "https://github.com/Stephane-D/SGDK"


def repo_files() -> dict[str, Path]:
    """target path -> source file."""
    out = {f: ROOT / f for f in make_package.package_files(require_sgdk=False)
           if not f.startswith("sgdk/")}
    out.update({f: ROOT / f for f in REPO_EXTRA})
    out.update({dst: ROOT / src if (ROOT / src).is_file() else ROOT / dst
                for dst, src in ROOT_FILES.items()})
    if not public_layout.is_public(ROOT):
        mapped = {public_layout.rename(dst): src for dst, src in out.items()}
        if len(mapped) != len(out):
            raise ValueError("public layout contains colliding paths")
        return mapped
    return out


def export_bytes(src: Path) -> bytes:
    data = src.read_bytes() if _SNAPSHOT is None else _SNAPSHOT[src.relative_to(ROOT).as_posix()]
    return data if public_layout.is_public(ROOT) or src.name in ("public_layout.py", "test_public_layout.py") else public_layout.rewrite(data)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("target", type=Path)
    ap.add_argument("--copy-sgdk", action="store_true")
    ap.add_argument("--source-ref", help="export this committed Git tree, excluding concurrent working edits")
    a = ap.parse_args()
    if a.target.exists() and any(a.target.iterdir()):
        print(f"ERROR: {a.target} is not empty", file=sys.stderr)
        return 1
    files = repo_files()
    if a.source_ref:
        global _SNAPSHOT
        paths = sorted({src.relative_to(ROOT).as_posix() for src in files.values()})
        archive = subprocess.check_output(["git", "-C", str(ROOT), "archive",
                                           "--format=tar", a.source_ref, "--", *paths])
        with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
            _SNAPSHOT = {member.name: tree.extractfile(member).read()
                         for member in tree.getmembers() if member.isfile()}
        missing = set(paths) - _SNAPSHOT.keys()
        if missing:
            raise ValueError(f"source ref lacks selected files: {sorted(missing)}")
    bad = []
    for dst, src in sorted(files.items()):
        if not src.is_file():
            bad.append(f"{dst} (missing source {src.relative_to(ROOT)})")
            continue
        data = export_bytes(src)
        if is_banned(src.relative_to(ROOT).as_posix())[0]:
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
        out.write_bytes(export_bytes(src))
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
