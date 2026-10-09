#!/usr/bin/env python3
"""Hard gate: VRAM tile-bank ranges do not overlap each other or VDP tables.

Reads constants from RoomRom/src/roomrom_vram_map.h and
RoomRom/src/atlas/items_chr_x4.h.

CRITICAL (PR-2b 2026-05-08): RoomRom now uses 64x32 plane mode
(render_mode_set_h64v32 in main.c init_video; VDP reg 16 = $9001).
With BG_A address override -> $C000 and BG_B override -> $E000,
SGDK case-11 layout places VDP tables at:
  plane A  = $C000 (tile 1536)   4 KB
  Window   = $D000 (tile 1664)   4 KB
  plane B  = $E000 (tile 1792)   4 KB
  HScroll  = $F000 (tile 1920)   1 KB
  SAT      = $F400 (tile 1952)   640 B
  free     = $F800-$FFFF         2 KB (unused, reserved for future)

Conservative tile-data ceiling for 64x32 mode: $C000 = tile 1536
(+192 tiles vs 64x64 mode's $A800 limit).

Exit code 0 = pass, 1 = fail.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VRAM_MAP_H    = ROOT / "src" / "roomrom_vram_map.h"
ITEM_CHR_H    = ROOT / "src" / "atlas" / "items_chr_x4.h"
BG_CHR_H      = ROOT / "src" / "bg_sparse_chr.h"

VDP_TABLES = {
    # Actual VRAM table layout per RoomRom/src/main.c init_video (read live
    # 2026-05-18 Phase E.0 verifier alignment). PR-2c chose to SHARE planes
    # A+B at $C000 (BG_B mirrors BG_A during scrolls to avoid color-0 leak
    # through to the other plane). Window is at $E000, NOT $D000 as a prior
    # version of this dict claimed.
    #
    # Region $D000-$DFFF is FREE (4 KB / 128 tiles) — would yield headroom
    # if planes A+B were relocated there and TILE_DATA_LIMIT raised to
    # $D000. That relocation requires updating ROOMROM_PLANE_SHARED_BASE
    # (RoomRom/src/render_adapter_sgdk.c), PLANE_A_BASE
    # (src/sgdk_adapter/render_adapter.c), the init_video VDP_setBG[AB]
    # Address calls, and the _Static_assert in
    # src/state/vram_map_state.h. Deferred to a future cleanup phase.
    "plane_ab": (0xC000, 0xC000 + 0x1000),  # SHARED A+B 4 KB
    "window":   (0xE000, 0xE000 + 0x1000),  # tile 1792..1919 (4 KB)
    "h_scroll": (0xF000, 0xF000 + 0x0400),  # tile 1920..1951 (1 KB allocated)
    "sat":      (0xF400, 0xF400 + 0x0280),  # tile 1952..1971 (640 B)
}

# Phase L (2026-05-19): HScroll mode = HSCROLL_PLANE per RoomRom/src/main.c
# init_video uses SGDK default. HSCROLL_PLANE encoding writes ONE 32-bit
# entry at byte 0 of the table = 4 bytes used out of 1024 allocated.
# Remaining 1020 B (~31 tiles) is unused scratch — adjacent to SAT,
# not addressable as contiguous tile data without rebasing HScroll
# elsewhere. Documented as available SAT-extension headroom.
HSCROLL_TABLE_USED_BYTES = 4
HSCROLL_TABLE_FREE_BYTES = 1024 - HSCROLL_TABLE_USED_BYTES

# Conservative end-of-tile-data limit. Planes A+B sit at $C000 (shared);
# anything past that overlaps and gets clobbered each frame. $D000-$DFFF
# is free but unreachable as contiguous tile data without relocating
# planes (see VDP_TABLES note above).
TILE_DATA_LIMIT_BYTES = 0xC000
TILE_DATA_LIMIT_TILES = TILE_DATA_LIMIT_BYTES // 32  # 1536


def fail(msg):
    print(f"verify_vram_budget: FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def parse_constants():
    text_map  = VRAM_MAP_H.read_text(encoding="utf-8")
    text_item = ITEM_CHR_H.read_text(encoding="utf-8")

    consts = {}

    # --- BG and SPR banks (literal numeric defines in roomrom_vram_map.h) ---
    for name in ("ROOMROM_BG_TILE_BASE",
                 "ROOMROM_BG_TILE_COUNT_PER_PAL",
                 "ROOMROM_BG_SUBPAL_COUNT",
                 "ROOMROM_SPR_TILE_BASE",
                 "ROOMROM_SPR_TILE_COUNT_PER_PAL",
                 "ROOMROM_SPR_SUBPAL_COUNT",
                 "ROOMROM_BOSS_SUBPAL3_TILE_COUNT",
                 "ROOMROM_CLOUD_TILE_BASE", "ROOMROM_CLOUD_TILE_COUNT",
                 "ROOMROM_FIREBALL_TILE_BASE", "ROOMROM_FIREBALL_TILE_COUNT",
                 "ROOMROM_FIREBALL_SUBPAL3_TILE_BASE",
                 "ROOMROM_FIREBALL_SUBPAL3_TILE_COUNT",
                 "ROOMROM_SPARK_SUBPAL3_TILE_BASE",
                 "ROOMROM_SPARK_SUBPAL3_TILE_COUNT",
                 "ROOMROM_SPARK_TILE_BASE", "ROOMROM_SPARK_TILE_COUNT",
                 "ROOMROM_HUD_MARKER_TILE_BASE", "ROOMROM_HUD_MARKER_TILE_COUNT",
                 "ROOMROM_SHIELD_TILE_BASE", "ROOMROM_SHIELD_TILE_COUNT",
                 "ROOMROM_CELLAR_BG_TILE_BASE", "ROOMROM_CELLAR_BG_TILE_COUNT",
                 "ROOMROM_SUBSCREEN_SPRITE_TILE_BASE",
                 "ROOMROM_SUBSCREEN_SPRITE_TILE_COUNT",
                 "ROOMROM_LINK_LIFT_TILE_BASE", "ROOMROM_LINK_LIFT_TILE_COUNT",
                 "ROOMROM_LINK_FLASH3_TILE_BASE", "ROOMROM_LINK_FLASH3_TILE_COUNT",
                 "ROOMROM_LINK_ATTACK_FLASH3_TILE_BASE",
                 "ROOMROM_LINK_ATTACK_FLASH3_TILE_COUNT",
                 "ROOMROM_PT1_PAIR_TILE_BASE", "ROOMROM_PT1_PAIR_TILE_COUNT",
                 "ROOMROM_SUBPAL3_PAIR_TILE_COUNT"):
        m = re.search(rf"#define\s+{name}\s+(\d+)u?", text_map)
        if not m:
            fail(f"missing {name} in {VRAM_MAP_H}")
        consts[name] = int(m.group(1))

    # Check the actual upload size, not only two mutually consistent stale
    # map constants. T-174 grew the atlas while the old gate still passed.
    m = re.search(r"#define\s+BG_SPARSE_TILE_COUNT\s+(\d+)u?",
                  BG_CHR_H.read_text(encoding="utf-8"))
    if not m:
        fail(f"missing BG_SPARSE_TILE_COUNT in {BG_CHR_H}")
    # The reservation is a fixed region (all sprite bases derive from it);
    # the ROM-generated atlas must fit inside it.
    if int(m.group(1)) > consts["ROOMROM_BG_TILE_COUNT_PER_PAL"]:
        fail("generated sparse atlas exceeds the BG reservation")

    # --- ITEM bank ---
    # ROOMROM_ITEM_TILE_BASE = ROOMROM_SPR_TILE_BASE + ROOMROM_SPR_TILE_COUNT_PER_PAL
    # (transitive resolution: read from the header expression, then derive here)
    consts["ROOMROM_ITEM_TILE_BASE"] = (
        consts["ROOMROM_SPR_TILE_BASE"] + consts["ROOMROM_SPR_TILE_COUNT_PER_PAL"]
    )

    # ROOMROM_ITEM_TILE_COUNT_PER_PAL aliases ROOMROM_ATLAS_ITEMS_X4_TILE_COUNT
    # from atlas/items_chr_x4.h (supersedes legacy ROOMROM_ITEM_CHR_TILE_COUNT)
    m = re.search(r"#define\s+ROOMROM_ATLAS_ITEMS_X4_TILE_COUNT\s+(\d+)u?", text_item)
    if not m:
        fail(f"missing ROOMROM_ATLAS_ITEMS_X4_TILE_COUNT in {ITEM_CHR_H}")
    consts["ROOMROM_ITEM_TILE_COUNT_PER_PAL"] = int(m.group(1))

    # ROOMROM_ITEM_SUBPAL_COUNT is a literal numeric define
    m = re.search(r"#define\s+ROOMROM_ITEM_SUBPAL_COUNT\s+(\d+)u?", text_map)
    if not m:
        fail(f"missing ROOMROM_ITEM_SUBPAL_COUNT in {VRAM_MAP_H}")
    consts["ROOMROM_ITEM_SUBPAL_COUNT"] = int(m.group(1))

    # HUD backdrop sprite-strip retired 2026-05-15 — opaque HUD underlay
    # now provided by BG_A tile 0 (PAL0 color 0) via
    # clear_hud_underlay_for_row_base() in RoomRom/src/main.c. The 8 tiles
    # previously reserved at the post-item-bank position are freed back
    # into the headroom budget.

    # --- PR-5 BOSS bank (shares SCENE_OBJ slot inside SPR bank, NES parity
    # per z_03.asm:91 -- boss rooms have no enemies). Verified at the C
    # level via #define ROOMROM_BOSS_TILE_BASE = (SPR_TILE_BASE + 44u);
    # no independent VRAM range to check here. ---

    consts["ROOMROM_SUBPAL3_PAIR_TILE_BASE"] = (
        consts["ROOMROM_ITEM_TILE_BASE"] +
        consts["ROOMROM_ITEM_TILE_COUNT_PER_PAL"] * consts["ROOMROM_ITEM_SUBPAL_COUNT"] +
        consts["ROOMROM_BOSS_SUBPAL3_TILE_COUNT"])
    return consts


def tile_range_bytes(base_tile, tile_count):
    return (base_tile * 32, (base_tile + tile_count) * 32)


def overlaps(a, b):
    return not (a[1] <= b[0] or b[1] <= a[0])


def main():
    c = parse_constants()

    bg_count   = c["ROOMROM_BG_SUBPAL_COUNT"]   * c["ROOMROM_BG_TILE_COUNT_PER_PAL"]
    spr_count  = c["ROOMROM_SPR_SUBPAL_COUNT"]  * c["ROOMROM_SPR_TILE_COUNT_PER_PAL"]
    item_count = c["ROOMROM_ITEM_SUBPAL_COUNT"] * c["ROOMROM_ITEM_TILE_COUNT_PER_PAL"]

    bg_base   = c["ROOMROM_BG_TILE_BASE"]
    spr_base  = c["ROOMROM_SPR_TILE_BASE"]
    item_base = c["ROOMROM_ITEM_TILE_BASE"]

    bg_range   = tile_range_bytes(bg_base,   bg_count)
    spr_range  = tile_range_bytes(spr_base,  spr_count)
    item_range = tile_range_bytes(item_base, item_count)

    # --- BG/SPR existing checks (unchanged) ---

    # SPR base must be >= the address the BG bank reserves. Phase J.2:
    # sparse bank uses SUBPAL_COUNT=1 + TILE_COUNT_PER_PAL=BG_SPARSE_TILE_COUNT.
    # Pre-J this hardcoded 4x sub-pal expansion; now reads actual SUBPAL_COUNT.
    bg_reserve = c["ROOMROM_BG_TILE_BASE"] + c["ROOMROM_BG_SUBPAL_COUNT"] * c["ROOMROM_BG_TILE_COUNT_PER_PAL"]
    if c["ROOMROM_SPR_TILE_BASE"] < bg_reserve:
        fail(f"SPR base {spr_base} < BG reserve {bg_reserve} "
             "(BG bank could collide with SPR after future sub-pal expansion)")

    if overlaps(bg_range, spr_range):
        fail(f"BG range {bg_range} overlaps SPR range {spr_range}")

    if spr_range[1] > TILE_DATA_LIMIT_BYTES:
        fail(f"SPR range end 0x{spr_range[1]:X} exceeds tile-data limit "
             f"0x{TILE_DATA_LIMIT_BYTES:X} (=$C000 in 64x32 mode); tiles would clobber "
             f"VDP table region")

    for name, vdp_range in VDP_TABLES.items():
        if overlaps(bg_range, vdp_range):
            fail(f"BG range {bg_range} collides with VDP {name} {vdp_range}")
        if overlaps(spr_range, vdp_range):
            fail(f"SPR range {spr_range} collides with VDP {name} {vdp_range}")

    # --- ITEM bank checks ---

    # SPR bank end must not exceed ITEM bank start
    spr_end_tile  = spr_base  + spr_count
    item_end_tile = item_base + item_count

    if spr_end_tile > item_base:
        fail(f"SPR bank end tile {spr_end_tile} > ITEM bank start tile {item_base} "
             "(SPR and ITEM banks overlap)")

    if overlaps(bg_range, item_range):
        fail(f"BG range {bg_range} overlaps ITEM range {item_range}")

    if overlaps(spr_range, item_range):
        fail(f"SPR range {spr_range} overlaps ITEM range {item_range}")

    if item_end_tile > TILE_DATA_LIMIT_TILES:
        fail(f"ITEM bank end tile {item_end_tile} exceeds VDP table region start "
             f"tile {TILE_DATA_LIMIT_TILES} (=$C000 in 64x32 mode); ITEM bank would clobber "
             f"VDP table region")

    if item_range[1] > TILE_DATA_LIMIT_BYTES:
        fail(f"ITEM range end 0x{item_range[1]:X} exceeds tile-data limit "
             f"0x{TILE_DATA_LIMIT_BYTES:X} (=$C000 in 64x32 mode); tiles would clobber "
             f"VDP table region")

    for name, vdp_range in VDP_TABLES.items():
        if overlaps(item_range, vdp_range):
            fail(f"ITEM range {item_range} collides with VDP {name} {vdp_range}")

    # HUD backdrop sprite-strip retired 2026-05-15 — its 8-tile bank no
    # longer exists; the tiles previously consumed by it are reported as
    # freed headroom in the success summary below.

    # --- Success summary ---
    boss_subpal3_end = item_end_tile + c["ROOMROM_BOSS_SUBPAL3_TILE_COUNT"]
    if boss_subpal3_end > TILE_DATA_LIMIT_TILES:
        fail(f"Boss palette-3 bank ends at {boss_subpal3_end}, beyond VDP tables")
    banks = {"BG": bg_range, "SPR": spr_range, "ITEM": item_range,
             "BOSS_PAL3": tile_range_bytes(item_end_tile, c["ROOMROM_BOSS_SUBPAL3_TILE_COUNT"])}
    for name in ("SUBSCREEN_SPRITE", "CLOUD", "FIREBALL", "FIREBALL_SUBPAL3", "SPARK", "SPARK_SUBPAL3",
                 "HUD_MARKER", "SHIELD", "CELLAR_BG", "LINK_LIFT", "LINK_FLASH3", "LINK_ATTACK_FLASH3",
                 "PT1_PAIR", "SUBPAL3_PAIR"):
        region = tile_range_bytes(c[f"ROOMROM_{name}_TILE_BASE"], c[f"ROOMROM_{name}_TILE_COUNT"])
        if region[1] > TILE_DATA_LIMIT_BYTES:
            fail(f"{name} extends into VDP tables: {region}")
        for other, occupied in banks.items():
            if overlaps(region, occupied):
                fail(f"{name} overlaps {other}")
        banks[name] = region
    headroom_tiles = TILE_DATA_LIMIT_TILES - max(end for _, end in banks.values()) // 32
    # Phase J.2 (2026-05-18 doc-only): BG bank reserves 1024 tiles for
    # legacy 4x layout (still used by redux UW per uw_render.c branch)
    # but Phase J sparse atlas only fills ~532 tiles. Slots
    # BG_BASE+sparse_size .. SPR_BASE-1 = ~492 tile FREE ZONE within
    # the reserved BG region. Reachable for new content via direct
    # tile-id reference (no LUT mapping needed — addressed by absolute
    # VRAM tile index). Surfacing as "bg_free_zone" for visibility.
    try:
        sparse_h = ROOT / "src" / "bg_sparse_chr.h"
        sparse_text = sparse_h.read_text(encoding="utf-8")
        m = re.search(r"#define\s+BG_SPARSE_TILE_COUNT\s+(\d+)u?", sparse_text)
        bg_sparse_count = int(m.group(1)) if m else 0
        bg_free_zone = bg_count - bg_sparse_count if bg_sparse_count > 0 else 0
    except Exception:
        bg_free_zone = 0
    free_zone_suffix = (
        f"  bg_free_zone={bg_free_zone} tiles (sparse atlas leaves "
        f"BG slots free for direct-tile-id content)" if bg_free_zone > 0 else "")
    print(
        f"verify_vram_budget: OK  "
        f"BG=tiles {bg_base}..{bg_base + bg_count - 1}  "
        f"SPR=tiles {spr_base}..{spr_base + spr_count - 1}  "
        f"ITEM=tiles {item_base}..{item_end_tile - 1}  "
        f"BOSS=SCENE_OBJ-shared (NES parity)  "
        f"BOSS_PAL3=tiles {item_end_tile}..{boss_subpal3_end - 1}  "
        f"CLOUD=tiles {c['ROOMROM_CLOUD_TILE_BASE']}..{c['ROOMROM_CLOUD_TILE_BASE'] + c['ROOMROM_CLOUD_TILE_COUNT'] - 1}  "
        f"FIREBALL=tiles {c['ROOMROM_FIREBALL_TILE_BASE']}..{c['ROOMROM_FIREBALL_TILE_BASE'] + c['ROOMROM_FIREBALL_TILE_COUNT'] - 1}  "
        f"FIREBALL_PAL3=tiles {c['ROOMROM_FIREBALL_SUBPAL3_TILE_BASE']}..{c['ROOMROM_FIREBALL_SUBPAL3_TILE_BASE'] + c['ROOMROM_FIREBALL_SUBPAL3_TILE_COUNT'] - 1}  "
        f"SPARK=tiles {c['ROOMROM_SPARK_TILE_BASE']}..{c['ROOMROM_SPARK_TILE_BASE'] + c['ROOMROM_SPARK_TILE_COUNT'] - 1}  "
        f"SPARK_PAL3=tiles {c['ROOMROM_SPARK_SUBPAL3_TILE_BASE']}..{c['ROOMROM_SPARK_SUBPAL3_TILE_BASE'] + c['ROOMROM_SPARK_SUBPAL3_TILE_COUNT'] - 1}  "
        f"HUD_MARKER=tiles {c['ROOMROM_HUD_MARKER_TILE_BASE']}..{c['ROOMROM_HUD_MARKER_TILE_BASE'] + c['ROOMROM_HUD_MARKER_TILE_COUNT'] - 1}  "
        f"SHIELD=tiles {c['ROOMROM_SHIELD_TILE_BASE']}..{c['ROOMROM_SHIELD_TILE_BASE'] + c['ROOMROM_SHIELD_TILE_COUNT'] - 1}  "
        f"CELLAR_BG=tiles {c['ROOMROM_CELLAR_BG_TILE_BASE']}..{c['ROOMROM_CELLAR_BG_TILE_BASE'] + c['ROOMROM_CELLAR_BG_TILE_COUNT'] - 1}  "
        f"SUBSCREEN_SPRITE=tiles {c['ROOMROM_SUBSCREEN_SPRITE_TILE_BASE']}..{c['ROOMROM_SUBSCREEN_SPRITE_TILE_BASE'] + c['ROOMROM_SUBSCREEN_SPRITE_TILE_COUNT'] - 1}  "
        f"LINK_FLASH3=tiles {c['ROOMROM_LINK_FLASH3_TILE_BASE']}..{c['ROOMROM_LINK_FLASH3_TILE_BASE'] + c['ROOMROM_LINK_FLASH3_TILE_COUNT'] - 1}  "
        f"LINK_ATTACK_FLASH3=tiles {c['ROOMROM_LINK_ATTACK_FLASH3_TILE_BASE']}..{c['ROOMROM_LINK_ATTACK_FLASH3_TILE_BASE'] + c['ROOMROM_LINK_ATTACK_FLASH3_TILE_COUNT'] - 1}  "
        f"contiguous_tail_headroom={headroom_tiles} tiles before VDP tables"
        f"{free_zone_suffix}"
    )
    # Phase L (2026-05-19): surface HScroll table unused-byte count
    # for observability. HSCROLL_PLANE writes 4 B of 1024 B allocated;
    # 1020 B available as SAT extension scratch (not addressable as
    # tile data — adjacent to SAT not contiguous with BG/SPR/ITEM).
    print(
        f"  hscroll_table_unused={HSCROLL_TABLE_FREE_BYTES} bytes "
        f"(HSCROLL_PLANE mode uses 4 B of 1 KB allocated; available as "
        f"SAT-extension scratch only)"
    )


if __name__ == "__main__":
    main()
