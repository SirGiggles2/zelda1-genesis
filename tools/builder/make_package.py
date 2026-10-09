"""Build the release package: a zip of the converter with no game data.

Contents: the files a full conversion reads (package_manifest.txt, made by
package_closure.py), the entry points in ALWAYS, and SGDK whole (sgdk/
submodule, which bundles the m68k-elf GCC toolchain). Nothing package_check.py classifies as game-derived.
Entries are sorted and stamped with a fixed date, so the same tree gives
the same zip bytes. The zip is re-checked entry by entry before it is kept.

Usage:
    python tools/builder/make_package.py [--out builds/package/ZeldaGenesisConverter.zip]
Exit: 0 written and clean; 1 a game-derived entry or a local path was found;
2 SGDK missing.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "builder"))
from package_check import is_banned  # noqa: E402

FIXED_TIME = (2026, 1, 1, 0, 0, 0)
PACKAGE_ROOT = "ZeldaGenesisConverter"

# Candidate set: everything conversion could need. package_closure.py
# runs a traced full conversion from these and records the files actually
# read in package_manifest.txt; only those ship.
CANDIDATES = (
    "Converter.bat", "Debug.bat", "docs/CONVERTER.md",
    # Generated docs the build's freshness gate hashes (sprite_catalog spec).
    "docs/atlas/sprite_catalog.md", "docs/atlas/vram_map.md",
    "src/", "RoomRom/src/", "RoomRom/data/", "RoomRom/tools/", "RoomRom/out/",
    "tools/", "reference/aldonunez/", "reference/aldonunez_tpl/", "data/", "VGMS/",
)
# Shipped whatever the trace shows: entry points and files a Linux trace
# cannot reach (Windows launchers, the GUI, dependency install, docs).
ALWAYS = (
    "Converter.bat", "Debug.bat", "docs/CONVERTER.md",
    "tools/builder/gui.py", "tools/builder/converter_core.py",
    "tools/builder/requirements.txt",
)
MANIFEST = ROOT / "tools" / "builder" / "package_manifest.txt"
EXCLUDE_SUFFIXES = (".png", ".jpg", ".jpeg", ".gif", ".bmp", ".mp3", ".mp4",
                    ".log", ".lua", ".state", ".md~")


def tracked() -> list[str]:
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"],
                         capture_output=True, check=True).stdout.decode("utf-8")
    return [p for p in out.split("\0") if p and (ROOT / p).is_file()]


def sgdk_files(require: bool) -> list[str]:
    """SGDK (submodule) shipped whole, its bundled m68k-elf GCC included: a
    Linux trace cannot show what the Windows loader needs from it, and the
    pin check hashes the whole tree."""
    sgdk = ROOT / "sgdk"
    if require and not (sgdk / "lib" / "libmd.a").exists():
        raise SystemExit(2)
    files = [p.relative_to(ROOT).as_posix() for p in sgdk.rglob("*")
             if p.is_file() and ".git" not in p.relative_to(sgdk).parts]
    return files


def candidate_files(require_sgdk: bool = True) -> list[str]:
    files = [p for p in tracked()
             if not is_banned(p)[0]
             and (p in CANDIDATES or p.startswith(tuple(i for i in CANDIDATES if i.endswith("/"))))
             and not p.lower().endswith(EXCLUDE_SUFFIXES)]
    return sorted(set(files) | set(sgdk_files(require_sgdk)))


def manifest_files() -> list[str]:
    return [line for line in MANIFEST.read_text(encoding="utf-8").splitlines()
            if line and not line.startswith("#")]


def package_files(require_sgdk: bool = True) -> list[str]:
    files = set(manifest_files()) | set(ALWAYS)
    missing = sorted(f for f in files if not (ROOT / f).is_file())
    if missing:
        raise SystemExit(f"ERROR: manifest files missing from the tree: {missing[:10]} "
                         "(re-run tools/builder/package_closure.py)")
    return sorted(files | set(sgdk_files(require_sgdk)))


def write_zip(files: list[str], out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(".tmp")
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in files:
            info = zipfile.ZipInfo(f"{PACKAGE_ROOT}/{rel}", FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, (ROOT / rel).read_bytes())
    tmp.replace(out)


# P10.6: a shipped file must not depend on a developer's machine. Third-party
# binaries (SGDK, the m68k toolchain) carry their own build-host paths and
# are exempt.
LOCAL_PATH = re.compile(rb"[A-Za-z]:[\\/]+(?:Users|BizHawk|Zelda)\b|/home/[A-Za-z0-9_.-]+/"
                        rb"|/Users/[A-Za-z0-9_.-]+/|/tmp/cl[a]ude", re.I)
THIRD_PARTY = ("sgdk/",)


def local_paths(rel: str, data: bytes) -> list[str]:
    if rel.startswith(THIRD_PARTY):
        return []
    return sorted({m.group(0).decode("latin-1") for m in LOCAL_PATH.finditer(data)})


def check_zip(path: Path) -> list[str]:
    """Entries that are game-derived or name a developer's local path."""
    bad = []
    with zipfile.ZipFile(path) as z:
        for full in z.namelist():
            if "/" not in full:
                continue
            n = full.split("/", 1)[1]
            if is_banned(n)[0] and not n.startswith("sgdk/"):
                bad.append(f"{n} (game-derived)")
            elif hits := local_paths(n, z.read(full)):
                bad.append(f"{n} (local path {', '.join(hits)})")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path,
                    default=ROOT / "builds" / "package" / f"{PACKAGE_ROOT}.zip")
    ap.add_argument("--allow-missing-sgdk", action="store_true",
                    help="developer check only: package without sgdk/")
    args = ap.parse_args()
    try:
        files = package_files(require_sgdk=not args.allow_missing_sgdk)
    except SystemExit as e:
        if e.code != 2:
            raise
        print("ERROR: sgdk/ is not checked out (git submodule update --init sgdk)",
              file=sys.stderr)
        return 2
    write_zip(files, args.out)
    bad = check_zip(args.out)
    if bad:
        args.out.unlink()
        print(f"ERROR: {len(bad)} rejected entries, package discarded:", file=sys.stderr)
        for b in bad[:20]:
            print(f"  {b}", file=sys.stderr)
        return 1
    size = args.out.stat().st_size
    print(f"wrote {args.out} ({len(files)} files, {size / 1e6:.1f} MB); "
          f"0 game-derived entries, 0 local paths")
    return 0


if __name__ == "__main__":
    sys.exit(main())
