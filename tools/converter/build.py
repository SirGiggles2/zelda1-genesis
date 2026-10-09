"""Phase 17.2 — Unified builder UX shell.

Wraps the existing extractors + Build.bat into a single drag-drop-style
entry point.

Usage:
    python tools/converter/build.py <user-rom.nes>
        — full pipeline: validate ROM hash, run extractors, build
          Zelda.md.
    python tools/converter/build.py <user-rom.nes> --skip-extract
        — assume extractors have already run, just build.
    python tools/converter/build.py <user-rom.nes> --extract-only
        — validate the ROM and regenerate every ROM-derived input; no
          toolchain needed (tools/converter/regen_audit.py runs this).
    python tools/converter/build.py --check-toolchain
        — verify gcc/objcopy/python availability.

Exit codes:
    0  Build succeeded; final ROM at builds/Zelda.md.
    1  ROM validation failure (wrong hash) or extractor failure.
    2  Toolchain missing.
    3  Build failure (Build.bat returned non-zero).
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

# Supported input ROMs, pinned here rather than read from an extractor's
# output manifest: a clean package has no generated files yet, so the old
# lookup (data/audio/MANIFEST.json) found nothing and skipped validation.
SUPPORTED_ROMS = {
    "8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac":
        "The Legend of Zelda (USA) PRG0, iNES",
}
# The Redux option's assets come from the user's Zelda Redux ROM (same pin
# as engine/data/rom_inputs.lock); nothing Redux-derived ships.
SUPPORTED_REDUX_ROMS = {
    # Release v3.3.3 patches/Zelda1_Redux.ips applied to the supported ROM.
    "f3df57365d6f04e7a8947a2393b46c7e94dc971d99146f2d4c69e6047a23ed21":
        "The Legend of Zelda Redux v3.3.3, iNES",
}


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def check_toolchain() -> int:
    """Verify gcc/objcopy/python availability."""
    gcc = ROOT / "sgdk" / "bin" / "gcc.exe"
    objcopy = ROOT / "sgdk" / "bin" / "objcopy.exe"
    issues = []
    if not gcc.exists():
        issues.append(f"missing: {gcc}")
    if not objcopy.exists():
        issues.append(f"missing: {objcopy}")
    libmd = ROOT / "sgdk" / "lib" / "libmd.a"
    if not libmd.exists():
        issues.append(f"missing: {libmd} (the package's SGDK folder is incomplete)")
    if issues:
        for i in issues:
            print(f"  {i}", file=sys.stderr)
        print("The converter package is incomplete: extract the whole package again.",
              file=sys.stderr)
        return 2
    print(f"  gcc:     {gcc.name}")
    print(f"  objcopy: {objcopy.name}")
    print("Toolchain ok.")
    return 0


def validate_rom(rom_path: Path, supported: dict | None = None) -> int:
    """Check the ROM's sha256 against the pinned list."""
    supported = SUPPORTED_ROMS if supported is None else supported
    if not rom_path.exists():
        print(f"ERROR: ROM not found: {rom_path}", file=sys.stderr)
        return 1
    h = sha256_file(rom_path)
    if h not in supported:
        print("ERROR: unsupported ROM.", file=sys.stderr)
        print(f"  expected one of: {', '.join(supported)}", file=sys.stderr)
        print(f"  got:             {h}", file=sys.stderr)
        print(f"Supply {' or '.join(supported.values())}.", file=sys.stderr)
        return 1
    print(f"ROM ok: {rom_path.name} ({supported[h]})")
    return 0


# How each extractor receives the ROM. Probed from the sources, not
# assumed — the conventions are NOT uniform, and the previous code passed
# "--rom" to all of them. Only extract_nes_banks.py accepts that flag;
# the argparse-less scripts silently ignored it and fell back to their own
# lookup, so the user's ROM was never actually reaching them.
#
#   "env"  — reads ZELDA_NES_ROM, falls back to a repo-root path
#   "argv" — takes the ROM as sys.argv[1]
#   "flag" — takes --rom <path>
#   "none" — needs no ROM (reads the restored disassembly reference tree)
# Third element: extra flags the extractor needs to emit everything the build
# consumes. --legacy-inc is NOT optional for a from-scratch build: it is what
# writes src/data/music_blob.{dat,inc} (included by src/audio_driver.asm) and
# the reference/aldonunez/dat/*.dat sidecars. Without it the gate deletes
# music_blob.dat during its cache wipe and nothing regenerates it, so the
# assembler fails with "file not found: src/data/music_blob.dat".
EXTRACTORS: tuple[tuple[str, str, tuple[str, ...]], ...] = (
    # PRG banks first: later extractors read the dat sidecars it emits.
    ("extract_nes_banks.py", "flag", ()),
    ("extract_dat_sidecars.py", "env", ()),
    ("extract_chr.py", "env", ("--legacy-inc",)),
    ("extract_rooms.py", "env", ("--legacy-inc",)),
    ("extract_enemies.py", "env", ("--legacy-inc",)),
    ("extract_audio.py", "env", ("--legacy-inc",)),
    ("extract_dmc_samples.py", "argv", ()),
    ("extract_frontend.py", "env", ("--legacy-inc",)),
    ("extract_misc.py", "env", ("--legacy-inc",)),
    ("extract_demo_text.py", "env", ()),
    ("extract_intro_assets.py", "none", ()),
)

# Extractors that need ROM-derived state and run inside nes_stages()
# instead of EXTRACTORS (extractor_coverage_gate.py counts them as wired).
STAGE_EXTRACTORS: tuple[str, ...] = ("extract_fs_assets.py",)

# None left: every extractor runs unattended.
MANUAL_EXTRACTORS: tuple[tuple[str, str], ...] = ()


# Generators that derive build inputs from extractor output or from the
# committed disassembly (no ROM argument). Run after EXTRACTORS, in order.
GENERATORS: tuple[str, ...] = (
    "tools/audio/synth_square_sfx.py",
    "tools/audio/synth_noise_sfx.py",
    "tools/parity/cave_golden/synth_stairs_sfx.py",
    # UW room metadata tables from data/rooms/dungeons.c (+ room lists).
    "tools/levelinfo_start_rooms_gen.py",
    "tools/uw_expected_doors_gen.py",
    "tools/uw_dark_rooms_gen.py",
    "tools/uw_item_rooms_gen.py",
    "tools/uw_cellar_pairs_gen.py",
    "tools/converter/gen_uw_collision_c.py",
    "tools/gen_ow_bg_palram_table.py",
)

# Third-party modules the extractors import. Versions are pinned in
# tools/converter/requirements.txt (resampling output depends on them).
REQUIRED_MODULES = ("numpy", "scipy", "py65")


def check_python_deps() -> int:
    missing = []
    for mod in REQUIRED_MODULES:
        try:
            __import__(mod)
        except ImportError:
            missing.append(mod)
    if missing:
        print(f"ERROR: missing Python modules: {', '.join(missing)}", file=sys.stderr)
        print(f"  install: {sys.executable} -m pip install -r "
              f"{ROOT / 'tools' / 'converter' / 'requirements.txt'}", file=sys.stderr)
        return 2
    return 0


# Zelda Redux (ShadowOne333, GPL-3.0) is not part of this package. The user
# downloads it and passes the patch (--redux-patch: the .ips, or a .zip
# containing it) or an already patched ROM (--redux). The patched result
# must hash to SUPPORTED_REDUX_ROMS. The patch that does is patches/
# Zelda1_Redux.ips at the repository's v3.3.3 tag (REDUX_PATCH_SHA256); a
# user's release-page download did not (2026-10-09), so point to the file.
REDUX_DOWNLOAD = "https://raw.githubusercontent.com/ShadowOne333/The-Legend-of-Zelda-Redux/v3.3.3/patches/Zelda1_Redux.ips"
REDUX_PATCH_SHA256 = "97703146e386c07c440493aaaa3259b97f9128adb78f0718a4fee727094ea171"
# Known look-alikes: same name, different file (user report 2026-10-09).
REDUX_WRONG_PATCHES = {
    "04baf0e9834ed5470b719c09d2ce35baf38eb2b281f6326d964c41ff1e4cdb06":
        "the Zelda1_Redux.ips from the Redux release page",
}


def make_redux_rom(rom_path: Path, patch_path: Path) -> Path:
    """Apply the user's downloaded Redux patch to their ROM."""
    import zipfile  # noqa: PLC0415
    sys.path.insert(0, str(ROOT / "tools" / "converter"))
    from ips import apply_patch  # noqa: PLC0415
    rom = rom_path.read_bytes()
    if zipfile.is_zipfile(patch_path):
        with zipfile.ZipFile(patch_path) as z:
            patches = [z.read(n) for n in z.namelist()
                       if n.lower().endswith((".ips", ".bps"))]
    else:
        patches = [patch_path.read_bytes()]
    # A release archive can hold optional add-on patches too: keep the one
    # whose output is the pinned Redux ROM.
    for patch in patches:
        try:
            redux = apply_patch(rom, patch)
        except ValueError:
            continue
        if hashlib.sha256(redux).hexdigest() in SUPPORTED_REDUX_ROMS:
            out = ROOT / "build" / "redux" / "Zelda Redux.nes"
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(redux)
            print(f"Redux ROM built from {patch_path.name}")
            return out
    got = hashlib.sha256(patch_path.read_bytes()).hexdigest()
    known = (f"  This is {REDUX_WRONG_PATCHES[got]}, which differs from the v3.3.3 source patch.\n"
             if got in REDUX_WRONG_PATCHES else "")
    raise SystemExit(
        f"ERROR: {patch_path.name} is not the Zelda Redux v3.3.3 patch this port is built from.\n"
        f"{known}"
        f"  your file:  sha256 {got}\n"
        f"  needed:     Zelda1_Redux.ips, sha256 {REDUX_PATCH_SHA256}\n"
        f"  download it here: {REDUX_DOWNLOAD}")


def nes_stages(rom_path: Path, redux_path: Path | None) -> list[list[str]]:
    """Stages that take the ROM paths: the sprite atlas (PRG pattern blocks,
    item CHR manifest from its template) and the inputs read from what the
    NES draws (tools/nesemu runs the user's ROMs offline; replaces the
    BizHawk captures)."""
    roms = ["--rom", str(rom_path)] + (["--redux", str(redux_path)] if redux_path else [])
    stages = [
        ["engine/tools/extract_z1_prg_chr.py"],
        ["tools/converter/item_chr_manifest.py", *roms],
        ["engine/tools/gen_atlas.py"],
        ["tools/nesemu/zelda_uw_dump.py", "--rom", str(rom_path), "--rom-id", "orig"],
    ]
    if redux_path is not None:
        r = str(redux_path)
        stages += [
            # Redux overworld layouts/heaps/CHR located by hash in the ROM.
            ["tools/converter/rom_blobs.py", "--redux", r],
            ["engine/tools/gen_redux_assets.py"],
            # Redux UW background patterns and file-select screen.
            ["tools/nesemu/zelda_chr_dump.py", "--rom", r,
             "--out", "engine/out/nes_uw_chr_redux.bin"],
            ["engine/tools/gen_redux_uw_bg.py"],
            ["tools/nesemu/zelda_fs_dump.py", "--rom", r],
            ["tools/extract_fs_assets.py"],
            ["tools/nesemu/zelda_uw_dump.py", "--rom", r, "--rom-id", "redux"],
        ]
    stages += [
        ["engine/tools/uw_aggregate.py"],
        ["engine/tools/gen_uw_blob.py"],
        ["engine/tools/gen_bg_sparse.py"],
    ]
    return stages


def plan_steps(rom_path: Path, redux_path: Path | None) -> list[tuple[str, list[str]]]:
    """Every regeneration step in order, as (label, argv)."""
    # The disassembly's bank files first: the extractors parse them. Rebuilt
    # from the ROM into reference/aldonunez/ (the package ships only holes).
    steps: list[tuple[str, list[str]]] = [
        ("disasm_template.py", [sys.executable, str(ROOT / "tools" / "converter" / "disasm_template.py"),
                                "restore", "--rom", str(rom_path)]),
        ("music from your ROM", [sys.executable, str(ROOT / "tools/converter/nes_music.py"),
                                 "--rom", str(rom_path)])]
    for name, convention, extra in EXTRACTORS:
        ext = str(ROOT / "tools" / name)
        if convention == "flag":
            argv = [sys.executable, ext, "--rom", str(rom_path)]
        elif convention == "argv":
            argv = [sys.executable, ext, str(rom_path)]
        else:  # "env" and "none" both take no ROM argument
            argv = [sys.executable, ext]
        steps.append((name, argv + list(extra)))
    for rel in GENERATORS:
        steps.append((Path(rel).name, [sys.executable, str(ROOT / rel)]))
    for argv in nes_stages(rom_path, redux_path):
        steps.append((Path(argv[0]).name, [sys.executable, str(ROOT / argv[0]), *argv[1:]]))
    steps += [
        ("generated asset catalog", [sys.executable, str(ROOT / "tools/atlas/gen_sprite_catalog.py")]),
        ("record generated freshness", [sys.executable, str(ROOT / "tools/probes/check_generated_freshness.py"), "--gen"]),
        ("verify generated freshness", [sys.executable, str(ROOT / "tools/probes/check_generated_freshness.py")]),
    ]
    return steps


def progress(i: int, n: int, label: str) -> None:
    """Machine-readable progress line (read by tools/converter/gui.py)."""
    print(f"@@step {i}/{n} {label}", flush=True)


def run_extractors(rom_path: Path, redux_path: Path,
                   total_extra: int = 0) -> int:
    """Regenerate every ROM-derived asset from the user's NES ROM.

    Sets ZELDA_NES_ROM for the whole subprocess environment so the
    env-convention extractors see the ROM the user actually supplied
    rather than whatever happens to sit at the repo-root fallback path.
    total_extra: steps the caller runs afterwards (for progress totals).
    """
    env = dict(os.environ)
    env["ZELDA_NES_ROM"] = str(rom_path)
    steps = plan_steps(rom_path, redux_path)
    n = len(steps) + total_extra
    failures: list[str] = []
    for i, (label, argv) in enumerate(steps, 1):
        progress(i, n, label)
        # Never record a fresh baseline after a producer failed.
        if failures and label in ("generated asset catalog", "record generated freshness", "verify generated freshness"):
            print(f"  SKIPPED: {label}: extraction failed", file=sys.stderr)
            continue
        if not Path(argv[1]).exists():
            print(f"  MISSING: {argv[1]}", file=sys.stderr)
            failures.append(f"{label} (missing)")
            continue
        r = subprocess.run(argv, cwd=ROOT, env=env, check=False)
        if r.returncode != 0:
            print(f"  {label} exited {r.returncode}", file=sys.stderr, flush=True)
            failures.append(f"{label} (exit {r.returncode})")
    if failures:
        print(f"\n{len(failures)} step(s) failed:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    return 0


def build_rom(step: tuple[int, int] | None = None, music: Path | None = None) -> int:
    """Compile builds/Zelda.md (tools/build/build_rom.py, which Build.bat
    wraps; called directly so paths with spaces need no shell quoting).
    music: a folder of the user's own VGMs (tools/converter/local_music.py)."""
    env = dict(os.environ)
    env["ZELDA_NES_MUSIC"] = "1"
    env.pop("ZELDA_LOCAL_MUSIC", None)
    if music is not None:
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "converter" / "local_music.py"),
                            str(music)], cwd=ROOT, check=False)
        if r.returncode:
            return 1
        env["ZELDA_LOCAL_MUSIC"] = "1"
    if step:
        progress(*step, "build Genesis ROM")
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "build" / "build_rom.py")],
                       cwd=ROOT, env=env, check=False)
    if r.returncode != 0:
        print(f"ERROR: Genesis build failed (exit {r.returncode})", file=sys.stderr)
        return 3
    print(f"  output: {ROOT / 'builds' / 'Zelda.md'}")
    return 0


def deliver(output: Path, inputs: list[Path]) -> int:
    """Copy builds/Zelda.md to the user's chosen output path."""
    import shutil  # noqa: PLC0415
    src = ROOT / "builds" / "Zelda.md"
    out = output.resolve()
    if any(out == p.resolve() for p in inputs if p):
        print("ERROR: output path is one of the input files", file=sys.stderr)
        return 1
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, out)
    print(f"@@done {out} sha256={sha256_file(out)}", flush=True)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(prog="build.py")
    ap.add_argument("rom", type=Path, nargs="?",
                    help="path to user NES Zelda 1 ROM")
    ap.add_argument("--skip-extract", action="store_true",
                    help="assume extractors already ran")
    ap.add_argument("--extract-only", action="store_true",
                    help="validate the ROM and regenerate inputs; do not build")
    ap.add_argument("--redux-patch", type=Path,
                    help="Zelda Redux patch downloaded by the user (.ips or release .zip); "
                         f"get it from {REDUX_DOWNLOAD}")
    ap.add_argument("--redux", type=Path,
                    help="an already patched Zelda Redux ROM (instead of --redux-patch)")
    ap.add_argument("--check-toolchain", action="store_true",
                    help="verify gcc/objcopy availability")
    ap.add_argument("--music", type=Path,
                    help="folder of your own .vgm files (title, item, level9, ganon, triforce, "
                         "zelda, ending, overworld, underworld) to use instead of the port's "
                         "music; local builds only")
    ap.add_argument("--output", type=Path,
                    help="where to write the Genesis ROM (default: builds/Zelda.md only)")
    args = ap.parse_args()
    # Extractors run from the repository root. Resolve user paths before
    # changing working directory, including ROM/output paths with spaces.
    for name in ("rom", "redux", "redux_patch", "music", "output"):
        value = getattr(args, name)
        if value is not None:
            setattr(args, name, value.resolve())

    if args.check_toolchain:
        return check_toolchain()

    if not args.rom:
        ap.print_help()
        return 1

    if args.redux is not None and validate_rom(args.redux, SUPPORTED_REDUX_ROMS) != 0:
        return 1
    # Redux is part of the game (file select, Redux overworld/dungeons):
    # reject a missing Redux input before any step runs.
    if args.redux is None and args.redux_patch is None and not args.skip_extract:
        print("ERROR: Zelda Redux is required. Download it from "
              f"{REDUX_DOWNLOAD} and pass --redux-patch <Zelda1_Redux.ips>", file=sys.stderr)
        return 1
    if args.redux_patch is not None and not args.redux_patch.is_file():
        print(f"ERROR: Redux patch not found: {args.redux_patch}", file=sys.stderr)
        return 1
    if args.redux is None and args.redux_patch is not None:
        if validate_rom(args.rom) != 0:
            return 1
        args.redux = make_redux_rom(args.rom, args.redux_patch)
    if args.extract_only:
        if (rc := validate_rom(args.rom)) != 0:
            return rc
        if (rc := check_python_deps()) != 0:
            return rc
        return run_extractors(args.rom, args.redux)

    if (rc := check_toolchain()) != 0:
        return rc
    if (rc := check_python_deps()) != 0:
        return rc
    if (rc := validate_rom(args.rom)) != 0:
        return rc
    n = len(plan_steps(args.rom, args.redux)) + 1
    if not args.skip_extract:
        if (rc := run_extractors(args.rom, args.redux, total_extra=1)) != 0:
            return rc
    else:
        # A developer skip still uses this user's ROM, never a stale song table.
        if subprocess.run([sys.executable, str(ROOT / "tools/converter/nes_music.py"),
                           "--rom", str(args.rom)], cwd=ROOT).returncode:
            return 1
    if args.music is not None and not args.music.is_dir():
        print(f"ERROR: music folder not found: {args.music}", file=sys.stderr)
        return 1
    if (rc := build_rom((n, n), args.music)) != 0:
        return rc
    if args.output:
        return deliver(args.output, [args.rom, args.redux_patch, args.redux])
    return 0


if __name__ == "__main__":
    sys.exit(main())
