#!/usr/bin/env python3
"""Phase J Step 2 (2026-05-18 VRAM cleanup): emit sparse BG atlas.

Reads:
  data/chr/common.c              -> common_chr[7616]
  data/chr/overworld_bg.c        -> overworld_bg_chr[4160]
  data/chr/underworld_bg.c       -> underworld_bg_chr[4160]
  RoomRom/src/redux_overworld_bg.c -> redux_overworld_bg_chr[4160]
  + audit_per_tile_subpal.py (per-tile (tile_id, sub_pal) usage)

Emits:
  RoomRom/src/bg_sparse_chr.c
  RoomRom/src/bg_sparse_chr.h

Each (tile_id, sub_pal) combo USED by any UW or OW room produces ONE
Genesis tile in the flat atlas (pixel-bias encoded per current rule).
LUT maps NES (tile_id, sub_pal) -> sparse tile slot; 0xFFFF sentinel
for unused combos.

3 variants emitted:
  bg_sparse_chr_orig_ow  — common + overworld_bg
  bg_sparse_chr_orig_uw  — common + underworld_bg
  bg_sparse_chr_redux_ow — common + redux_overworld_bg

Per §36.1 MF1 decision: variant-specific atlases share the same LUT
(tile_id -> slot mapping is universal); content differs per scene
context. Upload path picks which variant blob to upload at scene init.

Legacy expanded_bg_chr.{c,h} stays in place. Renderer switch is
Phase J Step 3 (not this commit).
"""
from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

# Defer import of audit until after sys.path setup
from tools.probes.audit_per_tile_subpal import (  # noqa: E402
    parse_uw_blob, collect_uw_per_tile_subpals,
    parse_ow_blob, collect_ow_per_tile_subpals,
)

# NES Z_05.asm DoorFaceTiles{E,W,S,N}: 5 sets x 12 tiles per direction.
DOOR_FACE_TILES = {
    "E": [0x88, 0x74, 0x8A, 0x24, 0x87, 0x87, 0x75, 0x89, 0x24, 0x8B, 0x87, 0x87,
          0x88, 0xA4, 0x8A, 0xA6, 0x87, 0x87, 0xA5, 0x89, 0xA7, 0x8B, 0x87, 0x87,
          0x88, 0xAC, 0x8A, 0xAE, 0x87, 0x87, 0xAD, 0x89, 0xAF, 0x8B, 0x87, 0x87,
          0xDF, 0xDF, 0xDF, 0xDF, 0xF5, 0xF5, 0xDF, 0xDF, 0xDF, 0xDF, 0xF5, 0xF5,
          0xDF, 0x24, 0xDF, 0x92, 0xF5, 0xF5, 0x24, 0xDF, 0x93, 0xDF, 0xF5, 0xF5],
    "W": [0x82, 0x82, 0x83, 0x24, 0x85, 0x76, 0x82, 0x82, 0x24, 0x84, 0x77, 0x86,
          0x82, 0x82, 0x83, 0xA0, 0x85, 0xA2, 0x82, 0x82, 0xA1, 0x84, 0xA3, 0x86,
          0x82, 0x82, 0x83, 0xAC, 0x85, 0xAE, 0x82, 0x82, 0xAD, 0x84, 0xAF, 0x86,
          0xF5, 0xF5, 0xDE, 0xDE, 0xDE, 0xDE, 0xF5, 0xF5, 0xDE, 0xDE, 0xDE, 0xDE,
          0xF5, 0xF5, 0xDE, 0x90, 0xDE, 0x24, 0xF5, 0xF5, 0x91, 0xDE, 0x24, 0xDE],
    "S": [0x7E, 0x7F, 0x7D, 0x76, 0x24, 0x7D, 0x74, 0x24, 0x7D, 0x80, 0x81, 0x7D,
          0x7E, 0x7F, 0x7D, 0x9C, 0x9D, 0x7D, 0x9E, 0x9F, 0x7D, 0x80, 0x81, 0x7D,
          0x7E, 0x7F, 0x7D, 0xA8, 0xA9, 0x7D, 0xAA, 0xAB, 0x7D, 0x80, 0x81, 0x7D,
          0xDD, 0xDD, 0xF5, 0xDD, 0xDD, 0xF5, 0xDD, 0xDD, 0xF5, 0xDD, 0xDD, 0xF5,
          0xDD, 0xDD, 0xF5, 0x24, 0x8E, 0xF5, 0x24, 0x8F, 0xF5, 0xDD, 0xDD, 0xF5],
    "N": [0x78, 0x79, 0x7A, 0x78, 0x24, 0x77, 0x78, 0x24, 0x75, 0x78, 0x7B, 0x7C,
          0x78, 0x79, 0x7A, 0x78, 0x98, 0x99, 0x78, 0x9A, 0x9B, 0x78, 0x7B, 0x7C,
          0x78, 0x79, 0x7A, 0x78, 0xA8, 0xA9, 0x78, 0xAA, 0xAB, 0x78, 0x7B, 0x7C,
          0xF5, 0xDC, 0xDC, 0xF5, 0xDC, 0xDC, 0xF5, 0xDC, 0xDC, 0xF5, 0xDC, 0xDC,
          0xF5, 0xDC, 0xDC, 0xF5, 0x8C, 0x24, 0xF5, 0x8D, 0x24, 0xF5, 0xDC, 0xDC],
}


def door_face_cells(direction):
    """Play-area (col, row) for the 12 face tiles in LayOutDoors copy order:
    E/W: 3 cols x 2 rows per half, second half 2 rows lower; N/S: 2 cols x
    3 rows per half, second half 2 cols right."""
    col0, row0 = {"E": (28, 9), "W": (1, 9), "S": (14, 18), "N": (14, 1)}[direction]
    cells = []
    for half in (0, 1):
        if direction in "EW":
            for c in range(3):
                for r in range(2):
                    cells.append((col0 + c, row0 + 2 * half + r))
        else:
            for c in range(2):
                for r in range(3):
                    cells.append((col0 + 2 * half + c, row0 + r))
    return cells


def uw_door_face_combos(attr):
    """(tile, sub_pal) for every face set of every door of one UW room."""
    out = []
    for direction, tiles in DOOR_FACE_TILES.items():
        cells = door_face_cells(direction)
        for k, tid in enumerate(tiles):
            col, row = cells[k % 12]
            nt_row = row + 8
            at_idx = ((nt_row >> 2) << 3) | (col >> 2)
            shift = (((nt_row >> 1) & 1) << 2) | (((col >> 1) & 1) << 1)
            out.append((tid, (attr[at_idx & 0x3F] >> shift) & 0x03))
    return out


ROOMROM = ROOT / "RoomRom"
OUT_C = ROOMROM / "src" / "bg_sparse_chr.c"
OUT_H = ROOMROM / "src" / "bg_sparse_chr.h"

BYTES_PER_TILE = 32

# Source array byte offsets for NES tile ranges (per extract_chr.py:551-554).
# common_chr structure (7616 B = 238 tiles) — layout is SPR + BG + Misc:
#   bytes    0..3583  = SPR section  (112 tiles, NES tile ids $70..$DF SPR-side)
#   bytes 3584..7167  = BG section   (112 tiles, NES tile ids $00..$6F BG-side)
#   bytes 7168..7615  = Misc section (14 tiles,  NES tile ids $F2..$FF)
# overworld_bg_chr / underworld_bg_chr / redux_overworld_bg_chr:
#   tiles 0x70..0xF1  -> bytes (tile_id - 0x70) * 32 (130 tiles, 4160 B)
COMMON_BG_END  = 0x70      # tiles below this come from common_chr BG section
COMMON_MISC_START = 0xF2   # tiles >= this come from common_chr misc section
COMMON_BG_BYTE_OFFSET   = 3584   # BG section starts at byte 3584 in common_chr
                                  # (after 112-tile SPR section = 3584 B).
                                  # See data/chr/common.c layout from
                                  # tools/extract_chr.py:554 — common_data =
                                  # common_sp_data + common_bg_data + common_misc_data.
COMMON_MISC_BYTE_OFFSET = 7168
SCENE_BG_START = 0x70
SCENE_BG_END   = 0xF2      # exclusive: 0x70..0xF1 = scene-specific
SCENE_BG_BYTES = 4160      # 130 tiles * 32


def fail(msg):
    print(f"gen_bg_sparse: FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def parse_array(text, name):
    pat = re.compile(
        r"const\s+unsigned\s+char\s+" + re.escape(name) +
        r"\s*\[\s*(\d+)\s*\]\s*=\s*\{([^}]*)\}",
        re.MULTILINE | re.DOTALL,
    )
    m = pat.search(text)
    if not m:
        return None
    size = int(m.group(1))
    body = m.group(2)
    bytes_out = [int(t.group(1), 16) for t in re.finditer(r"0x([0-9A-Fa-f]{1,2})", body)]
    if len(bytes_out) != size:
        fail(f"{name}: declared {size} != parsed {len(bytes_out)}")
    return bytes(bytes_out)


def load_source(rel_path, name):
    path = ROOT / rel_path
    if not path.exists():
        fail(f"{rel_path}: not found")
    text = path.read_text(encoding="utf-8")
    data = parse_array(text, name)
    if data is None:
        fail(f"{rel_path}: array `{name}` not found")
    return data


def bias_byte(b, sub_pal):
    """Mirror expand_bg_chr.py:86-91 bias rule (out = 0 if in==0 else s*4+in)."""
    hi = (b >> 4) & 0x0F
    lo = b & 0x0F
    hi_out = 0 if hi == 0 else (sub_pal * 4 + hi)
    lo_out = 0 if lo == 0 else (sub_pal * 4 + lo)
    return ((hi_out & 0x0F) << 4) | (lo_out & 0x0F)


def get_nes_tile_bytes(tile_id, common_chr, scene_bg_chr, unified_source=False):
    """Return 32 raw bytes for a given NES BG tile_id, picking source per range.

    unified_source=True: redux UW path - scene_bg_chr is a flat 256-tile
    bank (8192 B) that REPLACES the entire BG bank. Look up tile_id
    directly in scene_bg_chr without splitting into common/scene/misc."""
    if unified_source:
        off = tile_id * BYTES_PER_TILE
        return scene_bg_chr[off:off + BYTES_PER_TILE]
    if tile_id < COMMON_BG_END:
        # 0x00..0x6F from common_chr BG section (after 112-tile SPR prefix)
        off = COMMON_BG_BYTE_OFFSET + tile_id * BYTES_PER_TILE
        return common_chr[off:off + BYTES_PER_TILE]
    if tile_id >= COMMON_MISC_START:
        # 0xF2..0xFF from common_chr misc section
        off = COMMON_MISC_BYTE_OFFSET + (tile_id - COMMON_MISC_START) * BYTES_PER_TILE
        return common_chr[off:off + BYTES_PER_TILE]
    # 0x70..0xF1 from scene-specific BG
    off = (tile_id - SCENE_BG_START) * BYTES_PER_TILE
    return scene_bg_chr[off:off + BYTES_PER_TILE]


def emit_sparse_blob(per_tile_usage, common_chr, scene_bg_chr, name_for_log,
                     unified_source=False):
    """Returns (flat_bytes, lut_256x4). LUT[tile_id][sub_pal] = slot_index
    (0..N-1) into the flat tile array; 0xFFFF sentinel if combo unused.

    unified_source=True (Phase J.2 redux UW path): scene_bg_chr is a
    flat 256-tile bank that REPLACES the entire BG bank (common +
    scene + misc all from same source)."""
    lut = [[0xFFFF] * 4 for _ in range(256)]
    flat = bytearray()
    slot = 0
    for tile_id in sorted(per_tile_usage.keys()):
        for sub_pal in sorted(per_tile_usage[tile_id]):
            raw = get_nes_tile_bytes(tile_id, common_chr, scene_bg_chr, unified_source)
            if len(raw) != BYTES_PER_TILE:
                fail(f"{name_for_log}: tile 0x{tile_id:02X} short: {len(raw)} B")
            for b in raw:
                flat.append(bias_byte(b, sub_pal))
            if slot >= 0xFFFF:
                fail(f"{name_for_log}: slot overflow at tile 0x{tile_id:02X} sub-pal {sub_pal}")
            lut[tile_id][sub_pal] = slot
            slot += 1
    return bytes(flat), lut


def emit_c_array(f, name, data):
    f.write(f"const unsigned char {name}[{len(data)}] __attribute__((aligned(4))) = {{\n")
    cols = 16
    for i in range(0, len(data), cols):
        chunk = data[i:i + cols]
        f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk))
        if i + cols < len(data):
            f.write(",")
        f.write("\n")
    f.write("};\n\n")


def emit_lut(f, name, lut):
    f.write(f"const unsigned short {name}[256][4] __attribute__((aligned(2))) = {{\n")
    for tile_id in range(256):
        entries = ", ".join(f"0x{v:04X}" for v in lut[tile_id])
        comma = "," if tile_id < 255 else ""
        f.write(f"    {{ {entries} }}{comma}  /* tile 0x{tile_id:02X} */\n")
    f.write("};\n\n")


def main():
    # Load source arrays
    common_chr = load_source("data/chr/common.c", "common_chr")
    overworld_bg_chr = load_source("data/chr/overworld_bg.c", "overworld_bg_chr")
    underworld_bg_chr = load_source("data/chr/underworld_bg.c", "underworld_bg_chr")
    redux_overworld_bg_chr = load_source(
        "RoomRom/src/redux_overworld_bg.c", "redux_overworld_bg_chr")
    # Phase J.2 (2026-05-18): redux UW source for 4th sparse variant.
    # redux_uw_bg_chr is 8192 bytes = 256 NES tiles already Genesis 4bpp.
    # But our get_nes_tile_bytes expects RAW NES 2bpp (16 bytes per tile).
    # For redux UW, source IS Genesis 4bpp already; sparse emit treats it
    # differently — extract as-if-NES (pixel values 0..3 in the 4bpp
    # nibbles for sub-pal 0 already) and apply bias to other sub-pals.
    redux_uw_bg_chr = load_source("RoomRom/src/redux_uw_bg.c", "redux_uw_bg_chr")

    # Run audits (deterministic per Phase J §36.1 MF4)
    uw_rooms = parse_uw_blob()
    ow_bytes = parse_ow_blob()
    per_tile_uw = collect_uw_per_tile_subpals(uw_rooms)
    per_tile_ow = collect_ow_per_tile_subpals(ow_bytes)

    # Combined usage (universal LUT spans UW + OW union)
    combined = defaultdict(set)
    for d in (per_tile_uw, per_tile_ow):
        for tid, sps in d.items():
            combined[tid].update(sps)

    # Force-include HUD tile IDs (Phase J §36.1 MF2: HUD rows 0..6 not
    # covered by room nametable audit). HUD lives on Window plane but
    # references same BG tile bank. Sourced from src/game/hud/hud_runtime.c
    # constants (TILE_*) + StatusBarTransferBufTemplate digits/letters.
    # Sub-pals 0, 1, 2 cover all HUD palette contexts (white/yellow/red).
    HUD_FORCE_TILES = (
        # Digits 0..9 (score, rupee count, key count, bomb count)
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
        # Letters used in StatusBarTransferBufTemplate
        0x0E, 0x0F, 0x12, 0x15,
        # Inventory glyphs + heart pieces
        0x21,           # TILE_LOW_X
        0x24,           # HUD_TILE_SPACE (also walkable sand BG; already covered)
        0x50, 0x51, 0x52,  # TILE_REDUX_HEART_OUTLINE / _MAP_MARKER / _HEART_FILL
        0x62,           # TILE_DASH
        0xF2, 0xF3, 0xF4, 0xF5,  # full/half/empty heart, gray map
    )
    for tid in HUD_FORCE_TILES:
        combined[tid].update([0, 1, 2])  # HUD pal context varies (white/yellow/red)

    # T-114: NES LayOutDoors writes any of 5 door-face sets (open, locked,
    # shutter, bombable wall, bombed hole) into each door's cells at runtime
    # (key used, wall bombed, shutter toggled). Room captures hold only one
    # state, so add every face tile at the sub-pal of the cell it lands on,
    # for every UW room. Tables: Z_05.asm DoorFaceTilesE/W/S/N; cells from
    # PlayAreaDoorFaceAddrs ($67A1/$654F/$6676/$6665) and the LayOutDoors
    # copy order (column-major halves).
    for _room_idx, nt, attr in uw_rooms:
        for tid, sub_pal in uw_door_face_combos(attr):
            combined[tid].add(sub_pal)

    # T-050/T-115: OW squares written only after a secret or a push, which
    # the room-layout audit never sees: stairs $70-$73 (tree / armos /
    # shortcut), the pushed rock $C8-$CB and gravestone $BC-$BF at their
    # new position, cave $24/$F3 and gray floor $26. OW playfields use
    # sub-pals 0, 2 and 3 (LevelBlockAttrsA/B & 3 over all 128 rooms).
    for tid in list(range(0x70, 0x74)) + list(range(0xC8, 0xCC)) \
             + list(range(0xBC, 0xC0)) + [0x24, 0x26, 0xF3]:
        combined[tid].update([0, 2, 3])

    # Original dungeon map transfer records use common misc glyphs absent
    # from room nametables (Z_06 LevelInfo_StatusBarMapTransferBuf).
    for tid in range(0xFB, 0x100):
        combined[tid].add(0)

    # Force-include redux automap tile range (0x30..0x4F, 32 tiles) for HUD
    # automap rendering. Sub-pals 0,1,2 cover the gray/blue/red room state.
    # Original hud_runtime.c::upload_redux_automap_chr handled this via
    # per-pal 4x legacy upload; sparse atlas absorbs it.
    for tid in range(0x30, 0x50):
        combined[tid].update([0, 1, 2])

    # Force-include common_chr BG range used by hud_upload_chr legacy path
    # (NES tiles 0x00..0x15 + 0x20..0x24 + 0x61..0x6E + 0xF7..0xF9 per
    # hud_runtime.c::upload_common_hud_chr). Most already in audit; this
    # is belt-and-suspenders for tiles HUD references but rooms don't.
    for tid in list(range(0x00, 0x16)) + list(range(0x20, 0x25)) \
             + list(range(0x61, 0x6F)) + list(range(0xF7, 0xFA)):
        combined[tid].update([0, 1, 2])

    # P6.2b (2026-05-19): Force-include full A..Z alphabet at $0A..$23 for
    # pause inventory subscreen text labels. Most alphabet tiles aren't
    # used by NES Z1 OW/UW rooms (only specific labels like "LIFE", numbers,
    # dungeon names) so sparse LUT excludes them. Inventory subscreen needs
    # to spell "INVENTORY", "USE B BUTTON FOR THIS", item names, etc — needs
    # ALL letters in atlas under sub-pal 0.
    for tid in range(0x0A, 0x24):  # A..V continues through to $23 ('V')
        combined[tid].update([0])
    # Letters $20..$23 = W X Y Z (already in HUD legacy range above for
    # sub-pals 0/1/2; harmless double-include for sub_pal 0).
    #
    # L4 (Phase 7 v2 2026-05-20): also include sub_pal 1 (red text) for
    # NES "INVENTORY" / "USE B BUTTON FOR THIS" + digits 0-9 (HUD-style)
    # and sub_pal 3 (brown/yellow) for "TRIFORCE" label.
    for tid in range(0x00, 0x24):  # digits + A..V
        combined[tid].update([1, 3])
    # Sprite tile $1E (small white square) — L3 cursor placeholder. NES
    # cursor sprite uses SPRITE pattern table so this isn't strictly a BG
    # sparse atlas concern, but route through PAL0 via sub_pal 1 if any
    # BG-side render of $1E happens.
    combined[0x1E].update([1, 2])
    #
    # V2.1 (2026-05-20): NES subscreen BG tilemap captured tiles. Box frame
    # ($69-$6E), triforce triangle ($E7-$F1, $F5). NES sub-pals per active
    # subscreen PALRAM: box frame = sub_pal 0 (white/blue), triangle =
    # sub_pal 3 (brown/yellow). Force-include at sub_pal 0 (default routing)
    # plus 3 for the brown/yellow tile variants.
    for tid in (0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E):
        combined[tid].update([0])
    # 2026-05-30: live NT2 attribute capture (pause_golden) proves the
    # subscreen triforce is sub-pal 1, NOT 3 — add sub-pal 1 so the menu
    # renders it byte-exact. (Keep 0/3 for other scenes' usage.)
    for tid in (0xE7, 0xE8, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF5):
        combined[tid].update([0, 1, 3])
    # 2026-05-30 UW dungeon map sheet: the full door-permutation glyph range
    # $E2-$F1 + blank $F5 + the level-map tiles $FD/$FE, all sub-pal 1 (live
    # NT2 attribute capture). Needed for the static UW frame AND the G4
    # dynamic map builder (any room can show any of the 16 door glyphs).
    for tid in range(0xE2, 0xF2):
        combined[tid].update([1])
    for tid in (0xFD, 0xFE):
        combined[tid].update([1])

    # Cave / NPC dialogue PUNCTUATION glyphs. The textbox char-streamer
    # (src/game/cave/cave_dispatch.c, transfer_buf_drain.c) writes NES
    # char codes straight to the BG nametable; a char code IS its BG font
    # tile id. PersonText blobs (src/data/person_text_data.c) use
    # punctuation $28 '(' , $29 '!', $2A ''' (apostrophe), $2C ',',
    # $2E '.', $2F '/' — none of which appear in room nametables, so the
    # usage audit excluded them and the sparse LUT returned 0xFFFF ->
    # blank. Result: "IT S DANGEROUS" instead of "IT'S DANGEROUS". Force-
    # include at sub-pal 0 (the fixed text routing in emit_nametable_record).
    for tid in (0x28, 0x29, 0x2A, 0x2C, 0x2E, 0x2F):
        combined[tid].update([0])

    # Emit per-variant blobs against COMBINED usage (so LUT is universal)
    orig_ow_blob, orig_ow_lut = emit_sparse_blob(
        combined, common_chr, overworld_bg_chr, "orig_ow")
    orig_uw_blob, orig_uw_lut = emit_sparse_blob(
        combined, common_chr, underworld_bg_chr, "orig_uw")
    redux_ow_blob, redux_ow_lut = emit_sparse_blob(
        combined, common_chr, redux_overworld_bg_chr, "redux_ow")
    # Phase J.2 (2026-05-18): 4th variant for redux UW. Same combined
    # tile-usage manifest -> universal LUT. Source bytes from
    # redux_uw_bg_chr replace underworld_bg_chr in the scene-BG range
    # (0x70..0xF1); common_chr stays for 0x00..0x6F + 0xF2..0xFF.
    redux_uw_blob, redux_uw_lut = emit_sparse_blob(
        combined, common_chr, redux_uw_bg_chr, "redux_uw",
        unified_source=True)

    # All four LUTs should be IDENTICAL (slot allocation is variant-invariant)
    assert orig_ow_lut == orig_uw_lut == redux_ow_lut == redux_uw_lut, \
        "LUT divergence - variant-invariant slot allocation expected"

    n_tiles = sum(len(s) for s in combined.values())
    bytes_per_variant = n_tiles * BYTES_PER_TILE
    print(f"  combined unique (tile_id, sub_pal) combos: {n_tiles}")
    print(f"  per-variant blob: {bytes_per_variant} bytes ({n_tiles} tiles)")
    print(f"  ROM total: 4 variants x {bytes_per_variant} B + 1 LUT (256x4x2 = 2048 B) "
          f"= {4 * bytes_per_variant + 2048} bytes")
    legacy_bytes = 3 * 7616 * 4 + 4 * 4160 * 4 + 1024 * 4  # rough estimate
    print(f"  (legacy x4 estimated: ~{legacy_bytes // 1024} KB)")

    # Emit header
    with OUT_H.open("w", encoding="utf-8") as f:
        f.write("/* Auto-generated by RoomRom/tools/gen_bg_sparse.py. Do not edit. */\n")
        f.write("#ifndef ROOMROM_BG_SPARSE_CHR_H\n")
        f.write("#define ROOMROM_BG_SPARSE_CHR_H\n\n")
        f.write("/* Phase J sparse BG atlas: per-variant flat tile array indexed by\n"
                " * bg_sparse_tile_lut[nes_tile_id][nes_sub_pal] -> slot index (0..N-1).\n"
                " * Sentinel 0xFFFF = (tile_id, sub_pal) never referenced by any room.\n"
                " * Each variant's blob is bias-encoded (out=(in==0)?0:(s*4+in)) so the\n"
                " * tile renders correctly via PAL0 pixel-bias when looked up at its slot.\n"
                " *\n"
                " * Phase J.2 (2026-05-18): 4 variants — orig+redux x OW+UW. Redux UW\n"
                " * uses unified_source mode (redux_uw_bg_chr is a flat 256-tile bank\n"
                " * that replaces the entire BG bank, common+scene+misc included). */\n\n")
        f.write(f"#define BG_SPARSE_TILE_COUNT      {n_tiles}u\n")
        f.write(f"#define BG_SPARSE_BLOB_BYTES      {bytes_per_variant}u\n")
        f.write("\n")
        f.write(f"extern const unsigned char  bg_sparse_chr_orig_ow  [{bytes_per_variant}];\n")
        f.write(f"extern const unsigned char  bg_sparse_chr_orig_uw  [{bytes_per_variant}];\n")
        f.write(f"extern const unsigned char  bg_sparse_chr_redux_ow [{bytes_per_variant}];\n")
        f.write(f"extern const unsigned char  bg_sparse_chr_redux_uw [{bytes_per_variant}];\n")
        f.write("extern const unsigned short bg_sparse_tile_lut[256][4];\n")
        f.write("\n#endif /* ROOMROM_BG_SPARSE_CHR_H */\n")

    # Emit source
    with OUT_C.open("w", encoding="utf-8") as f:
        f.write("/* Auto-generated by RoomRom/tools/gen_bg_sparse.py. Do not edit. */\n")
        f.write('#include "bg_sparse_chr.h"\n\n')
        emit_c_array(f, "bg_sparse_chr_orig_ow",  orig_ow_blob)
        emit_c_array(f, "bg_sparse_chr_orig_uw",  orig_uw_blob)
        emit_c_array(f, "bg_sparse_chr_redux_ow", redux_ow_blob)
        emit_c_array(f, "bg_sparse_chr_redux_uw", redux_uw_blob)
        emit_lut(f, "bg_sparse_tile_lut", orig_ow_lut)

    print(f"wrote {OUT_H.relative_to(ROOT)}")
    print(f"wrote {OUT_C.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
