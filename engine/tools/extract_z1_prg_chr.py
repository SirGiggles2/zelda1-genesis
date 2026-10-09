#!/usr/bin/env python3
"""engine atlas Phase 1b: extract PRG pattern blocks from Z1 NES ROM.

For each pattern-block label discovered by walking .INCBIN directives in
reference/aldonunez/Z_*.asm, loads the corresponding .dat file, searches
PRG ROM for the byte sequence, and if found uniquely:

  - Writes engine/out/prg_blocks/orig/<label>.bin  (copy of bytes)
  - Adds {label, offset, length, sha256} to an index JSON

Orig variant only. Redux extraction follows once Redux has its own
disasm .dat files.

ROM is read from the repo root (same parent as the main worktree).
Pinned SHA-256 is enforced via engine/data/rom_inputs.lock.

Spec: docs/superpowers/specs/2026-05-01-roomrom-z1-full-atlas-design.md
      Section 5.2 (extract_z1_prg_chr.py) + Section 10 Phase 1b.

Usage: python engine/tools/extract_z1_prg_chr.py
Exit:  0 if >= 1 block extracted; 1 if none extracted or ROM check fails.
"""

from __future__ import annotations

import hashlib
import json
import re
import os
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[2]
DISASM_DIR = ROOT / "reference" / "aldonunez"
DAT_DIR = DISASM_DIR / "dat"
LOCK_FILE = ROOT / "engine" / "data" / "rom_inputs.lock"
OUT_BASE = ROOT / "engine" / "out" / "prg_blocks"
INES_HEADER_SIZE = 16

# Only orig for Phase 1b; Redux has no .dat files yet.
ORIG_ROM_NAME = "Legend of Zelda, The (USA).nes"


# ---------------------------------------------------------------------------
# Lock file
# ---------------------------------------------------------------------------

def parse_lock(path: Path) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        toks = s.split()
        if len(toks) < 4:
            sys.exit(f"malformed lock line: {line!r}")
        out[toks[0]] = toks[1]
    return out


# ---------------------------------------------------------------------------
# INCBIN walker — scan Z_*.asm files for .INCBIN "dat/<label>.dat"
# ---------------------------------------------------------------------------

INCBIN_RE = re.compile(r'\.INCBIN\s+"dat/([^"]+)\.dat"')
LABEL_BEFORE_INCBIN_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")


def walk_incbin_labels(disasm_dir: Path) -> List[Tuple[str, str, int]]:
    """Return list of (label, asm_file_name, line_number) for all .INCBIN dat/*.dat references.

    The label is the identifier on the line immediately preceding the .INCBIN
    (the standard pattern in Z_*.asm). If no label precedes it, uses the
    dat filename stem as the label.
    """
    results: List[Tuple[str, str, int]] = []
    seen_labels: set[str] = set()
    asm_files = sorted(disasm_dir.glob("Z_*.asm"))
    for asm_path in asm_files:
        lines = asm_path.read_text(encoding="utf-8", errors="replace").splitlines()
        prev_label: Optional[str] = None
        for lineno, line in enumerate(lines, start=1):
            stripped = line.strip()
            # Track the last seen label
            lm = LABEL_BEFORE_INCBIN_RE.match(stripped)
            if lm:
                prev_label = lm.group(1)
            # Match .INCBIN
            m = INCBIN_RE.search(stripped)
            if m:
                dat_stem = m.group(1)
                label = prev_label if prev_label else dat_stem
                if label not in seen_labels:
                    results.append((label, asm_path.name, lineno))
                    seen_labels.add(label)
    return results


# ---------------------------------------------------------------------------
# PRG search
# ---------------------------------------------------------------------------

def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def find_block_in_prg(prg_bytes: bytes, needle: bytes) -> int:
    """Return PRG offset of needle (unique match) or -1 (not found) / -2 (ambiguous)."""
    first = prg_bytes.find(needle)
    if first < 0:
        return -1
    second = prg_bytes.find(needle, first + 1)
    if second >= 0:
        return -2
    return first


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    if not LOCK_FILE.exists():
        sys.exit(f"lock file missing: {LOCK_FILE}")
    lock = parse_lock(LOCK_FILE)

    # Find ROM: the builder passes the user's ROM in ZELDA_NES_ROM.
    env_rom = os.environ.get("ZELDA_NES_ROM")
    rom_path = Path(env_rom) if env_rom else ROOT / ORIG_ROM_NAME
    if not env_rom and not rom_path.exists():
        # Try main worktree sibling
        alt = ROOT.parent / "FINAL TRY" / ORIG_ROM_NAME
        if alt.exists():
            rom_path = alt
    if not rom_path.exists():
        sys.exit(f"orig ROM not found at {rom_path}")

    rom_bytes = rom_path.read_bytes()
    rom_sha = sha256_hex(rom_bytes)
    locked_sha = lock.get("orig")
    if locked_sha and rom_sha != locked_sha:
        sys.exit(
            f"ROM SHA-256 mismatch with lock file.\n"
            f"  lock: {locked_sha}\n"
            f"  rom:  {rom_sha}"
        )
    prg_bytes = rom_bytes[INES_HEADER_SIZE:]
    print(f"ROM:  {rom_path.name}")
    print(f"SHA:  {rom_sha}")
    print(f"PRG:  {len(prg_bytes)} bytes (iNES header skipped)")
    print()

    # Walk INCBIN labels
    incbin_entries = walk_incbin_labels(DISASM_DIR)
    print(f"Found {len(incbin_entries)} .INCBIN dat/*.dat label(s) across Z_*.asm files:")
    for label, asm_file, lineno in incbin_entries:
        print(f"  {label}  ({asm_file}:{lineno})")
    print()

    # Extract each block
    out_dir = OUT_BASE / "orig"
    out_dir.mkdir(parents=True, exist_ok=True)

    index: List[dict] = []
    extracted = 0
    skipped = 0
    missing_dat = 0

    for label, asm_file, lineno in incbin_entries:
        dat_path = DAT_DIR / f"{label}.dat"
        if not dat_path.exists():
            # The label may differ from the dat filename; try to find it from asm context
            # For now, skip with a warning
            print(f"  SKIP (no .dat): {label}  ({asm_file}:{lineno})")
            missing_dat += 1
            continue

        needle = dat_path.read_bytes()
        offset = find_block_in_prg(prg_bytes, needle)
        if offset == -1:
            print(
                f"  SKIP (not in PRG): {label}  ({dat_path.name}, {len(needle)} bytes)\n"
                f"    -> Block not found byte-identical in PRG. Z1 runtime transfer\n"
                f"       may transform bytes before CHR RAM load, OR block layout is\n"
                f"       non-contiguous. verify_chr_live.py will disambiguate."
            )
            skipped += 1
            continue
        if offset == -2:
            print(
                f"  SKIP (ambiguous): {label}  ({dat_path.name}, {len(needle)} bytes)\n"
                f"    -> Block appears >1 time in PRG. Use disasm-located offset, not\n"
                f"       byte search."
            )
            skipped += 1
            continue

        # Found uniquely
        block_sha = sha256_hex(needle)
        out_bin = out_dir / f"{label}.bin"
        out_bin.write_bytes(needle)
        entry = {
            "label": label,
            "asm_file": asm_file,
            "asm_line": lineno,
            "dat_file": dat_path.name,
            "prg_offset": offset,
            "file_offset": offset + INES_HEADER_SIZE,
            "length": len(needle),
            "sha256": block_sha,
            "out_bin": out_bin.relative_to(ROOT).as_posix(),
        }
        index.append(entry)
        print(
            f"  OK: {label}  PRG offset=0x{offset:06X}  "
            f"len={len(needle)}  sha256={block_sha[:16]}..."
        )
        extracted += 1

    print()
    print(f"Extracted: {extracted}  Skipped: {skipped}  Missing .dat: {missing_dat}")

    # Write index JSON
    index_path = OUT_BASE / "orig_index.json"
    index_doc = {
        "schema_version": 1,
        "generator": "engine/tools/extract_z1_prg_chr.py",
        "variant": "orig",
        "rom_name": ORIG_ROM_NAME,
        "rom_sha256": rom_sha,
        "ines_header_size": INES_HEADER_SIZE,
        "notes": [
            "prg_offset is relative to start of PRG ROM (after 16-byte iNES header).",
            "file_offset is absolute byte offset in the .nes file.",
            "Blocks found uniquely in PRG byte-for-byte against the disasm .dat files.",
            "Blocks not found: Z1 runtime transfer may transform bytes pre-CHR-RAM load.",
            "Redux variant extraction deferred (no Redux-specific .dat files yet).",
        ],
        "blocks": index,
    }
    # Force-create parent (gitignored, but we committed with -f in P1a)
    index_path.parent.mkdir(parents=True, exist_ok=True)
    index_path.write_text(
        json.dumps(index_doc, indent=2, ensure_ascii=True) + "\n",
        encoding="ascii",
    )
    print(f"Index: {index_path}")

    if extracted == 0:
        print("ERROR: no blocks extracted", file=sys.stderr)
        return 1
    print(f"\nextract_z1_prg_chr: OK ({extracted} block(s) extracted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
