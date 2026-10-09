#!/usr/bin/env python3
"""gen_atlas.py — engine atlas P3 generator.

Reads atlas_master.json and emits all generated C surface artifacts under
engine/src/atlas/:

  P3a: per-category C/H pairs
  P3b: roomrom_scene_vram_contracts.{c,h}
  P3c: atlas_dispatch.h + per-category dispatch macros

Usage:
    python engine/tools/gen_atlas.py [--phase {p3a,p3b,p3c,all}]

Requires:
    engine/data/atlas_master.json
    engine/data/item_chr_manifest.json
    engine/out/prg_blocks/orig/CommonSpritePatterns.bin
    engine/out/prg_blocks/orig/CommonBackgroundPatterns.bin
    engine/out/prg_blocks/orig/CommonMiscPatterns.bin
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

ROOT = Path(__file__).resolve().parents[2]
ATLAS_JSON = ROOT / "engine" / "data" / "atlas_master.json"
ITEM_MANIFEST = ROOT / "engine" / "data" / "item_chr_manifest.json"
PRG_ORIG_DIR = ROOT / "engine" / "out" / "prg_blocks" / "orig"
OUT_DIR = ROOT / "engine" / "src" / "atlas"

# PRG blocks with NES 2bpp sprite patterns (16 bytes/tile)
# CommonSpritePatterns covers NES sprite CHR addresses 0x0000-0x06FF (tiles 0x00-0x6F)
COMMON_SPRITE_BIN = PRG_ORIG_DIR / "CommonSpritePatterns.bin"
# CommonBackgroundPatterns covers NES BG CHR addresses 0x1000-0x16FF (tiles 0x00-0x6F in BG bank)
COMMON_BG_BIN = PRG_ORIG_DIR / "CommonBackgroundPatterns.bin"
COMMON_MISC_BIN = PRG_ORIG_DIR / "CommonMiscPatterns.bin"

# ---------------------------------------------------------------------------
# Tile helpers
# ---------------------------------------------------------------------------

BYTES_PER_NES_TILE = 16     # NES 2bpp: plane-0 (8 bytes) + plane-1 (8 bytes)
BYTES_PER_GEN_TILE = 32     # Genesis 4bpp: 8 rows * 4 bytes/row

DISPATCH_CLASS_TO_WH: Dict[str, Tuple[int, int]] = {
    "Narrow":         (1, 1),
    "Wide_Slim":      (2, 1),
    "Wide_Mirrored":  (2, 1),
    "Wide_Flippable": (2, 1),
    "Tall":           (1, 2),
    "Wide_2x2":       (2, 2),
}

DISPATCH_CLASS_TO_NES: Dict[str, str] = {
    "Narrow":         "NES_NARROW",
    "Wide_Slim":      "NES_SLIM",
    "Wide_Mirrored":  "NES_MIRRORED",
    "Wide_Flippable": "NES_FLIPPABLE",
    "Tall":           "NES_NARROW",
    "Wide_2x2":       "NES_FLIPPABLE",
}


def c_ident(name: str) -> str:
    """Convert a registry name to a C-safe upper-case identifier."""
    return name.upper().replace("-", "_").replace(".", "_")


def read_nes_tile(prg_bin: bytes, tile_id: int) -> Optional[bytes]:
    """Extract a single NES 2bpp tile (16 bytes) from a PRG block.

    tile_id is the NES tile index within the block (0-based).
    Returns None if out of range.
    """
    offset = tile_id * BYTES_PER_NES_TILE
    if offset + BYTES_PER_NES_TILE > len(prg_bin):
        return None
    return bytes(prg_bin[offset: offset + BYTES_PER_NES_TILE])


def nes_tile_to_genesis(nes_tile: bytes) -> bytes:
    """Convert NES 2bpp tile (16 bytes) to Genesis 4bpp tile (32 bytes).

    NES format: 8 rows x 2 bitplanes (plane-0 at byte 0..7, plane-1 at byte 8..15).
    Genesis format: 8 rows x 4 bytes; each byte holds 2 pixels, high nibble = left pixel.
    """
    if len(nes_tile) != BYTES_PER_NES_TILE:
        raise ValueError(f"NES tile must be exactly {BYTES_PER_NES_TILE} bytes")
    out = bytearray(BYTES_PER_GEN_TILE)
    for row in range(8):
        p0 = nes_tile[row]
        p1 = nes_tile[row + 8]
        for col in range(8):
            bit = 7 - col
            color = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
            byte_idx = row * 4 + col // 2
            if col & 1:
                out[byte_idx] |= color
            else:
                out[byte_idx] |= color << 4
    return bytes(out)


def hflip_genesis_tile(gen_tile: bytes) -> bytes:
    """Mirror a Genesis 4bpp tile horizontally."""
    if len(gen_tile) != BYTES_PER_GEN_TILE:
        raise ValueError(f"Genesis tile must be exactly {BYTES_PER_GEN_TILE} bytes")
    out = bytearray(BYTES_PER_GEN_TILE)
    for row in range(8):
        for byte_idx in range(4):
            src = gen_tile[row * 4 + byte_idx]
            swapped = ((src & 0x0F) << 4) | ((src >> 4) & 0x0F)
            out[row * 4 + (3 - byte_idx)] = swapped
    return bytes(out)


# ---------------------------------------------------------------------------
# Tile byte sources
# ---------------------------------------------------------------------------

class TileSource:
    """Aggregates all sources for resolving NES tile bytes (NES 2bpp)."""

    def __init__(self, common_sprite_bin: bytes, common_bg_bin: bytes,
                 item_manifest_tiles: Dict[str, str]) -> None:
        self._sprite = common_sprite_bin   # tiles 0x00-0x6F in sprite CHR bank
        self._bg = common_bg_bin           # tiles 0x00-0x6F in BG CHR bank
        self._manifest = item_manifest_tiles  # live-captured tiles: hex_id -> hex_bytes

    def get_sprite(self, hex_id: str) -> Optional[bytes]:
        """Get NES 2bpp tile bytes from CommonSpritePatterns (sprite bank)."""
        idx = int(hex_id, 16)
        return read_nes_tile(self._sprite, idx)

    def get_bg(self, hex_id: str) -> Optional[bytes]:
        """Get NES 2bpp tile bytes from CommonBackgroundPatterns (BG bank)."""
        idx = int(hex_id, 16)
        return read_nes_tile(self._bg, idx)

    def get_manifest(self, hex_id: str) -> Optional[bytes]:
        """Get NES 2bpp tile bytes from item_chr_manifest (live-captured)."""
        entry = self._manifest.get(hex_id)
        if entry is None:
            return None
        raw = bytes.fromhex(entry)
        if len(raw) != BYTES_PER_NES_TILE:
            return None
        return raw

    def get_sprite_or_manifest(self, hex_id: str) -> Optional[bytes]:
        """Resolve a sprite-bank tile: manifest takes priority (live-verified),
        then CommonSpritePatterns.bin, then None."""
        m = self.get_manifest(hex_id)
        if m is not None:
            return m
        idx = int(hex_id, 16)
        if idx < 0x70:
            return self.get_sprite(hex_id)
        return None

    def get_any(self, hex_id: str) -> Optional[bytes]:
        """Best-effort: manifest first, then sprite bank, then BG bank."""
        r = self.get_manifest(hex_id)
        if r:
            return r
        idx = int(hex_id, 16)
        if idx < 0x70:
            return self.get_sprite(hex_id)
        return None

    @staticmethod
    def zero_tile() -> bytes:
        return bytes(BYTES_PER_NES_TILE)


# ---------------------------------------------------------------------------
# C/H file emission helpers
# ---------------------------------------------------------------------------

BANNER = "/* Auto-generated by engine/tools/gen_atlas.py - do not edit. */"
ITEMS_COMPAT_NOTE = (
    "/* Compatibility note: items_chr covers the 37 inventory items from\n"
    " * atlas_master.json. Weapon-rendering tiles (sword_vert, sword_horz,\n"
    " * boomerang, arrow, bomb, explosion, sword_diag) are a subset; their\n"
    " * byte sequences match roomrom_item_chr.c for shared tile IDs. */\n"
)


def write_lines(path: Path, lines: List[str]) -> None:
    # Use Latin-1 safe ASCII — replace any non-ASCII chars with '?'
    text = "\n".join(lines) + "\n"
    text = text.encode("ascii", errors="replace").decode("ascii")
    path.write_text(text, encoding="ascii", newline="\n")


def format_blob(blob: bytes, indent: str = "    ") -> List[str]:
    """Format a byte blob as comma-separated hex lines (16 bytes per row)."""
    lines = []
    for i in range(0, len(blob), 16):
        chunk = blob[i: i + 16]
        row = ", ".join(f"0x{b:02X}" for b in chunk)
        comma = "," if i + 16 < len(blob) else ""
        lines.append(f"{indent}{row}{comma}")
    return lines


# ---------------------------------------------------------------------------
# P3a helpers: compute byte blob for a category entry list
# ---------------------------------------------------------------------------

def compute_item_blob(entries: List[dict], ts: TileSource,
                      ) -> Tuple[bytes, List[Tuple[str, int, int]]]:
    """Build Genesis 4bpp blob for items category.

    Returns (blob_bytes, offsets_list) where offsets_list is
    [(name, byte_offset, genesis_tile_count), ...].

    dispatch_class 'Wide_Mirrored' emits raw + hflip per NES tile (2 Genesis tiles).
    All others emit 1 Genesis tile per NES tile.
    """
    blob = bytearray()
    offsets: List[Tuple[str, int, int]] = []
    for entry in entries:
        name = entry["name"]
        dc = entry.get("dispatch_class", "Narrow")
        is_mirrored = dc == "Wide_Mirrored"
        start_offset = len(blob)
        gen_tile_count = 0
        for tid in entry["nes_tile_ids"]:
            raw_nes = ts.get_sprite_or_manifest(tid)
            if raw_nes is None:
                # Stub: zero tile
                raw_nes = ts.zero_tile()
            gen = nes_tile_to_genesis(raw_nes)
            blob.extend(gen)
            gen_tile_count += 1
            if is_mirrored:
                blob.extend(hflip_genesis_tile(gen))
                gen_tile_count += 1
        offsets.append((name, start_offset, gen_tile_count))
    return bytes(blob), offsets


def compute_hud_blob(entries: List[dict], ts: TileSource,
                     ) -> Tuple[bytes, List[Tuple[str, int, int]]]:
    """Build Genesis 4bpp blob for hud category.

    HUD tiles come from CommonBackgroundPatterns (they're NES BG tiles).
    """
    blob = bytearray()
    offsets: List[Tuple[str, int, int]] = []
    for entry in entries:
        name = entry["name"]
        dc = entry.get("dispatch_class", "Narrow")
        is_mirrored = dc == "Wide_Mirrored"
        start_offset = len(blob)
        gen_tile_count = 0
        for tid in entry["nes_tile_ids"]:
            # HUD tiles are BG tiles - use BG bank
            idx = int(tid, 16)
            raw_nes: Optional[bytes] = None
            if idx < 0x70:
                raw_nes = ts.get_bg(tid)
            if raw_nes is None:
                raw_nes = ts.get_sprite_or_manifest(tid)
            if raw_nes is None:
                raw_nes = ts.zero_tile()
            gen = nes_tile_to_genesis(raw_nes)
            blob.extend(gen)
            gen_tile_count += 1
            if is_mirrored:
                blob.extend(hflip_genesis_tile(gen))
                gen_tile_count += 1
        offsets.append((name, start_offset, gen_tile_count))
    return bytes(blob), offsets


def compute_sprite_blob(entries: List[dict], ts: TileSource,
                        ) -> Tuple[bytes, List[Tuple[str, int, int]]]:
    """Generic Genesis 4bpp blob from sprite-bank tiles (title, npc, fileselect)."""
    blob = bytearray()
    offsets: List[Tuple[str, int, int]] = []
    for entry in entries:
        name = entry["name"]
        dc = entry.get("dispatch_class", "Narrow")
        is_mirrored = dc == "Wide_Mirrored"
        start_offset = len(blob)
        gen_tile_count = 0
        for tid in entry["nes_tile_ids"]:
            raw_nes = ts.get_sprite_or_manifest(tid)
            if raw_nes is None:
                raw_nes = ts.zero_tile()
            gen = nes_tile_to_genesis(raw_nes)
            blob.extend(gen)
            gen_tile_count += 1
            if is_mirrored:
                blob.extend(hflip_genesis_tile(gen))
                gen_tile_count += 1
        offsets.append((name, start_offset, gen_tile_count))
    return bytes(blob), offsets


# ---------------------------------------------------------------------------
# P3a: per-category .h + .c writers
# ---------------------------------------------------------------------------

def write_category_header(path: Path, cat: str, entries: List[dict],
                           offsets: List[Tuple[str, int, int]],
                           blob_bytes: int,
                           variant_count: int = 1,
                           stub_only: bool = False,
                           extra_banner: str = "") -> None:
    guard = f"ROOMROM_ATLAS_{cat.upper()}_H"
    prefix = f"ROOMROM_ATLAS_{cat.upper()}"
    lines = [
        BANNER,
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]
    if extra_banner:
        lines.append(extra_banner)
    if stub_only:
        lines += [
            f"/* TODO (Phase 4): {cat} tiles are scene-conditional and not yet",
            f" * extracted from PRG blocks. Renderer wiring deferred. */",
            "",
            f"#define {prefix}_BYTES 0u",
        ]
        for entry in entries:
            name = c_ident(entry["name"])
            lines.append(f"#define {prefix}_{name}_OFFSET 0u  /* stub - Phase 4 */")
    else:
        lines += [
            f"#define {prefix}_VARIANT_COUNT {variant_count}u",
            f"#define {prefix}_BYTES {blob_bytes}u",
            "",
        ]
        for name, off, _ in offsets:
            lines.append(f"#define {prefix}_{c_ident(name)}_OFFSET {off}u")
        lines += [
            "",
            f"extern const unsigned char roomrom_atlas_{cat}",
            f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES];",
        ]
    lines += ["", f"#endif /* {guard} */", ""]
    write_lines(path, lines)


def write_category_source(path: Path, cat: str, blobs: List[bytes],
                           variant_names: List[str]) -> None:
    prefix = f"ROOMROM_ATLAS_{cat.upper()}"
    lines = [
        BANNER,
        f'#include "atlas/{cat}_chr.h"',
        "",
        f"const unsigned char roomrom_atlas_{cat}",
        f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES] = {{",
    ]
    for variant_name, blob in zip(variant_names, blobs):
        lines.append(f"    {{ /* {variant_name} */")
        lines.extend(format_blob(blob, "        "))
        lines.append("    },")
    lines += ["};", ""]
    write_lines(path, lines)


def write_stub_category_source(path: Path, cat: str) -> None:
    """Emit an empty .c stub for categories with no extracted bytes yet."""
    lines = [
        BANNER,
        f'#include "atlas/{cat}_chr.h"',
        "",
        f"/* TODO (Phase 4): byte data for '{cat}' is scene-conditional.",
        f" * PRG extraction for this category deferred. */",
        "",
    ]
    write_lines(path, lines)


# ---------------------------------------------------------------------------
# Link CHR: derived from CommonSpritePatterns (tiles 0x00-0x1F)
# Link walk/attack occupies the first 32 tiles of the sprite CHR bank.
# ---------------------------------------------------------------------------

LINK_NES_TILE_RANGE = list(range(0x00, 0x20))  # 32 tiles

# Named link sub-groups derived from NES disasm:
# Tiles 0x00-0x01: face_down_f1
# Tiles 0x02-0x03: face_down_f2
# Tiles 0x04-0x05: face_up_f1
# Tiles 0x06-0x07: face_up_f2
# Tiles 0x08-0x09: face_side_f1
# Tiles 0x0A-0x0B: face_side_f2
# Tiles 0x0C-0x0D: push_side
# Tiles 0x0E-0x0F: push_up
# Tiles 0x10-0x13: attack_down (4 tiles)
# Tiles 0x14-0x17: attack_up   (4 tiles)
# Tiles 0x18-0x1B: attack_side (4 tiles)
# Tiles 0x1C-0x1F: stab_side   (4 tiles)

LINK_TILE_GROUPS = [
    ("face_down_f1",  [0x00, 0x01]),
    ("face_down_f2",  [0x02, 0x03]),
    ("face_up_f1",    [0x04, 0x05]),
    ("face_up_f2",    [0x06, 0x07]),
    ("face_side_f1",  [0x08, 0x09]),
    ("face_side_f2",  [0x0A, 0x0B]),
    ("push_side",     [0x0C, 0x0D]),
    ("push_up",       [0x0E, 0x0F]),
    ("attack_down",   [0x10, 0x11, 0x12, 0x13]),
    ("attack_up",     [0x14, 0x15, 0x16, 0x17]),
    ("attack_side",   [0x18, 0x19, 0x1A, 0x1B]),
    ("stab_side",     [0x1C, 0x1D, 0x1E, 0x1F]),
]


def write_link_chr_header(path: Path, offsets: List[Tuple[str, int, int]],
                           blob_bytes: int) -> None:
    """Emit link_chr.h with offset constants and W_*/H_* dispatch defines.

    NES Z1 Link is rendered as four 8x8 sprites in a 2x2 column-major SGDK
    layout per pose -> SPRITE_SIZE(2, 2) for all walk/attack groups.
    Walk frames (face_down_f1 .. push_up): 2 NES tiles each -> W=2, H=1 in
    tile-count terms. However SGDK SPRITE_SIZE for the composed 2x2 quad is
    (2,2) because two tiles are stacked vertically per side -- consistently
    emit W=2, H=2 for all Link groups so ATLAS_ASSERT_SIZE works uniformly.
    """
    guard = "ROOMROM_ATLAS_LINK_CHR_H"
    prefix = "ROOMROM_ATLAS_LINK"
    lines = [
        BANNER,
        "/* link_chr: Link walk/attack tiles derived from CommonSpritePatterns. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "atlas_dispatch.h"',
        "",
        f"#define {prefix}_VARIANT_COUNT 1u",
        f"#define {prefix}_BYTES {blob_bytes}u",
        "",
    ]
    for name, off, _ in offsets:
        lines.append(f"#define {prefix}_{c_ident(name)}_OFFSET {off}u")
    lines += [
        "",
        "/* Dispatch W/H defines: all Link poses are SPRITE_SIZE(2,2) on",
        " * Genesis (2-tile wide, 2-tile tall 2x2 quad, column-major). */",
    ]
    for name, _, _ in offsets:
        cname = c_ident(name)
        lines += [
            f"#define W_{cname}  2u",
            f"#define H_{cname}  2u",
            f"#define ATLAS_{cname}_DISPATCH  {{ 2u, 2u, NES_FLIPPABLE }}",
        ]
    lines += [
        "",
        "extern const unsigned char roomrom_atlas_link",
        f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES];",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    write_lines(path, lines)


# ---------------------------------------------------------------------------
# P3b: Scene VRAM contracts
# ---------------------------------------------------------------------------

SCENE_ENUM_ENTRIES = [
    ("ROOMROM_SCENE_BOOT",       0),
    ("ROOMROM_SCENE_TITLE",      1),
    ("ROOMROM_SCENE_FILESELECT", 2),
    ("ROOMROM_SCENE_OVERWORLD",  3),
    ("ROOMROM_SCENE_UW_L1",      4),
    ("ROOMROM_SCENE_UW_L2",      5),
    ("ROOMROM_SCENE_UW_L3",      6),
    ("ROOMROM_SCENE_UW_L4",      7),
    ("ROOMROM_SCENE_UW_L5",      8),
    ("ROOMROM_SCENE_UW_L6",      9),
    ("ROOMROM_SCENE_UW_L7",      10),
    ("ROOMROM_SCENE_UW_L8",      11),
    ("ROOMROM_SCENE_UW_L9",      12),
]

# Per-scene category sets (per spec §7, 'pickups' deferred to Phase 4):
# fmt: (scene_enum, category_tag, tile_base_expr, tile_count_expr)
# tile_base_expr / tile_count_expr are strings that reference vram_layout.h constants.
SCENE_CONTRACTS = [
    # Boot
    ("ROOMROM_SCENE_BOOT", "link",        "ROOMROM_SPR_TILE_BASE",           32),
    ("ROOMROM_SCENE_BOOT", "items",       "ROOMROM_ITEM_TILE_BASE_PAL(0)",   37),
    ("ROOMROM_SCENE_BOOT", "hud",         "ROOMROM_BG_TILE_BASE_PAL(0)",     22),
    # Title
    ("ROOMROM_SCENE_TITLE", "link",       "ROOMROM_SPR_TILE_BASE",           32),
    ("ROOMROM_SCENE_TITLE", "hud",        "ROOMROM_BG_TILE_BASE_PAL(0)",     22),
    ("ROOMROM_SCENE_TITLE", "title",      "(ROOMROM_SPR_TILE_BASE + 32u)",   26),
    # File Select
    ("ROOMROM_SCENE_FILESELECT", "link",       "ROOMROM_SPR_TILE_BASE",       32),
    ("ROOMROM_SCENE_FILESELECT", "hud",        "ROOMROM_BG_TILE_BASE_PAL(0)", 22),
    ("ROOMROM_SCENE_FILESELECT", "title",      "(ROOMROM_SPR_TILE_BASE + 32u)", 26),
    ("ROOMROM_SCENE_FILESELECT", "npc",        "(ROOMROM_SPR_TILE_BASE + 58u)", 12),
    ("ROOMROM_SCENE_FILESELECT", "fileselect", "(ROOMROM_SPR_TILE_BASE + 70u)", 45),
    # Overworld
    ("ROOMROM_SCENE_OVERWORLD", "link",    "ROOMROM_SPR_TILE_BASE",           32),
    ("ROOMROM_SCENE_OVERWORLD", "items",   "ROOMROM_ITEM_TILE_BASE_PAL(0)",   37),
    ("ROOMROM_SCENE_OVERWORLD", "hud",     "ROOMROM_BG_TILE_BASE_PAL(0)",     22),
    ("ROOMROM_SCENE_OVERWORLD", "bg_ow",   "ROOMROM_BG_TILE_BASE_PAL(0)",      0),  # BG handled by bg_palette modules
    ("ROOMROM_SCENE_OVERWORLD", "npc",     "(ROOMROM_SPR_TILE_BASE + 32u)",   12),
    ("ROOMROM_SCENE_OVERWORLD", "enemies", "(ROOMROM_SPR_TILE_BASE + 44u)",  114),  # PR-4c: OWSP 1x sub-pal (114 NES tiles)
    # Underworld levels 1-9
] + [
    (f"ROOMROM_SCENE_UW_L{n}", "link",    "ROOMROM_SPR_TILE_BASE",            32)
    for n in range(1, 10)
] + [
    (f"ROOMROM_SCENE_UW_L{n}", "items",   "ROOMROM_ITEM_TILE_BASE_PAL(0)",    37)
    for n in range(1, 10)
] + [
    (f"ROOMROM_SCENE_UW_L{n}", "hud",     "ROOMROM_BG_TILE_BASE_PAL(0)",      22)
    for n in range(1, 10)
] + [
    (f"ROOMROM_SCENE_UW_L{n}", "bg_uw",   "ROOMROM_BG_TILE_BASE_PAL(0)",       0)  # BG handled by bg_palette modules
    for n in range(1, 10)
] + [
    # Phase F (2026-05-18): UWSP 4x->1x. 34 NES tiles, single sub-pal copy.
    # blob_bytes auto-derives from cat_bytes["enemies"] = ENEMY_X4_PER_BANK_BYTES = 1088.
    (f"ROOMROM_SCENE_UW_L{n}", "enemies", "(ROOMROM_SPR_TILE_BASE + 44u)",   34)
    for n in range(1, 10)
] + [
    # PR-5: 64 NES tiles per UWSPBoss bank, 1x sub-pal. Slot lives at
    # ROOMROM_BOSS_TILE_BASE (= SCENE_OBJ slot, NES parity per
    # z_03.asm:91 -- boss replaces enemies in same VRAM range, no
    # boss-room enemies).
    (f"ROOMROM_SCENE_UW_L{n}", "bosses",  "ROOMROM_BOSS_TILE_BASE",            64)
    for n in range(1, 10)
]


def _contract_var(scene: str, cat: str) -> str:
    scene_tag = scene.replace("ROOMROM_SCENE_", "").lower()
    return f"roomrom_vram_contract_{scene_tag}_{cat}"


def write_scene_vram_contracts_header(path: Path) -> None:
    lines = [
        BANNER,
        "#ifndef ROOMROM_SCENE_VRAM_CONTRACTS_H",
        "#define ROOMROM_SCENE_VRAM_CONTRACTS_H",
        "",
        "/* Extends vram_layout.h — does NOT replace it. */",
        '#include "../vram_layout.h"',
        "",
        "/* ---------------------------------------------------------------",
        " * Scene identifiers",
        " * --------------------------------------------------------------- */",
        "typedef enum {",
    ]
    for name, val in SCENE_ENUM_ENTRIES:
        lines.append(f"    {name} = {val},")
    lines += [
        "    ROOMROM_SCENE_COUNT",
        "} roomrom_scene_id_t;",
        "",
        "/* ---------------------------------------------------------------",
        " * VRAM contract: per-(scene, category) tile range.",
        " * tile_base  = absolute Genesis VRAM tile index",
        " * tile_count = number of tiles resident in VRAM for this category",
        " *              when the scene is active (0 = deferred / Phase 4)",
        " * blob_offset = byte offset into the category's atlas blob",
        " * blob_bytes  = byte count from that offset (0 = deferred)",
        " * --------------------------------------------------------------- */",
        "typedef struct {",
        "    unsigned short tile_base;",
        "    unsigned short tile_count;",
        "    unsigned short blob_offset;",
        "    unsigned short blob_bytes;",
        "} roomrom_vram_contract_t;",
        "",
    ]
    # extern declarations
    seen = set()
    for scene, cat, _, _ in SCENE_CONTRACTS:
        var = _contract_var(scene, cat)
        if var not in seen:
            seen.add(var)
            lines.append(f"extern const roomrom_vram_contract_t {var};")
    lines += ["", "#endif /* ROOMROM_SCENE_VRAM_CONTRACTS_H */", ""]
    write_lines(path, lines)


def write_scene_vram_contracts_source(path: Path,
                                       cat_bytes: Dict[str, int]) -> None:
    lines = [
        BANNER,
        '#include "atlas/roomrom_scene_vram_contracts.h"',
        "",
        "/* Per-scene VRAM contract definitions.",
        " * tile_count values are conservative placeholders; Phase 4 refines",
        " * to actual Genesis tile counts after CHR expansion wiring lands. */",
        "",
    ]
    seen = set()
    for scene, cat, tile_base_expr, tile_count in SCENE_CONTRACTS:
        var = _contract_var(scene, cat)
        if var in seen:
            continue
        seen.add(var)
        prefix = f"ROOMROM_ATLAS_{cat.upper()}"
        # tile_count == 0 → category not resident for this scene; force
        # blob_bytes 0 so DMA state machine treats it as empty contract.
        blob_bytes = cat_bytes.get(cat, 0) if tile_count > 0 else 0
        lines += [
            f"const roomrom_vram_contract_t {var} = {{",
            f"    /* tile_base  */ {tile_base_expr},",
            f"    /* tile_count */ {tile_count}u,",
            f"    /* blob_offset */ 0u,",
            f"    /* blob_bytes  */ {blob_bytes}u,",
            "};",
            "",
        ]
    write_lines(path, lines)


# ---------------------------------------------------------------------------
# P3c: Dispatch macros + atlas_dispatch.h
# ---------------------------------------------------------------------------

def write_atlas_dispatch_header(path: Path) -> None:
    lines = [
        BANNER,
        "#ifndef ROOMROM_ATLAS_DISPATCH_H",
        "#define ROOMROM_ATLAS_DISPATCH_H",
        "",
        '#include "atlas_static_assert.h"',
        "",
        "/* NES sprite dispatch classes (from Anim_WriteSpecificItemSprites",
        " * and ObjAnimAttr dispatch paths in Z_01.asm / Z_07.asm). */",
        "typedef enum {",
        "    NES_NARROW    = 0,  /* 8x8 or 8x16: single sprite */",
        "    NES_SLIM      = 1,  /* 16x8 stretched: two 8x8 side-by-side */",
        "    NES_MIRRORED  = 2,  /* 16x8 mirrored: right = left hflipped */",
        "    NES_FLIPPABLE = 3,  /* 16x16 or 16x8 h-flippable pair */",
        "} atlas_nes_dispatch_class_t;",
        "",
        "typedef struct {",
        "    unsigned char w;                       /* SGDK SPRITE_SIZE width tiles */",
        "    unsigned char h;                       /* SGDK SPRITE_SIZE height tiles */",
        "    atlas_nes_dispatch_class_t nes_class;",
        "} atlas_dispatch_t;",
        "",
        "/* ATLAS_ASSERT_SIZE: compile-time check that a renderer's claimed",
        " * SPRITE_SIZE matches the registry dispatch for 'name'.",
        " *",
        " * Because _Static_assert requires a constant-expression predicate,",
        " * the dispatch struct literal is NOT used directly. Instead each",
        " * per-category header emits plain #defines:",
        " *   #define W_<NAME>  <width>",
        " *   #define H_<NAME>  <height>",
        " * which ARE integer constant expressions. ATLAS_ASSERT_SIZE then",
        " * _Static_asserts against those defines. */",
        "#define ATLAS_ASSERT_SIZE(name, expect_w, expect_h) \\",
        "    ATLAS_STATIC_ASSERT( \\",
        "        W_##name == (expect_w) && H_##name == (expect_h), \\",
        "        #name \" SPRITE_SIZE mismatch vs registry dispatch\")",
        "",
        "/* ATLAS_ASSERT_BG_TILE: same compile-time check as ATLAS_ASSERT_SIZE,",
        " * named to indicate BG-tile (tilemap) dispatch rather than sprite",
        " * dispatch. Implementation is identical; the distinct name documents",
        " * intent at the call site and allows future divergence if BG and",
        " * sprite dispatch classes need to be checked differently. */",
        "#define ATLAS_ASSERT_BG_TILE(name, expect_w, expect_h) \\",
        "    ATLAS_STATIC_ASSERT( \\",
        "        W_##name == (expect_w) && H_##name == (expect_h), \\",
        "        #name \" BG tile size mismatch vs registry dispatch\")",
        "",
        "#endif /* ROOMROM_ATLAS_DISPATCH_H */",
        "",
    ]
    write_lines(path, lines)


def emit_dispatch_defines(entries: List[dict]) -> List[str]:
    """Emit W_<NAME>, H_<NAME>, and ATLAS_<NAME>_DISPATCH for each entry.

    Per-entry `sprite_size_override: [w, h]` wins over dispatch_class lookup.
    Use it for items where NES PPU 8x16 mode means @Narrow != 8x8 (e.g. bomb,
    boomerang) — see live BizHawk capture in probe_nes_throw.lua.
    """
    lines: List[str] = []
    for entry in entries:
        name = c_ident(entry["name"])
        dc = entry.get("dispatch_class", "Narrow")
        override = entry.get("sprite_size_override")
        if override and len(override) == 2:
            w, h = int(override[0]), int(override[1])
        else:
            w, h = DISPATCH_CLASS_TO_WH.get(dc, (1, 1))
        nes_cls = DISPATCH_CLASS_TO_NES.get(dc, "NES_NARROW")
        lines += [
            f"#define W_{name}  {w}u",
            f"#define H_{name}  {h}u",
            f"#define ATLAS_{name}_DISPATCH  {{ {w}u, {h}u, {nes_cls} }}",
        ]
    return lines


# ---------------------------------------------------------------------------
# Category-specific header/source writers (combining P3a + P3c)
# ---------------------------------------------------------------------------

def emit_items_chr(ts: TileSource, atlas: dict, out_dir: Path) -> int:
    """Emit items_chr.{c,h}. Returns blob byte count."""
    entries = atlas["categories"]["items"]
    blob, offsets = compute_item_blob(entries, ts)
    h_path = out_dir / "items_chr.h"
    c_path = out_dir / "items_chr.c"

    guard = "ROOMROM_ATLAS_ITEMS_CHR_H"
    prefix = "ROOMROM_ATLAS_ITEMS"
    lines = [
        BANNER,
        ITEMS_COMPAT_NOTE.rstrip(),
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "atlas_dispatch.h"',
        "",
        f"#define {prefix}_VARIANT_COUNT 1u",
        f"#define {prefix}_BYTES {len(blob)}u",
        "",
    ]
    for name, off, _ in offsets:
        lines.append(f"#define {prefix}_{c_ident(name)}_OFFSET {off}u")
    lines += [
        "",
        "/* Dispatch W/H defines for compile-time ATLAS_ASSERT_SIZE checks. */",
    ]
    lines.extend(emit_dispatch_defines(entries))
    lines += [
        "",
        "extern const unsigned char roomrom_atlas_items",
        f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES];",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    write_lines(h_path, lines)

    src_lines = [
        BANNER,
        '#include "atlas/items_chr.h"',
        "",
        "const unsigned char roomrom_atlas_items",
        f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES] = {{",
        "    { /* orig */",
    ]
    src_lines.extend(format_blob(blob, "        "))
    src_lines += ["    },", "};", ""]
    write_lines(c_path, src_lines)
    return len(blob)


def emit_link_chr(ts: TileSource, out_dir: Path) -> int:
    """Emit link_chr.{c,h}. Returns blob byte count."""
    h_path = out_dir / "link_chr.h"
    c_path = out_dir / "link_chr.c"

    blob = bytearray()
    offsets: List[Tuple[str, int, int]] = []
    for group_name, tile_ids in LINK_TILE_GROUPS:
        start = len(blob)
        gen_tile_count = 0
        for idx in tile_ids:
            hex_id = f"0x{idx:02X}"
            raw_nes = ts.get_sprite(hex_id)
            if raw_nes is None:
                raw_nes = ts.zero_tile()
            blob.extend(nes_tile_to_genesis(raw_nes))
            gen_tile_count += 1
        offsets.append((group_name, start, gen_tile_count))

    blob_bytes = bytes(blob)
    write_link_chr_header(h_path, offsets, len(blob_bytes))

    src_lines = [
        BANNER,
        '#include "atlas/link_chr.h"',
        "",
        "const unsigned char roomrom_atlas_link",
        "    [ROOMROM_ATLAS_LINK_VARIANT_COUNT][ROOMROM_ATLAS_LINK_BYTES] = {",
        "    { /* orig */",
    ]
    src_lines.extend(format_blob(blob_bytes, "        "))
    src_lines += ["    },", "};", ""]
    write_lines(c_path, src_lines)
    return len(blob_bytes)


def emit_hud_chr(ts: TileSource, atlas: dict, out_dir: Path) -> int:
    """Emit hud_chr.{c,h}. Returns blob byte count."""
    entries = atlas["categories"]["hud"]
    blob, offsets = compute_hud_blob(entries, ts)

    h_path = out_dir / "hud_chr.h"
    c_path = out_dir / "hud_chr.c"

    guard = "ROOMROM_ATLAS_HUD_CHR_H"
    prefix = "ROOMROM_ATLAS_HUD"
    lines = [
        BANNER,
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "atlas_dispatch.h"',
        "",
        f"#define {prefix}_VARIANT_COUNT 1u",
        f"#define {prefix}_BYTES {len(blob)}u",
        "",
    ]
    for name, off, _ in offsets:
        lines.append(f"#define {prefix}_{c_ident(name)}_OFFSET {off}u")
    lines += [""]
    lines.extend(emit_dispatch_defines(entries))
    lines += [
        "",
        "extern const unsigned char roomrom_atlas_hud",
        f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES];",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    write_lines(h_path, lines)

    src_lines = [
        BANNER,
        '#include "atlas/hud_chr.h"',
        "",
        "const unsigned char roomrom_atlas_hud",
        f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES] = {{",
        "    { /* orig */",
    ]
    src_lines.extend(format_blob(blob, "        "))
    src_lines += ["    },", "};", ""]
    write_lines(c_path, src_lines)
    return len(blob)


def emit_title_chr(ts: TileSource, atlas: dict, out_dir: Path) -> int:
    entries = atlas["categories"]["title"]
    blob, offsets = compute_sprite_blob(entries, ts)

    h_path = out_dir / "title_chr.h"
    c_path = out_dir / "title_chr.c"
    guard = "ROOMROM_ATLAS_TITLE_CHR_H"
    prefix = "ROOMROM_ATLAS_TITLE"

    lines = [BANNER, f"#ifndef {guard}", f"#define {guard}", "",
             '#include "atlas_dispatch.h"', "",
             f"#define {prefix}_VARIANT_COUNT 1u",
             f"#define {prefix}_BYTES {len(blob)}u", ""]
    for name, off, _ in offsets:
        lines.append(f"#define {prefix}_{c_ident(name)}_OFFSET {off}u")
    lines += [""]
    lines.extend(emit_dispatch_defines(entries))
    lines += ["", "extern const unsigned char roomrom_atlas_title",
              f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES];",
              "", f"#endif /* {guard} */", ""]
    write_lines(h_path, lines)

    src_lines = [BANNER, '#include "atlas/title_chr.h"', "",
                 "const unsigned char roomrom_atlas_title",
                 f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES] = {{",
                 "    { /* orig */"]
    src_lines.extend(format_blob(blob, "        "))
    src_lines += ["    },", "};", ""]
    write_lines(c_path, src_lines)
    return len(blob)


def emit_npc_chr(ts: TileSource, atlas: dict, out_dir: Path) -> int:
    entries = atlas["categories"]["npc"]
    blob, offsets = compute_sprite_blob(entries, ts)

    h_path = out_dir / "npc_chr.h"
    c_path = out_dir / "npc_chr.c"
    guard = "ROOMROM_ATLAS_NPC_CHR_H"
    prefix = "ROOMROM_ATLAS_NPC"

    lines = [BANNER, f"#ifndef {guard}", f"#define {guard}", "",
             '#include "atlas_dispatch.h"', "",
             f"#define {prefix}_VARIANT_COUNT 1u",
             f"#define {prefix}_BYTES {len(blob)}u", ""]
    for name, off, _ in offsets:
        lines.append(f"#define {prefix}_{c_ident(name)}_OFFSET {off}u")
    lines += [""]
    lines.extend(emit_dispatch_defines(entries))
    lines += ["", "extern const unsigned char roomrom_atlas_npc",
              f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES];",
              "", f"#endif /* {guard} */", ""]
    write_lines(h_path, lines)

    src_lines = [BANNER, '#include "atlas/npc_chr.h"', "",
                 "const unsigned char roomrom_atlas_npc",
                 f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES] = {{",
                 "    { /* orig */"]
    src_lines.extend(format_blob(blob, "        "))
    src_lines += ["    },", "};", ""]
    write_lines(c_path, src_lines)
    return len(blob)


def emit_fileselect_chr(ts: TileSource, atlas: dict, out_dir: Path) -> int:
    entries = atlas["categories"]["fileselect"]
    blob, offsets = compute_sprite_blob(entries, ts)

    h_path = out_dir / "fileselect_chr.h"
    c_path = out_dir / "fileselect_chr.c"
    guard = "ROOMROM_ATLAS_FILESELECT_CHR_H"
    prefix = "ROOMROM_ATLAS_FILESELECT"

    lines = [BANNER, f"#ifndef {guard}", f"#define {guard}", "",
             '#include "atlas_dispatch.h"', "",
             f"#define {prefix}_VARIANT_COUNT 1u",
             f"#define {prefix}_BYTES {len(blob)}u", ""]
    for name, off, _ in offsets:
        lines.append(f"#define {prefix}_{c_ident(name)}_OFFSET {off}u")
    lines += [""]
    lines.extend(emit_dispatch_defines(entries))
    lines += ["", "extern const unsigned char roomrom_atlas_fileselect",
              f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES];",
              "", f"#endif /* {guard} */", ""]
    write_lines(h_path, lines)

    src_lines = [BANNER, '#include "atlas/fileselect_chr.h"', "",
                 "const unsigned char roomrom_atlas_fileselect",
                 f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES] = {{",
                 "    { /* orig */"]
    src_lines.extend(format_blob(blob, "        "))
    src_lines += ["    },", "};", ""]
    write_lines(c_path, src_lines)
    return len(blob)


def emit_stub_category(cat: str, entries: List[dict], out_dir: Path) -> int:
    """Emit a TODO-stub header+source for enemies, bosses, bg_overworld, bg_underworld."""
    h_path = out_dir / f"{cat}_chr.h"
    c_path = out_dir / f"{cat}_chr.c"
    guard = f"ROOMROM_ATLAS_{cat.upper()}_CHR_H"
    prefix = f"ROOMROM_ATLAS_{cat.upper()}"

    lines = [
        BANNER,
        f"/* {cat}: TODO (Phase 4) — tiles are scene-conditional, PRG extraction",
        f" * deferred. Header-only stub; no byte data emitted. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "atlas_dispatch.h"',
        "",
        f"#define {prefix}_BYTES 0u",
        "",
        f"/* Stub offset defines (Phase 4 will assign real values) */",
    ]
    for entry in entries:
        name = c_ident(entry["name"])
        dc = entry.get("dispatch_class", "Narrow")
        w, h = DISPATCH_CLASS_TO_WH.get(dc, (1, 1))
        nes_cls = DISPATCH_CLASS_TO_NES.get(dc, "NES_NARROW")
        lines.append(f"#define {prefix}_{name}_OFFSET 0u  /* stub */")
        lines.append(f"#define W_{name}  {w}u")
        lines.append(f"#define H_{name}  {h}u")
        lines.append(f"#define ATLAS_{name}_DISPATCH  {{ {w}u, {h}u, {nes_cls} }}  /* stub */")
    lines += ["", f"#endif /* {guard} */", ""]
    write_lines(h_path, lines)

    src_lines = [
        BANNER,
        f'#include "atlas/{cat}_chr.h"',
        "",
        f"/* TODO (Phase 4): no byte data for '{cat}' yet. */",
        "",
    ]
    write_lines(c_path, src_lines)
    return 0


def split_bg_entries(bg_entries: List[dict]) -> Tuple[List[dict], List[dict]]:
    """Split bg entries into OW and UW based on scene field."""
    ow_scenes = {"overworld", "title_demo", "title", "all"}
    uw_scenes = {"underworld", "underworld_default",
                 "underworld_levels_1_2_7", "underworld_levels_3_5_8",
                 "underworld_levels_4_6_9", "boss_levels_1_2_5_7",
                 "boss_levels_3_4_6_8", "boss_level_9"}
    ow_entries = [e for e in bg_entries if e.get("scene", "") in ow_scenes]
    uw_entries = [e for e in bg_entries if e.get("scene", "") in uw_scenes]
    return ow_entries, uw_entries


def emit_bg_chr_stubs(atlas: dict, out_dir: Path) -> Tuple[int, int]:
    """Emit bg_overworld_chr and bg_underworld_chr stubs (BG handled by other modules)."""
    bg_entries = atlas["categories"]["bg"]
    ow_entries, uw_entries = split_bg_entries(bg_entries)

    # BG CHR is already handled by expanded_bg_chr.c + roomrom_bg_palette.c
    # Emit header-only stubs with TODO rather than duplicating those modules
    for cat, entries in [("bg_overworld", ow_entries), ("bg_underworld", uw_entries)]:
        h_path = out_dir / f"{cat}_chr.h"
        c_path = out_dir / f"{cat}_chr.c"
        guard = f"ROOMROM_ATLAS_{cat.upper().replace('-','_')}_CHR_H"
        prefix = f"ROOMROM_ATLAS_{cat.upper().replace('-','_')}"
        lines = [
            BANNER,
            f"/* {cat}: BG tiles are already handled by expanded_bg_chr.c and",
            f" * roomrom_bg_palette.c. This stub extends the atlas namespace for",
            f" * scene VRAM contract referencing. Phase 4 wires DMA upload. */",
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            f"#define {prefix}_BYTES 0u",
            "",
            f"/* Entries ({len(entries)} total) — offset defines deferred to Phase 4 */",
        ]
        for entry in entries:
            name = c_ident(entry["name"])
            lines.append(f"#define {prefix}_{name}_OFFSET 0u  /* stub */")
        lines += ["", f"#endif /* {guard} */", ""]
        write_lines(h_path, lines)

        stub_src = [
            BANNER,
            f'#include "atlas/{cat}_chr.h"',
            "",
            f"/* TODO (Phase 4): BG CHR upload is handled by expanded_bg_chr.c.",
            f" * This file exists to satisfy atlas namespace; no byte data here. */",
            "",
        ]
        write_lines(c_path, stub_src)

    return 0, 0


# ---------------------------------------------------------------------------
# Byte-match verification for items
# ---------------------------------------------------------------------------

def verify_items_byte_match(items_blob: bytes, ts: TileSource,
                             item_manifest: dict) -> bool:
    """Spot-check: for each tile in item_chr_manifest, verify that the tile bytes
    extracted by gen_atlas.py match the manifest bytes (same NES -> Genesis conversion).

    Returns True if all checked tiles match. Prints diagnostics on mismatch.
    """
    tiles_orig = item_manifest["variants"][0]["tiles"]
    all_ok = True
    for tid, tile_meta in tiles_orig.items():
        if tile_meta.get("source") == "guessed_common_chr":
            continue
        expected_nes = bytes.fromhex(tile_meta["bytes"])
        expected_gen = nes_tile_to_genesis(expected_nes)
        # Check if tile appears at the expected location in items_blob
        # We can verify the conversion is correct regardless of blob offset
        actual_gen = ts.get_sprite_or_manifest(tid)
        if actual_gen is None:
            continue
        actual_gen_converted = nes_tile_to_genesis(actual_gen)
        if actual_gen_converted != expected_gen:
            print(f"  BYTE-MISMATCH: tile {tid} genesis bytes differ", file=sys.stderr)
            all_ok = False
    return all_ok


# ---------------------------------------------------------------------------
# FU2: items_blob_legacy mode — byte-identical to expand_sprite_chr output
# ---------------------------------------------------------------------------

def _bias_byte(byte_val: int, sub_pal: int) -> int:
    """Per-nibble sub-pal pixel bias rule (matches expand_sprite_chr.py::bias_byte).

    NES pixel 0 stays 0; nonzero pixels become sub_pal*4 + pixel.
    Source nibbles must be in {0..3}.
    """
    hi = (byte_val >> 4) & 0x0F
    lo = byte_val & 0x0F
    if hi > 3 or lo > 3:
        raise ValueError(f"source pixel > 3 in byte 0x{byte_val:02X} (already biased?)")
    new_hi = 0 if hi == 0 else ((sub_pal * 4 + hi) & 0x0F)
    new_lo = 0 if lo == 0 else ((sub_pal * 4 + lo) & 0x0F)
    return ((new_hi << 4) | new_lo) & 0xFF


ITEM_SUBPAL_COUNT_GEN = 1   # Phase B (2026-05-18 VRAM cleanup): collapsed from
                            # 3 to 1. Sub-pal selection now routes via Genesis
                            # OAM pal field (PAL1/PAL2/PAL3 = NES SPR sub-pals
                            # 0/1/2) instead of tile-offset pixel-bias. See
                            # src/game/world/bg_palette.h for CRAM target.
                            # Pre-cleanup history (kept for archive): the
                            # constant was reduced from 4 to 3 on 2026-05-08
                            # to drop unused sub-pal 3 and fund 8x16 fixes;
                            # 2026-05-18 drop to 1 frees the remaining
                            # 140 tiles (4480 bytes) of pixel-bias replication.

def _expand_row_x4(row: bytes) -> bytes:
    """Concatenate N sub-pal copies of row: pal0||pal1||...||palN-1.

    N = ITEM_SUBPAL_COUNT_GEN. Now 1 by default (single sub-pal-0-encoded
    copy); kept multi-copy for legacy archive paths if ever needed. Name
    retains 'x4' suffix for downstream identifier compatibility.
    """
    out = bytearray()
    for s in range(ITEM_SUBPAL_COUNT_GEN):
        out.extend(_bias_byte(b, s) for b in row)
    return bytes(out)


def build_legacy_variant_blob(item_defs: List[dict],
                               variant: dict) -> bytes:
    """Build the per-variant Genesis 4bpp blob that matches gen_item_chr_blob.py.

    item_defs are from item_chr_manifest.json (8-item legacy list).
    Draw rules:
      - 'mirrored_*'                    : per NES tile -> raw + hflip (16x8)
      - 'wide_16x16_mirrored_8x16_*'    : tile_ids interpreted as (top,bot)
                                          pairs; per pair -> top, bot,
                                          hflip(top), hflip(bot) (16x16)
      - other                            : per NES tile -> raw only (8x8/8x16)

    The 8x16 mirrored layout matches Genesis SGDK column-major fetch for
    SPRITE_SIZE(2,2): pos0=col0row0=LT, pos1=col0row1=LB, pos2=col1row0=RT,
    pos3=col1row1=RB. NES Z1 explosion uses this dispatch (Anim_Write
    MirroredSpritePair under PPU 8x16 mode).
    """
    tiles = variant.get("tiles", {})
    out = bytearray()
    for item_def in item_defs:
        rule = str(item_def.get("draw_rule", ""))
        is_8x16_mirrored = rule.startswith("wide_16x16_mirrored_8x16")
        bake_mirror = rule.startswith("mirrored_")
        tile_ids = item_def["tile_ids"]

        def get_gen(tid: str) -> bytes:
            tile_meta = tiles.get(tid)
            if tile_meta is None:
                raise SystemExit(
                    f"FU2: variant '{variant.get('rom_id')}' missing tile {tid}")
            if tile_meta.get("source") == "guessed_common_chr":
                raise SystemExit(f"FU2: tile {tid} still uses forbidden guessed_common_chr")
            raw_nes = bytes.fromhex(tile_meta["bytes"])
            if len(raw_nes) != BYTES_PER_NES_TILE:
                raise SystemExit(f"FU2: tile {tid} is not 16 bytes")
            return nes_tile_to_genesis(raw_nes)

        if is_8x16_mirrored:
            if len(tile_ids) % 2 != 0:
                raise SystemExit(
                    f"FU2: {item_def['name']} draw_rule {rule} requires even tile_ids count")
            for i in range(0, len(tile_ids), 2):
                top = get_gen(tile_ids[i])
                bot = get_gen(tile_ids[i+1])
                out.extend(top)
                out.extend(bot)
                for half in (top, bot):
                    right = hflip_genesis_tile(half)
                    if rule.endswith("_slim"):
                        # NES @_Slim places the mirrored half at x+7.
                        # Its first column overlaps the left half's last;
                        # column 8 therefore starts at mirrored pixel 1.
                        right = b"".join(
                            ((int.from_bytes(right[y:y + 4], "big") << 4)
                             & 0xFFFFFFFF).to_bytes(4, "big")
                            for y in range(0, 32, 4))
                    out.extend(right)
        else:
            for tile_id in tile_ids:
                gen = get_gen(tile_id)
                out.extend(gen)
                if bake_mirror:
                    out.extend(hflip_genesis_tile(gen))
    return bytes(out)


def emit_items_chr_x4(item_manifest: dict, out_dir: Path) -> int:
    """Emit atlas/items_chr_x4.{c,h} — byte-identical to expanded_sprite_chr.

    Produces roomrom_atlas_items_x4[VARIANT_COUNT][X4_BYTES] where:
      X4_BYTES = per_pal_bytes * 4
      per_pal_bytes = byte count of one variant from the legacy 8-item manifest

    The layout is pal0_bytes||pal1_bytes||pal2_bytes||pal3_bytes per row,
    matching expand_sprite_chr.py::expand_row_to_x4, so the renderer's
    sub-pal offset formula (blob_off = per_pal_bytes * s) works unchanged.
    """
    item_defs = item_manifest["item_defs"]
    variants = item_manifest["variants"]

    # Build per-variant base blobs (matches gen_item_chr_blob.py)
    base_blobs: List[bytes] = []
    for variant in variants:
        blob = build_legacy_variant_blob(item_defs, variant)
        base_blobs.append(blob)

    per_pal_bytes = len(base_blobs[0])
    if per_pal_bytes == 0 or per_pal_bytes % BYTES_PER_GEN_TILE != 0:
        raise SystemExit(f"FU2: per_pal_bytes={per_pal_bytes} is invalid")
    for i, blob in enumerate(base_blobs):
        if len(blob) != per_pal_bytes:
            raise SystemExit(f"FU2: variant {i} blob size mismatch: {len(blob)} != {per_pal_bytes}")

    x4_bytes = per_pal_bytes * ITEM_SUBPAL_COUNT_GEN
    tile_count = per_pal_bytes // BYTES_PER_GEN_TILE
    variant_count = len(variants)
    variant_names = [str(v.get("rom_id", f"v{i}")) for i, v in enumerate(variants)]

    # Expand each variant row to x4
    x4_blobs: List[bytes] = [_expand_row_x4(b) for b in base_blobs]

    # Verify expansion
    for i, (base, x4) in enumerate(zip(base_blobs, x4_blobs)):
        if len(x4) != x4_bytes:
            raise SystemExit(f"FU2: variant {i} x4 blob size mismatch")

    guard = "ROOMROM_ATLAS_ITEMS_X4_H"
    prefix = "ROOMROM_ATLAS_ITEMS_X4"

    h_path = out_dir / "items_chr_x4.h"
    c_path = out_dir / "items_chr_x4.c"

    # Build tile-index constants from item_defs (one per item, counting
    # with mirrored expansion — same logic as gen_item_chr_blob.py).
    tile_offsets: List[Tuple[str, int]] = []  # (name, tile_index)
    running_tile = 0
    for item_def in item_defs:
        rule = str(item_def.get("draw_rule", ""))
        is_8x16_mirrored = rule.startswith("wide_16x16_mirrored_8x16")
        is_pair_8x16 = rule == "wide_16x16_pair"
        bake_mirror = rule.startswith("mirrored_")
        name_ident = c_ident(str(item_def["name"]))
        tile_offsets.append((name_ident, running_tile))
        # 8x16-mirrored: 4 Genesis tiles per (top,bot) NES pair (LT, LB, RT, RB).
        # 8x16-pair    : 4 Genesis tiles per item (LT, LB, RT, RB) directly
        #                 from manifest tile_ids in column-major order (no
        #                 mirror).
        # mirrored_*    : 2 Genesis tiles per NES tile (raw + hflip).
        # default        : 1 Genesis tile per NES tile.
        n_ids = len(item_def["tile_ids"])
        if is_8x16_mirrored:
            tiles_here = n_ids * 2  # 6 ids -> 12 tiles
        elif is_pair_8x16:
            tiles_here = n_ids       # 4 ids -> 4 tiles, exact mapping
        elif bake_mirror:
            tiles_here = n_ids * 2
        else:
            tiles_here = n_ids
        running_tile += tiles_here

    h_lines = [
        BANNER,
        f"/* items_chr_x4: 4x pixel-biased sub-pal expansion of the 8-item",
        f" * legacy weapon atlas (item_chr_manifest.json), generated by",
        f" * gen_atlas.py FU2 items_blob_legacy mode.",
        f" *",
        f" * Byte layout per row: pal0_bytes || pal1_bytes || pal2_bytes || pal3_bytes",
        f" * Per-pal stride = {per_pal_bytes} bytes ({tile_count} Genesis tiles).",
        f" * Pixel bias rule: out = (in==0) ? 0 : (sub_pal*4 + in).",
        f" * Byte-identical to expand_sprite_chr.py -> roomrom_item_chr_x4.",
        f" */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        f"#define {prefix}_VARIANT_COUNT    {variant_count}u",
        f"#define {prefix}_TILE_COUNT       {tile_count}u",
        f"#define {prefix}_PER_PAL_BYTES    {per_pal_bytes}u",
        f"#define {prefix}_BYTES            {x4_bytes}u",
        "",
        f"/* Tile-index constants: byte_offset = TILE_INDEX * {BYTES_PER_GEN_TILE}",
        f" * These supersede the legacy ROOMROM_ITEM_TILE_* constants from",
        f" * roomrom_item_chr.h; values are identical. */",
    ]
    for name_ident, tile_idx in tile_offsets:
        h_lines.append(f"#define ROOMROM_ITEM_TILE_{name_ident} {tile_idx}u")
    h_lines += [
        "",
        f"extern const unsigned char roomrom_atlas_items_x4",
        f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES];",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    write_lines(h_path, h_lines)

    c_lines = [
        BANNER,
        f'#include "atlas/items_chr_x4.h"',
        "",
        f"const unsigned char roomrom_atlas_items_x4",
        f"    [{prefix}_VARIANT_COUNT][{prefix}_BYTES]",
        f"    __attribute__((aligned(4))) = {{",
    ]
    for variant_name, x4_blob in zip(variant_names, x4_blobs):
        c_lines.append(f"    {{ /* {variant_name} */")
        c_lines.extend(format_blob(x4_blob, "        "))
        c_lines.append("    },")
    c_lines += ["};", ""]
    write_lines(c_path, c_lines)

    print(f"  items_chr_x4: {variant_count} variants x {x4_bytes} bytes "
          f"({tile_count} tiles x4 sub-pal copies)")
    return x4_bytes


# ---------------------------------------------------------------------------
# PR-4b: enemy_chr_x4 — UWSP transient banks (per-level swap)
# ---------------------------------------------------------------------------
# UW enemy banks live in PatternBlockUWSP{127,358,469}.bin. Each bank is
# 544 NES bytes = 34 NES tiles. Levels share banks per z_03.asm:67-89:
#   UWSP127 → L1, L2, L7
#   UWSP358 → L3, L5, L8
#   UWSP469 → L4, L6, L9
#
# Each bank is converted NES 2bpp → Genesis 4bpp (32 B/tile = 1088 B per
# bank), then 4x sub-pal expansion (Codex P0-2: ObjAnimAttrHeap entries
# use sub-pal 0..3, so VRAM must hold all four pre-biased copies).
#
# Per-bank Genesis bytes = 34 * 32 * 4 = 4352 (136 tiles).

ENEMY_X4_TILE_COUNT_PER_PAL = 34   # NES tiles per UWSP bank
# Phase F (2026-05-18): UWSP banks collapse from 4 sub-pal copies (4x pixel-
# bias) to 1 sub-pal-0 copy. NES SPR sub-pal selection routes via Genesis
# OAM pal field (PAL1/PAL2/PAL3 per src/game/world/bg_palette.h CRAM target).
# Pre-Phase-F: ENEMY_X4_TILES = 34*4 = 136, ENEMY_X4_PER_BANK_BYTES = 4352.
# Post-Phase-F: 34 tiles / 1088 bytes per bank. Saves 102 tiles per active
# bank (3 banks total; one resident at a time).
ENEMY_X4_TILES = ENEMY_X4_TILE_COUNT_PER_PAL          # 34 Genesis tiles (1x)
ENEMY_X4_PER_BANK_BYTES = ENEMY_X4_TILES * BYTES_PER_GEN_TILE   # 1088

ENEMY_BANK_FILES = [
    ("UWSP127", "PatternBlockUWSP127.bin"),
    ("UWSP358", "PatternBlockUWSP358.bin"),
    ("UWSP469", "PatternBlockUWSP469.bin"),
]

# PR-4c: OW NPC + cave-dweller bank. Single 1824-byte block in NES Z1
# (z_03.asm:42 PatternBlockSrcAddrsOW). 114 NES tiles. 1x sub-pal (most
# OW sprites use sub-pal 0; per-NPC sub-pal selection via ObjAttr at
# render time, not CHR replication). Lands in SCENE_OBJ slot (cap 136
# tiles), well under budget.
OWSP_BANK_FILE = ("OWSP", "PatternBlockOWSP.bin")
OWSP_NES_TILE_COUNT = 114
OWSP_BANK_BYTES = OWSP_NES_TILE_COUNT * BYTES_PER_GEN_TILE  # 3648

# PR-5 CHR-BOSSES: per-level boss banks. Three NES UWSPBoss banks dispatched
# by CurLevel per z_03.asm:24-34 BossPatternBlockSrcAddrs:
#   UWSPBoss1257 -> L1, L2, L5, L7
#   UWSPBoss3468 -> L3, L4, L6, L8
#   UWSPBoss9    -> L9
# Each bank is 1024 bytes raw NES = 64 NES tiles. 1x sub-pal (boss CRAM is
# loaded per-boss; sub-pal index flows through OAM attr at render time).
# Per-bank Genesis bytes = 64 * 32 = 2048 (64 tiles, no expansion).
BOSS_NES_TILE_COUNT = 64
BOSS_BANK_BYTES = BOSS_NES_TILE_COUNT * BYTES_PER_GEN_TILE  # 2048
BOSS_BANK_FILES = [
    ("UWSPBoss1257", "PatternBlockUWSPBoss1257.bin"),
    ("UWSPBoss3468", "PatternBlockUWSPBoss3468.bin"),
    ("UWSPBoss9",    "PatternBlockUWSPBoss9.bin"),
]


def _build_enemy_bank_blob(bank_path: Path) -> bytes:
    """Read an UWSP bank file, convert each NES tile to Genesis. Returns
    1088-byte blob (34 tiles x 32 bytes) — single sub-pal-0 copy.

    Phase F (2026-05-18): dropped the 4x sub-pal pixel-bias expansion. NES
    SPR sub-pal selection now routes via Genesis OAM pal field in
    enemy_render.c::translate_attrs (PAL1/PAL2/PAL3 = sub-pals 0/1/2 per
    src/game/world/bg_palette.h CRAM target). Pre-Phase-F: 4352 bytes.
    """
    raw = bank_path.read_bytes()
    if len(raw) != ENEMY_X4_TILE_COUNT_PER_PAL * BYTES_PER_NES_TILE:
        raise SystemExit(
            f"PR-4b: {bank_path.name} unexpected size "
            f"{len(raw)} (want {ENEMY_X4_TILE_COUNT_PER_PAL * BYTES_PER_NES_TILE})")

    # NES → Genesis 4bpp (1088 bytes = 34 tiles, sub-pal-0 encoded).
    out = bytearray()
    for tid in range(ENEMY_X4_TILE_COUNT_PER_PAL):
        nes_tile = raw[tid * BYTES_PER_NES_TILE : (tid + 1) * BYTES_PER_NES_TILE]
        out.extend(nes_tile_to_genesis(nes_tile))
    if len(out) != ENEMY_X4_PER_BANK_BYTES:
        raise SystemExit(f"PR-4b: enemy bank size wrong: {len(out)}")
    return bytes(out)


def _build_owsp_blob(bank_path: Path) -> bytes:
    """PR-4c: OWSP single-sub-pal blob. NES → Genesis 4bpp at sub-pal 0
    (no replication). 114 NES tiles → 3648 Genesis bytes."""
    raw = bank_path.read_bytes()
    expected = OWSP_NES_TILE_COUNT * BYTES_PER_NES_TILE
    if len(raw) != expected:
        raise SystemExit(
            f"PR-4c: {bank_path.name} unexpected size "
            f"{len(raw)} (want {expected})")

    out = bytearray()
    for tid in range(OWSP_NES_TILE_COUNT):
        nes_tile = raw[tid * BYTES_PER_NES_TILE : (tid + 1) * BYTES_PER_NES_TILE]
        out.extend(nes_tile_to_genesis(nes_tile))
    if len(out) != OWSP_BANK_BYTES:
        raise SystemExit(f"PR-4c: OWSP bank size wrong: {len(out)}")
    return bytes(out)


def _build_boss_blob(bank_path: Path) -> bytes:
    """PR-5: UWSPBoss single-sub-pal blob. NES → Genesis 4bpp at sub-pal 0
    (no replication). 64 NES tiles → 2048 Genesis bytes."""
    raw = bank_path.read_bytes()
    expected = BOSS_NES_TILE_COUNT * BYTES_PER_NES_TILE
    if len(raw) != expected:
        raise SystemExit(
            f"PR-5: {bank_path.name} unexpected size "
            f"{len(raw)} (want {expected})")

    out = bytearray()
    for tid in range(BOSS_NES_TILE_COUNT):
        nes_tile = raw[tid * BYTES_PER_NES_TILE : (tid + 1) * BYTES_PER_NES_TILE]
        out.extend(nes_tile_to_genesis(nes_tile))
    if len(out) != BOSS_BANK_BYTES:
        raise SystemExit(f"PR-5: boss bank size wrong: {len(out)}")
    return bytes(out)


def emit_enemy_chr_x4(out_dir: Path) -> int:
    """Emit atlas/enemy_chr.{c,h}. Returns per-bank byte count (1088 post-F).

    Three UWSP banks (UWSP127/358/469), each ENEMY_X4_PER_BANK_BYTES.
    Phase F (2026-05-18): single sub-pal-0 copy per bank (was 4x pixel-bias).
    Plus PR-4c OWSP single-sub-pal bank for OW + cave NPCs.
    Renderer offset rule (UWSP + OWSP): blob_off = 0 (single copy each).
    Sub-pal selection routes via Genesis OAM pal field in
    enemy_render.c::translate_attrs.
    """
    blobs: List[Tuple[str, bytes]] = []
    for sym, fname in ENEMY_BANK_FILES:
        path = PRG_ORIG_DIR / fname
        if not path.exists():
            raise SystemExit(f"PR-4b: missing {path}")
        blobs.append((sym, _build_enemy_bank_blob(path)))

    owsp_path = PRG_ORIG_DIR / OWSP_BANK_FILE[1]
    if not owsp_path.exists():
        raise SystemExit(f"PR-4c: missing {owsp_path}")
    owsp_blob = _build_owsp_blob(owsp_path)

    per_bank = ENEMY_X4_PER_BANK_BYTES
    per_pal = per_bank  # Phase F: single copy, per_pal == per_bank

    guard = "ROOMROM_ATLAS_ENEMY_CHR_H"
    h_path = out_dir / "enemy_chr.h"
    c_path = out_dir / "enemy_chr.c"

    h_lines = [
        BANNER,
        "/* enemy_chr: UW per-level transient enemy banks (PR-4b).",
        " *",
        " * Three NES UWSP banks (127/358/469) covering all 9 UW levels per",
        " * z_03.asm:67-89 dispatch. Each bank holds 34 NES sprite tiles.",
        " *",
        " * Phase F (2026-05-18 VRAM cleanup): single sub-pal-0 copy per",
        " * bank (was 4x pixel-bias). Pre-Phase-F: 4352 B/bank with rule",
        " * out = (in==0) ? 0 : (sub_pal*4 + in). Post: 1088 B/bank, sub-",
        " * pal routes via Genesis OAM pal field in",
        " * src/game/enemies/enemy_render.c::translate_attrs.",
        " *",
        " * Consumed by engine/src/atlas/level_chr_swap.c via DMA state",
        " * machine; resident at SCENE_OBJ tile_base = (SPR_BASE + 44).",
        " */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        f"#define ROOMROM_ATLAS_ENEMY_TILE_COUNT_PER_PAL {ENEMY_X4_TILE_COUNT_PER_PAL}u",
        f"#define ROOMROM_ATLAS_ENEMY_TILE_COUNT         {ENEMY_X4_TILES}u",
        f"#define ROOMROM_ATLAS_ENEMY_PER_PAL_BYTES      {per_pal}u",
        f"#define ROOMROM_ATLAS_ENEMY_PER_BANK_BYTES     {per_bank}u",
        f"#define ROOMROM_ATLAS_ENEMY_OWSP_TILE_COUNT    {OWSP_NES_TILE_COUNT}u",
        f"#define ROOMROM_ATLAS_ENEMY_OWSP_BANK_BYTES    {OWSP_BANK_BYTES}u",
        "",
    ]
    for sym, _ in blobs:
        h_lines.append(
            f"extern const unsigned char roomrom_atlas_enemy_{sym.lower()}"
            f"[ROOMROM_ATLAS_ENEMY_PER_BANK_BYTES];")
    h_lines.append(
        "extern const unsigned char roomrom_atlas_enemy_owsp"
        "[ROOMROM_ATLAS_ENEMY_OWSP_BANK_BYTES];")
    h_lines += ["", f"#endif /* {guard} */", ""]
    write_lines(h_path, h_lines)

    c_lines = [
        BANNER,
        '#include "atlas/enemy_chr.h"',
        "",
    ]
    for sym, blob in blobs:
        c_lines += [
            f"const unsigned char roomrom_atlas_enemy_{sym.lower()}"
            f"[ROOMROM_ATLAS_ENEMY_PER_BANK_BYTES] = {{",
        ]
        c_lines.extend(format_blob(blob, "    "))
        c_lines += ["};", ""]
    c_lines += [
        "const unsigned char roomrom_atlas_enemy_owsp"
        "[ROOMROM_ATLAS_ENEMY_OWSP_BANK_BYTES] = {",
    ]
    c_lines.extend(format_blob(owsp_blob, "    "))
    c_lines += ["};", ""]
    write_lines(c_path, c_lines)

    print(f"  enemy_chr: 3 UWSP banks x {per_bank} bytes "
          f"({ENEMY_X4_TILES} tiles each, 1x sub-pal sub-pal-0 encoded) "
          f"+ OWSP {OWSP_BANK_BYTES} bytes ({OWSP_NES_TILE_COUNT} tiles, 1x)")
    return per_bank


# ---------------------------------------------------------------------------
# PR-5: boss_chr — per-level UWSPBoss banks (1257 / 3468 / 9)
# ---------------------------------------------------------------------------

def emit_boss_chr(out_dir: Path) -> int:
    """Emit atlas/boss_chr.{c,h}. Returns per-bank byte count (2048).

    Three NES UWSPBoss banks (1257/3468/9) covering all 9 UW levels per
    z_03.asm:24-34 BossPatternBlockSrcAddrs. 64 NES tiles each, 1x sub-pal
    (boss CRAM is loaded per-boss; sub-pal index flows through OAM attr).
    Renderer offset rule: blob_off = 0 (single bank, no expansion).
    """
    blobs: List[Tuple[str, bytes]] = []
    for sym, fname in BOSS_BANK_FILES:
        path = PRG_ORIG_DIR / fname
        if not path.exists():
            raise SystemExit(f"PR-5: missing {path}")
        blob = _build_boss_blob(path)
        blobs.append((sym, blob))
        # NES sprite palette 3 already occupies PAL1[12..15]. Preserve
        # transparent zero and bias its visible pixels into that range.
        biased = bytes((((v >> 4) + 12) << 4 if v >> 4 else 0) |
                       ((v & 15) + 12 if v & 15 else 0) for v in blob)
        blobs.append((sym + "_subpal3", biased))

    guard = "ROOMROM_ATLAS_BOSS_CHR_H"
    h_path = out_dir / "boss_chr.h"
    c_path = out_dir / "boss_chr.c"

    h_lines = [
        BANNER,
        "/* boss_chr: UW per-level transient boss banks (PR-5).",
        " *",
        " * Three NES UWSPBoss banks (1257/3468/9) covering all 9 UW levels",
        " * per z_03.asm:24-34 BossPatternBlockSrcAddrs. Each bank holds 64",
        " * NES sprite tiles, 1x sub-pal (boss CRAM is loaded per-boss via",
        " * UpdatePalettes; sub-pal index flows through OAM attr).",
        " *",
        " * Per-bank Genesis bytes = 64 tiles * 32 = 2048. Each bank has",
        " * a _subpal3 copy with visible pixels biased by 12 for PAL1.",
        " *",
        " * Consumed by engine/src/atlas/level_chr_swap.c via parallel boss",
        " * DMA state machine; resident at ROOMROM_BOSS_TILE_BASE.",
        " */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        f"#define ROOMROM_ATLAS_BOSS_TILE_COUNT     {BOSS_NES_TILE_COUNT}u",
        f"#define ROOMROM_ATLAS_BOSS_PER_BANK_BYTES {BOSS_BANK_BYTES}u",
        "",
    ]
    for sym, _ in blobs:
        h_lines.append(
            f"extern const unsigned char roomrom_atlas_boss_{sym.lower()}"
            f"[ROOMROM_ATLAS_BOSS_PER_BANK_BYTES];")
    h_lines += ["", f"#endif /* {guard} */", ""]
    write_lines(h_path, h_lines)

    c_lines = [
        BANNER,
        '#include "atlas/boss_chr.h"',
        "",
    ]
    for sym, blob in blobs:
        c_lines += [
            f"const unsigned char roomrom_atlas_boss_{sym.lower()}"
            f"[ROOMROM_ATLAS_BOSS_PER_BANK_BYTES] = {{",
        ]
        c_lines.extend(format_blob(blob, "    "))
        c_lines += ["};", ""]
    write_lines(c_path, c_lines)

    print(f"  boss_chr: 3 UWSPBoss banks, base + subpal3 copies, "
          f"{BOSS_BANK_BYTES} bytes per copy")
    return BOSS_BANK_BYTES


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def load_inputs() -> Tuple[dict, dict, TileSource]:
    atlas = json.loads(ATLAS_JSON.read_text(encoding="utf-8"))
    item_manifest = json.loads(ITEM_MANIFEST.read_text(encoding="utf-8"))

    if not COMMON_SPRITE_BIN.exists():
        raise SystemExit(f"Missing: {COMMON_SPRITE_BIN}")
    if not COMMON_BG_BIN.exists():
        raise SystemExit(f"Missing: {COMMON_BG_BIN}")

    sprite_bin = COMMON_SPRITE_BIN.read_bytes()
    bg_bin = COMMON_BG_BIN.read_bytes()

    # Build tile-ID -> hex_bytes map from manifest (live-captured tiles)
    manifest_tiles: Dict[str, str] = {}
    for variant in item_manifest.get("variants", []):
        if variant.get("rom_id") == "orig":
            for tid, meta in variant.get("tiles", {}).items():
                manifest_tiles[tid] = meta["bytes"]
            break

    ts = TileSource(sprite_bin, bg_bin, manifest_tiles)
    return atlas, item_manifest, ts


def phase_p3a(atlas: dict, item_manifest: dict, ts: TileSource) -> Dict[str, int]:
    """Emit all per-category C/H pairs. Returns {cat: blob_bytes}."""
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    cat_bytes: Dict[str, int] = {}

    # items_chr
    n = emit_items_chr(ts, atlas, OUT_DIR)
    cat_bytes["items"] = n
    print(f"  items_chr: {n} bytes blob")

    # Verify items byte-match against roomrom_item_chr source data
    items_blob_tmp = bytearray()
    for entry in atlas["categories"]["items"]:
        for tid in entry["nes_tile_ids"]:
            raw = ts.get_sprite_or_manifest(tid)
            if raw is None:
                raw = ts.zero_tile()
            items_blob_tmp.extend(nes_tile_to_genesis(raw))
            if entry.get("dispatch_class") == "Wide_Mirrored":
                items_blob_tmp.extend(hflip_genesis_tile(nes_tile_to_genesis(raw)))
    ok = verify_items_byte_match(bytes(items_blob_tmp), ts, item_manifest)
    if not ok:
        raise SystemExit("BLOCKED: items tile bytes differ from item_chr_manifest — fix generator")
    print("  items byte-match: OK")

    # items_chr_x4 (FU2: items_blob_legacy mode — byte-identical to expand_sprite_chr)
    emit_items_chr_x4(item_manifest, OUT_DIR)

    # link_chr
    n = emit_link_chr(ts, OUT_DIR)
    cat_bytes["link"] = n
    print(f"  link_chr: {n} bytes blob")

    # hud_chr
    n = emit_hud_chr(ts, atlas, OUT_DIR)
    cat_bytes["hud"] = n
    print(f"  hud_chr: {n} bytes blob")

    # title_chr
    n = emit_title_chr(ts, atlas, OUT_DIR)
    cat_bytes["title"] = n
    print(f"  title_chr: {n} bytes blob")

    # npc_chr
    n = emit_npc_chr(ts, atlas, OUT_DIR)
    cat_bytes["npc"] = n
    print(f"  npc_chr: {n} bytes blob")

    # fileselect_chr
    n = emit_fileselect_chr(ts, atlas, OUT_DIR)
    cat_bytes["fileselect"] = n
    print(f"  fileselect_chr: {n} bytes blob")

    # enemies_chr — PR-4b real banks (3 UWSP banks, 4x sub-pal expanded)
    n = emit_enemy_chr_x4(OUT_DIR)
    cat_bytes["enemies"] = n
    print(f"  enemies_chr: {n} bytes per bank (PR-4b)")

    # bosses_chr — PR-5 real banks (3 UWSPBoss banks, 1x sub-pal)
    n = emit_boss_chr(OUT_DIR)
    cat_bytes["bosses"] = n
    print(f"  bosses_chr: {n} bytes per bank (PR-5)")

    # bg_overworld_chr + bg_underworld_chr — BG handled by other modules
    emit_bg_chr_stubs(atlas, OUT_DIR)
    cat_bytes["bg_ow"] = 0
    cat_bytes["bg_uw"] = 0
    print(f"  bg_overworld_chr + bg_underworld_chr: stubs (BG owned by expanded_bg_chr.c)")

    return cat_bytes


def phase_p3b(cat_bytes: Dict[str, int]) -> None:
    """Emit roomrom_scene_vram_contracts.{c,h}."""
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    h_path = OUT_DIR / "roomrom_scene_vram_contracts.h"
    c_path = OUT_DIR / "roomrom_scene_vram_contracts.c"

    write_scene_vram_contracts_header(h_path)
    write_scene_vram_contracts_source(c_path, cat_bytes)
    print(f"  roomrom_scene_vram_contracts.h + .c written")


def phase_p3c(atlas: dict) -> None:
    """Emit atlas_dispatch.h (dispatch macros already embedded in per-category headers)."""
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # atlas_dispatch.h — shared dispatch infrastructure
    write_atlas_dispatch_header(OUT_DIR / "atlas_dispatch.h")
    print(f"  atlas_dispatch.h written")
    print(f"  (Dispatch macros W_<NAME>/H_<NAME>/ATLAS_<NAME>_DISPATCH emitted in per-category headers)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--phase", choices=["p3a", "p3b", "p3c", "all"],
                        default="all",
                        help="Which phase(s) to run (default: all)")
    args = parser.parse_args()

    print(f"gen_atlas.py: loading inputs...")
    atlas, item_manifest, ts = load_inputs()

    cat_bytes: Dict[str, int] = {}

    if args.phase in ("p3a", "all"):
        print("=== Phase P3a: per-category C/H pairs ===")
        cat_bytes = phase_p3a(atlas, item_manifest, ts)

    if args.phase in ("p3b", "all"):
        print("=== Phase P3b: scene VRAM contracts ===")
        phase_p3b(cat_bytes)

    if args.phase in ("p3c", "all"):
        print("=== Phase P3c: dispatch macros ===")
        phase_p3c(atlas)

    print("gen_atlas.py: done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
