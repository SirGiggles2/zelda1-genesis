#!/usr/bin/env python3
"""Verify RoomRom item CHR manifest and renderer source-safety gates.

Default: prints NES-dispatch / manifest mismatches as warnings and exits 0.
Pass --strict to make mismatches a build failure (intended for CI gating).
Items can opt out of the dispatch check by adding an
'sprite_size_override_reason' key in the manifest item_def, citing the
NES draw routine + line that justifies the divergence.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "RoomRom" / "data" / "item_chr_manifest.json"
HEADER = ROOT / "RoomRom" / "src" / "atlas" / "items_chr_x4.h"
SOURCE = ROOT / "src" / "game" / "world" / "render" / "sprite_render.c"  # Phase 12.2 promoted

REQUIRED_DEFS = {
    "sword_vert",
    "sword_horz",
    "boomerang",
    "arrow_vert",
    "arrow_horz",
    "bomb",
    "explosion",
}

FORBIDDEN_COMMON_GUESSES = {
    "0x82u", "0x83u", "0x84u", "0x85u",
    "0x86u", "0x87u", "0x88u", "0x89u",
}

# NES dispatch path -> (expected SGDK sprite_size [W, H], NES tiles per frame).
# Derived from Anim_WriteSpecificItemSprites at reference/aldonunez/Z_01.asm:5279
# and the @Wide sub-dispatch at :5301-5306.
#   tile == 0xF3                  -> Narrow      (1 sprite, 8x8)
#   tile <  0x20                  -> Wide/Slim   (2 sprites overlap 1px, 15x8)
#   tile in [0x20, 0x62)          -> Narrow      (1 sprite, 8x8)
#   tile in [0x62, 0x6C)          -> Wide/Slim   (2 sprites, ~15x8)
#   tile in [0x6C, 0x7C)          -> Mirrored    (2 sprites, right=left hflip)
#   tile >= 0x7C  (and != 0xF3)   -> Flippable   (2 sprites, [02] and [02]+2)
DISPATCH_TABLE = {
    "Narrow":     {"size": [1, 1], "tiles_per_frame": 1},
    "Slim":       {"size": [2, 1], "tiles_per_frame": 2},
    "Mirrored":   {"size": [2, 1], "tiles_per_frame": 1},
    "Flippable":  {"size": [2, 1], "tiles_per_frame": 2},
}


def classify_nes_dispatch(tile: int) -> str:
    if tile == 0xF3:
        return "Narrow"
    if tile < 0x20:
        if tile < 0x6C:
            return "Slim"
        if tile < 0x7C:
            return "Mirrored"
        return "Flippable"
    if tile < 0x62:
        return "Narrow"
    if tile < 0x6C:
        return "Slim"
    if tile < 0x7C:
        return "Mirrored"
    return "Flippable"


def fail(msg: str) -> None:
    raise SystemExit(f"FAIL: {msg}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="fail on NES-dispatch / manifest mismatches "
                             "(default: warn + exit 0)")
    args = parser.parse_args()

    if not MANIFEST.exists():
        fail(f"missing manifest: {MANIFEST}")
    manifest = json.loads(MANIFEST.read_text(encoding="ascii"))
    if manifest.get("schema_version") != 1:
        fail("unsupported manifest schema")
    if manifest.get("source_policy") != "live_nes_chr_only":
        fail("manifest source_policy must be live_nes_chr_only")

    defs = manifest.get("item_defs", [])
    names = {d.get("name") for d in defs}
    missing = sorted(REQUIRED_DEFS - names)
    if missing:
        fail(f"missing item defs: {', '.join(missing)}")

    total_tiles = 0
    dispatch_warnings: list[str] = []
    for item_def in defs:
        name = item_def.get("name")
        tile_ids = item_def.get("tile_ids", [])
        if not tile_ids:
            fail(f"{name} has no tile_ids")
        # Generator emits Genesis tiles per declared NES tile depending on
        # draw_rule: mirrored_* -> raw + hflipped (2 per tile); 8x16 mirrored
        # 16x16 (e.g. explosion) -> 4 per pair (LT, LB, RT, RB);
        # default -> 1 per tile.
        rule = str(item_def.get("draw_rule", ""))
        if rule.startswith("wide_16x16_mirrored_8x16"):
            per_input = 2  # 6 ids -> 12 tiles
        elif rule == "wide_16x16_pair":
            per_input = 1  # 4 ids -> 4 tiles (LT, LB, RT, RB)
        elif rule.startswith("mirrored_"):
            per_input = 2
        else:
            per_input = 1
        total_tiles += len(tile_ids) * per_input

        nes_frame_tile = item_def.get("nes_frame_tile")
        if not nes_frame_tile:
            fail(f"{name} missing nes_frame_tile (need it to classify dispatch)")
        try:
            tile_int = int(nes_frame_tile, 16)
        except (TypeError, ValueError):
            fail(f"{name} nes_frame_tile {nes_frame_tile!r} is not hex")

        dispatch = classify_nes_dispatch(tile_int)
        spec = DISPATCH_TABLE[dispatch]
        override_reason = item_def.get("sprite_size_override_reason")
        manifest_size = item_def.get("sprite_size")
        if manifest_size != spec["size"] and not override_reason:
            dispatch_warnings.append(
                f"{name}: nes_frame_tile {nes_frame_tile} -> {dispatch} dispatch "
                f"(NES sprite size {spec['size']}); manifest sprite_size "
                f"{manifest_size} disagrees. If renderer intentionally diverges, "
                f"add an override key 'sprite_size_override_reason' citing the "
                f"NES draw routine + line."
            )

        # Flag over-extraction: tile_ids count must equal a whole number of
        # frames at NES tiles_per_frame.
        per_frame = spec["tiles_per_frame"]
        n_frames = item_def.get("nes_frame_count")
        if n_frames is None:
            # Best-effort: tile_ids count should be a multiple of per_frame.
            if len(tile_ids) % per_frame != 0 and not override_reason:
                dispatch_warnings.append(
                    f"{name}: {len(tile_ids)} tile_ids not a multiple of "
                    f"{per_frame} (NES draws {per_frame} tiles per frame for "
                    f"{dispatch} dispatch). Add 'nes_frame_count' to manifest."
                )
        else:
            expected = int(n_frames) * per_frame
            if len(tile_ids) != expected and not override_reason:
                dispatch_warnings.append(
                    f"{name}: tile_ids count {len(tile_ids)} != "
                    f"nes_frame_count {n_frames} * tiles_per_frame {per_frame} "
                    f"= {expected}. Either over-extracted or wrong count."
                )

    if dispatch_warnings:
        header = ("NES dispatch / manifest mismatches"
                  + (" (--strict)" if args.strict else " (warnings)"))
        body = ":\n  - " + "\n  - ".join(dispatch_warnings)
        if args.strict:
            fail(header + body)
        print("WARN: " + header + body)

    variants = manifest.get("variants", [])
    if not variants:
        fail("manifest has no variants")
    for variant in variants:
        tiles = variant.get("tiles", {})
        for item_def in defs:
            for tile_id in item_def["tile_ids"]:
                meta = tiles.get(tile_id)
                if meta is None:
                    fail(f"{variant.get('rom_id')} missing tile {tile_id}")
                if meta.get("source") == "guessed_common_chr":
                    fail(f"{tile_id} uses guessed_common_chr")
                raw = bytes.fromhex(meta.get("bytes", ""))
                if len(raw) != 16:
                    fail(f"{variant.get('rom_id')} {tile_id} is not 16 bytes")

    if not HEADER.exists():
        fail(f"missing generated header: {HEADER}")
    htext = HEADER.read_text(encoding="ascii")
    m = re.search(r"#define\s+ROOMROM_ATLAS_ITEMS_X4_TILE_COUNT\s+(\d+)u", htext)
    if not m:
        fail("atlas header missing ROOMROM_ATLAS_ITEMS_X4_TILE_COUNT")
    if int(m.group(1)) != total_tiles:
        fail("atlas header tile count does not match manifest")

    src = SOURCE.read_text(encoding="utf-8")
    for token in FORBIDDEN_COMMON_GUESSES:
        if token in src:
            fail(f"renderer still contains forbidden guessed item tile {token}")
    if "roomrom_atlas_items_x4" not in src:
        fail("renderer is not wired to atlas/items_chr_x4 blob")

    print(
        f"OK: item CHR manifest verified "
        f"({len(variants)} variant(s), {total_tiles} generated tiles)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
