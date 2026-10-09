"""From-scratch gate: convert from the release package, twice, and compare.

1. Build (or take) the release zip (tools/converter/make_package.py).
2. Unpack it into a clean directory whose path contains spaces (A).
3. Rejections first: a corrupted copy of the ROM, a missing Redux patch
   and a truncated Redux patch must each fail before any conversion step,
   with no output.
4. Run the package's build.py with the user's ROM and Redux download.
   Full build where the Genesis toolchain runs (Windows, or Wine
   elsewhere): the output ROM is the result. --extract-only: the
   regenerated build inputs are the result.
5. Repeat in a second clean directory (B); results must be byte-identical.
6. The user's files must be unchanged. In extract-only mode every
   regenerated file must also equal the committed copy.
7. Full mode: the output ROM must reach gameplay (boot_check.py, headless
   Genesis Plus GX; skipped with a note when the core is not built).

Usage:
    python tools/converter/from_scratch_gate.py --rom <zelda.nes> --redux-patch <redux zip/ips>
        [--package <zip>] [--extract-only] [--keep]
Exit: 0 PASS; 1 bad input; 2 a conversion failed; 3 A != B; 4 differs from
the committed tree; 5 an input file changed; 6 a bad input was accepted;
7 the output ROM did not reach gameplay.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "converter"))
from package_check import is_banned  # noqa: E402
import make_package  # noqa: E402

REPORT = ROOT / "docs" / "audit" / "from_scratch_status.md"


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def generated(tree: Path) -> dict[str, str]:
    """sha256 of every game-derived file in an unpacked tree."""
    out = {}
    for p in tree.rglob("*"):
        if p.is_file() and "__pycache__" not in p.parts:
            rel = p.relative_to(tree).as_posix()
            # sgdk/ is third-party (shipped as-is), not game-derived.
            if is_banned(rel)[0] and not rel.startswith(("build/", "builds/", "sgdk/")):
                out[rel] = sha(p)
    return out


def convert(pkg: Path, workdir: Path, rom: Path, redux: Path | None,
            full: bool) -> tuple[int, Path, Path]:
    if not (workdir / make_package.PACKAGE_ROOT).is_dir():
        with zipfile.ZipFile(pkg) as z:
            z.extractall(workdir)
    tree = workdir / make_package.PACKAGE_ROOT
    out = workdir / "My Genesis ROMs" / "Zelda.md"
    argv = [sys.executable, str(tree / "tools" / "converter" / "build.py"), str(rom)]
    argv += ["--redux-patch", str(redux)] if redux else []
    argv += ["--output", str(out)] if full else ["--extract-only"]
    r = subprocess.run(argv, cwd=tree, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    (workdir / "convert.log").write_text(r.stdout + r.stderr, encoding="utf-8")
    return r.returncode, tree, out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--redux-patch", type=Path, required=True)
    ap.add_argument("--package", type=Path)
    ap.add_argument("--extract-only", action="store_true",
                    help="regenerate inputs only (no Genesis toolchain run)")
    ap.add_argument("--keep", action="store_true", help="keep the work directories")
    args = ap.parse_args()
    for p in (args.rom, args.redux_patch):
        if not p.is_file():
            print(f"ERROR: not found: {p}", file=sys.stderr)
            return 1
    inputs_before = {p: sha(p) for p in (args.rom, args.redux_patch)}
    full = not args.extract_only and (os.name == "nt" or shutil.which("wine") is not None)
    base = Path(tempfile.mkdtemp(prefix="from scratch "))
    try:
        pkg = args.package
        if pkg is None:
            pkg = base / "package.zip"
            rc = subprocess.run([sys.executable, str(ROOT / "tools/converter/make_package.py"),
                                 "--out", str(pkg)] + ([] if full else ["--allow-missing-sgdk"]),
                                check=False).returncode
            if rc:
                return 2
        # Rejections: each must fail before any step and leave no output.
        bad_rom = base / "bad input" / "Zelda bad.nes"
        bad_rom.parent.mkdir(parents=True)
        data = bytearray(args.rom.read_bytes())
        data[0x10] ^= 0xFF
        bad_rom.write_bytes(bytes(data))
        bad_patch = base / "bad input" / "Redux truncated.ips"
        bad_patch.write_bytes(args.redux_patch.read_bytes()[:len(args.redux_patch.read_bytes()) // 2]
                              if not zipfile.is_zipfile(args.redux_patch) else b"PATCH")
        rejections = (("corrupted ROM", bad_rom, args.redux_patch),
                      ("missing Redux", args.rom, None),
                      ("truncated Redux patch", args.rom, bad_patch))
        # Run in A's tree before A converts: a rejection must not write anything.
        for label, rom, redux in rejections:
            work = base / "clean env A"
            rc, _, out = convert(pkg, work, rom, redux, full)
            log = (work / "convert.log").read_text(encoding="utf-8")
            if rc != 1 or out.exists() or "@@step" in log:
                print(f"FAIL: {label} accepted or failed late (exit {rc}, output "
                      f"{'written' if out.exists() else 'none'})", file=sys.stderr)
                return 6
            print(f"reject {label}: ok (exit 1 before any step: {log.strip().splitlines()[0][:90]})")
        results = {}
        outs = {}
        for run in ("A", "B"):
            work = base / f"clean env {run}"
            rc, tree, out = convert(pkg, work, args.rom, args.redux_patch, full)
            if rc:
                print(f"run {run}: conversion failed (exit {rc}); log: {work / 'convert.log'}",
                      file=sys.stderr)
                print((work / "convert.log").read_text(encoding="utf-8")[-3000:], file=sys.stderr)
                return 2
            results[run] = {"rom": sha(out)} if full else generated(tree)
            outs[run] = out
            print(f"run {run}: ok ({'ROM ' + results[run]['rom'][:16] if full else str(len(results[run])) + ' generated files'})")
        if any(sha(p) != h for p, h in inputs_before.items()):
            print("FAIL: an input file changed", file=sys.stderr)
            return 5
        if results["A"] != results["B"]:
            diff = sorted(k for k in set(results["A"]) | set(results["B"])
                          if results["A"].get(k) != results["B"].get(k))
            print(f"FAIL: run A != run B ({diff[:10]})", file=sys.stderr)
            return 3
        mismatch = []
        if not full:
            for rel, h in results["A"].items():
                committed = ROOT / rel
                if committed.is_file() and sha(committed) != h:
                    mismatch.append(rel)
        if mismatch:
            print(f"FAIL: {len(mismatch)} regenerated files differ from the committed tree: "
                  f"{mismatch[:10]}", file=sys.stderr)
            return 4
        boot = "not run (extract-only)"
        if full:
            r = subprocess.run([sys.executable, str(ROOT / "tools/converter/boot_check.py"),
                                "--rom", str(args.rom), "--genesis", str(outs["A"])],
                               capture_output=True, text=True, check=False)
            print(r.stdout + r.stderr, end="")
            if r.returncode == 2:
                boot = "SKIPPED (no Genesis Plus GX core; tools/emu/build_gpgx.sh)"
            elif r.returncode:
                return 7
            else:
                boot = r.stdout.strip().splitlines()[-1]
        what = (f"Genesis ROM sha256 {results['A']['rom']}; {boot}" if full else
                f"{len(results['A'])} regenerated inputs, all equal to the committed tree")
        verdict = (f"from_scratch_gate: PASS ({'full build' if full else 'extract-only'}) - "
                   f"release package unpacked into clean paths with spaces; corrupted ROM, "
                   f"missing Redux and truncated Redux patch rejected before any step; runs A and B "
                   f"byte-identical; {what}; input files unchanged")
        print(verdict)
        REPORT.write_text("# from_scratch_gate\n\n> Generated by `tools/converter/from_scratch_gate.py`.\n\n"
                          f"package: {pkg.name} sha256 {sha(pkg)}\n\n"
                          f"user ROM sha256: {inputs_before[args.rom]}\n\n{verdict}\n",
                          encoding="utf-8")
        return 0
    finally:
        if not args.keep:
            shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
