#!/usr/bin/env python3
"""Verify checked-in generated sources are fresh vs their inputs + gen scripts.

Per Codex parity audit (debate roomrom-default-rule, 2026-05-04 ACTION 3):
RoomRom standalone build silently accepts stale generated sources for atlas /
expanded_bg_chr / redux / palette / uw_blob / uw_collision_c. This gate
closes the staleness window.

Mechanism: each spec lists (script, inputs, outputs). All three are hashed
into a single sentinel SHA. Sentinels stored in
RoomRom/data/regen_sentinels.json. On build: re-hash, compare. If inputs or
script changed without outputs being regenerated (or vice versa), sentinel
mismatch fires and build fails.

Usage:
    python tools/probes/check_generated_freshness.py            # verify
    python tools/probes/check_generated_freshness.py --gen      # write sentinels
    python tools/probes/check_generated_freshness.py --gen --only uw_collision

Wired into tools/debug/build_debug.py as part of the Debug build.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SENTINELS_PATH = REPO_ROOT / "RoomRom" / "data" / "regen_sentinels.json"

TEXT_HASH_SUFFIXES = frozenset({
    ".asm",
    ".c",
    ".h",
    ".json",
    ".md",
    ".py",
    ".txt",
    ".yaml",
    ".yml",
})

# Each spec: name, script (one .py), inputs (list of paths or glob patterns
# rooted at REPO_ROOT), outputs (list of paths or globs).
GEN_SPECS: list[dict] = [
    {
        "name": "item_tile_atlas_idx",
        "script": "tools/atlas/gen_item_tile_atlas_idx.py",
        "inputs": ["RoomRom/data/item_chr_manifest.json"],
        "outputs": ["src/game/enemies/item_tile_atlas_idx.h"],
    },
    {
        "name": "uw_collision",
        "script": "tools/builder/gen_uw_collision_c.py",
        "inputs": [
            "data/rooms/dungeons.c",
            "data/rooms/MANIFEST.json",
            "tools/builder/extract_uw_collision.py",  # imported helper
        ],
        "outputs": [
            "RoomRom/src/uw_collision_data.c",
            "RoomRom/src/uw_collision_data.h",
        ],
    },
    {
        "name": "uw_blob",
        "script": "RoomRom/tools/gen_uw_blob.py",
        # T-080: rooms are captured from the user's ROM by the offline NES
        # (tools/nesemu), aggregated by uw_aggregate.py. Track those
        # producers and the room lists, not the untracked aggregate.
        "inputs": [
            "tools/nesemu/nes.py",
            "tools/nesemu/zelda_uw_dump.py",
            "RoomRom/tools/uw_aggregate.py",
            "RoomRom/data/uw_level*_quest*_rooms.json",
        ],
        "outputs": [
            "RoomRom/src/uw_room_blob.c",
        ],
    },
    {
        "name": "atlas",
        "script": "RoomRom/tools/gen_atlas.py",
        "inputs": [
            "RoomRom/data/atlas_master.json",
            "RoomRom/data/item_chr_manifest.json",
            "RoomRom/out/prg_blocks/orig/**/*",
        ],
        "outputs": [
            "RoomRom/src/atlas/*.c",
            "RoomRom/src/atlas/*.h",
        ],
    },
    {
        "name": "bg_palette_blob",
        "script": "RoomRom/tools/gen_bg_palette_blob.py",
        "inputs": [
            "data/misc/palettes.c",
        ],
        "outputs": [
            # Phase 12.2: bg_palette_blob output promoted to src/game/world/.
            "src/game/world/ow_palette.c",
            "src/game/world/ow_palette.h",
        ],
    },
    {
        "name": "redux_roomrom",
        "script": "RoomRom/tools/gen_redux_roomrom.py",
        # T-080: Redux sources are located in the user's Redux ROM by
        # tools/builder/rom_blobs.py from the shipped template.
        "inputs": [
            "RoomRom/data/redux_sources.template.json",
            "tools/builder/rom_blobs.py",
            "data/rooms/overworld.c",
        ],
        "outputs": [
            "RoomRom/src/redux_overworld.c",
            "RoomRom/src/redux_overworld_bg.c",
        ],
    },
    {
        "name": "redux_uw_bg",
        "script": "RoomRom/tools/gen_redux_uw_bg.py",
        # T-080: dumped from the user's Redux ROM by the offline NES.
        "inputs": [
            "tools/nesemu/nes.py",
            "tools/nesemu/zelda_chr_dump.py",
            "tools/nesemu/zelda_uw_dump.py",
        ],
        "outputs": [
            "RoomRom/src/redux_uw_bg.c",
        ],
    },
    # Phase J.2 (2026-05-18): expand_bg_chr.py retired. Legacy
    # expanded_bg_chr.{c,h} deleted; all consumers migrated to sparse
    # atlas (RoomRom/tools/gen_bg_sparse.py).
    {
        "name": "bg_sparse",
        "script": "RoomRom/tools/gen_bg_sparse.py",
        "inputs": [
            "data/chr/common.c",
            "data/chr/overworld_bg.c",
            "data/chr/underworld_bg.c",
            "RoomRom/src/redux_overworld_bg.c",
            "tools/probes/audit_per_tile_subpal.py",
            "RoomRom/src/uw_room_blob.c",
            "data/rooms/overworld.c",
        ],
        "outputs": [
            "RoomRom/src/bg_sparse_chr.c",
            "RoomRom/src/bg_sparse_chr.h",
        ],
    },
    {
        "name": "sprite_catalog",
        "script": "tools/atlas/gen_sprite_catalog.py",
        "inputs": [
            "RoomRom/src/roomrom_vram_map.h",
            "RoomRom/data/chr_atlas_master.json",
            "RoomRom/data/item_chr_manifest.json",
            "RoomRom/src/atlas/items_chr_x4.h",
            "RoomRom/src/atlas/enemy_chr.h",
            "RoomRom/src/atlas/link_chr.h",
            "RoomRom/src/atlas/hud_chr.h",
            "RoomRom/src/atlas/npc_chr.h",
            "RoomRom/src/atlas/title_chr.h",
            "RoomRom/src/atlas/fileselect_chr.h",
            "RoomRom/src/atlas/boss_chr.h",
        ],
        "outputs": [
            "docs/atlas/sprite_catalog.md",
            "docs/atlas/vram_map.md",
            "RoomRom/data/sprite_catalog.json",
        ],
    },
]


def normalize_text_line_endings(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def hash_file(path: Path) -> str:
    h = hashlib.sha256()
    if path.suffix.lower() in TEXT_HASH_SUFFIXES:
        h.update(normalize_text_line_endings(path.read_bytes()))
        return h.hexdigest()

    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def expand_paths(patterns: list[str]) -> list[Path]:
    """Resolve glob patterns under REPO_ROOT to concrete files (sorted).

    Sort key is the POSIX relative path string, not Path ordering: Windows
    Path comparison is case-insensitive, so sorted(Path) gave a different
    order (and sentinel) on Windows than on Linux for mixed-case names.
    """
    out: list[Path] = []
    for pat in patterns:
        if any(c in pat for c in "*?["):
            matches = [p for p in REPO_ROOT.glob(pat) if p.is_file()]
            matches.sort(key=lambda p: p.relative_to(REPO_ROOT).as_posix())
            out.extend(matches)
        else:
            out.append(REPO_ROOT / pat)
    return out


def compute_sentinel(spec: dict) -> tuple[str, list[str]]:
    """Hash script + inputs + outputs into one sentinel.

    Returns (sentinel_sha256, missing_paths). Missing paths are listed but
    do not crash — caller decides whether to fail.
    """
    h = hashlib.sha256()
    missing: list[str] = []

    def feed(role: str, paths: list[Path]) -> None:
        for p in paths:
            rel = p.relative_to(REPO_ROOT).as_posix() if p.is_absolute() else str(p)
            h.update(f"{role}:{rel}:".encode("utf-8"))
            if not p.exists() or not p.is_file():
                h.update(b"<missing>")
                missing.append(rel)
            else:
                h.update(hash_file(p).encode("utf-8"))
            h.update(b"\n")

    script_path = REPO_ROOT / spec["script"]
    feed("script", [script_path])
    feed("input", expand_paths(spec["inputs"]))
    feed("output", expand_paths(spec["outputs"]))
    return h.hexdigest(), missing


def load_sentinels() -> dict:
    if not SENTINELS_PATH.exists():
        return {}
    return json.loads(SENTINELS_PATH.read_text(encoding="utf-8"))


def write_sentinels(data: dict) -> None:
    SENTINELS_PATH.write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--gen", action="store_true",
                    help="write sentinels from current state")
    ap.add_argument("--only", default=None,
                    help="restrict to one spec name")
    args = ap.parse_args(argv[1:])

    selected = [s for s in GEN_SPECS if args.only is None or s["name"] == args.only]
    if not selected:
        print(f"[check_generated_freshness] no spec named {args.only!r}", file=sys.stderr)
        return 1

    if args.gen:
        sentinels = load_sentinels()
        for spec in selected:
            sha, missing = compute_sentinel(spec)
            sentinels[spec["name"]] = sha
            note = f" ({len(missing)} missing inputs)" if missing else ""
            print(f"[check_generated_freshness] gen {spec['name']}: {sha[:12]}...{note}")
        write_sentinels(sentinels)
        print(f"[check_generated_freshness] wrote {len(sentinels)} sentinels -> "
              f"{SENTINELS_PATH.relative_to(REPO_ROOT)}")
        return 0

    sentinels = load_sentinels()
    if not sentinels:
        print("[check_generated_freshness] no sentinels yet; pass "
              "(run with --gen to bootstrap)")
        return 0

    fails = []
    for spec in selected:
        actual, missing = compute_sentinel(spec)
        expected = sentinels.get(spec["name"])
        if expected is None:
            fails.append((spec["name"], "no sentinel recorded", actual))
            continue
        if expected != actual:
            fails.append((spec["name"], f"sentinel mismatch (expected {expected[:12]}..., got {actual[:12]}...)", actual))

    if not fails:
        print(f"[check_generated_freshness] OK ({len(selected)} specs match)")
        return 0

    print(f"[check_generated_freshness] FAIL ({len(fails)}/{len(selected)} specs):", file=sys.stderr)
    for name, reason, actual in fails:
        print(f"  {name}: {reason}", file=sys.stderr)
        print(f"    Re-run the gen, then: python tools/probes/check_generated_freshness.py --gen --only {name}",
              file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
