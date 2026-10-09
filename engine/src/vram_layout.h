#ifndef ROOMROM_VRAM_MAP_H
#define ROOMROM_VRAM_MAP_H

#include "atlas/items_chr_x4.h"

/* engine VRAM tile map -- single source of truth.
 *
 * Concrete numeric values committed after running
 * engine/tools/audit_vram_tile_usage.py on the Phase 1 baseline trees.
 *
 * Pre-Phase-3 (current): only sub-pal 0 is populated; existing CHR
 * uploads land at the legacy hard-coded bases (HUD_TILE_BASE = 1,
 * SPRITE_VRAM_TILE_BASE = 512, COMMON_VRAM_TILE_BASE = 936). This header
 * defines the *target* post-expansion layout; Phase 3 relocates the
 * upload addresses + emits the 4 sub-pal tile copies to fill the bank.
 *
 * Layout (post-Phase-3, post-ITEM-bank, per `audit_vram_tile_usage.py`):
 *   tile 0                                  blank (transparent fallback)
 *   tile 1   .. 1 + 4*256 - 1 = 1024        BG bank (NES BG + HUD tiles,
 *                                            4 sub-pal copies x 256 tiles)
 *   tile 1025 .. 1025 + 312 - 1 = 1336      SPR bank (Link, sword body,
 *                                            common_chr sprite half --
 *                                            intentionally 1x: every
 *                                            sprite in this bank only
 *                                            uses NES sprite sub-pal 0)
 *   tile 1337 .. 1337 + 4*49 - 1 = 1532     ITEM bank (item atlas:
 *                                            sword, beam, boomerang,
 *                                            arrow, bomb, explosion,
 *                                            sword_diag -- 4 sub-pal
 *                                            copies x 49 tiles, NES
 *                                            DrawCloud/etc cite sub-pal
 *                                            1+ per tile)
 *   tile 1533 .. 1535                       reserved / future (3 tiles)
 *   tile 1536+                              VDP plane / window / SAT / HScroll
 *                                            tables (post-PR-2c 64x64 layout
 *                                            allocates $C000+ for tables;
 *                                            1536 = $C000 / 32).
 *
 * VRAM table addresses (PR-2c 64x64 mode, shared BGA/BGB):
 *   plane A/B= $C000  (tiles 1536..1791, 8 KB)
 *   window   = $E000  (tiles 1792..1919, 4 KB)
 *   hscroll  = $F000  (tiles 1920..1951, 1 KB)
 *   SAT      = $F400  (tiles 1952..1971, 640 B)
 *   free     = $F800-$FFFF                  (2 KB unused, future use)
 *
 * Tile bank ends at 1460 -- 75 tiles of headroom before the table region.
 * (+192 tiles vs pre-PR-2 64x64 mode which capped at $A800 = 1344 tiles.)
 * Audit command:  python engine/tools/audit_vram_tile_usage.py
 *
 * Notes:
 * - SPR sub-pal stride = 312 covers common_chr full (238) + Link walk (32)
 *   + Link attack (16) + item atlas (26). sprites_chr (232 OW enemies) is
 *   NOT in the bank -- enemies are out-of-scope per engine roadmap; they
 *   re-enter the bank when ported.
 * - SPR is intentionally 1x (sub-pal 0 only). The full SPR bank at 4x
 *   would be 549*4=2196 tiles and collide with the VDP table region at
 *   tile 1536. Items -- the only sprite category with non-trivial NES
 *   sub-pal variation -- live in the dedicated ITEM bank below with
 *   their own 4x sub-pal expansion.
 * - HUD shares the BG bank: HUD tiles ARE NES BG tiles, same expansion
 *   rule, same sub-pal stride.
 */

/* tile 0 is always blank (transparent fallback for any plane). */
#define ROOMROM_BLANK_TILE              0u

/* BG bank — Phase J (2026-05-18): sparse atlas replaces 4x pixel-bias
 * replication. Only (tile_id, sub_pal) combos USED by UW + OW rooms +
 * HUD are emitted. Universal LUT `bg_sparse_tile_lut[256][4]` maps NES
 * (tile_id, sub_pal) -> sparse-atlas slot. Slot N at VRAM tile
 * (ROOMROM_BG_TILE_BASE + N). Renderer (ow/uw/hud) uses
 * roomrom_bg_sparse_tile_word(raw, pal) helper.
 *
 * Phase J Step 3: bank actual size = BG_SPARSE_TILE_COUNT (~357 tiles).
 * Tile slots from BG_BASE + BG_SPARSE_TILE_COUNT up to SPR_BASE are
 * FREE (Phase J.2 follow-up may shift SPR/ITEM/SCENE_OBJ banks down to
 * claim the gap as headroom). For now the gap is documented free VRAM
 * available for new content without touching the verifier ceiling.
 *
 * Legacy ROOMROM_BG_TILE_COUNT_PER_PAL kept at 256 (NES tile ID range)
 * for any consumer that needs the NES domain count. ROOMROM_BG_SUBPAL_COUNT
 * kept at 4 for the same reason. Macro ROOMROM_BG_TILE_BASE_PAL(s) now
 * returns BG_TILE_BASE regardless of s (legacy contracts compile; new
 * code uses the LUT helper). */
#define ROOMROM_BG_TILE_BASE            1u
/* Phase J.2 (2026-05-18): BG bank physical size = BG_SPARSE_TILE_COUNT
 * (~532 tiles). Was 4 sub-pal x 256 = 1024 pre-Phase-J. Universal
 * sparse LUT addresses (tile_id, sub_pal) -> slot. Per-sub-pal stride
 * no longer applies; SUBPAL_COUNT=1 reflects sparse layout.
 * BG_TILE_COUNT_PER_PAL retained as 256 for any consumer querying the
 * NES tile ID domain (not VRAM bank size). */
#define ROOMROM_BG_TILE_COUNT_PER_PAL   677u    /* Fixed BG reservation; BG_SPARSE_TILE_COUNT (bg_sparse_chr.h) must not exceed it (build gate). */
#define ROOMROM_BG_SUBPAL_COUNT         1u      /* sparse: no per-sub-pal copies */
#define ROOMROM_SPR_TILE_BASE           678u    /* 1 + ROOMROM_BG_TILE_COUNT_PER_PAL. All SPR/ITEM/SCENE_OBJ bases derive from this. */
#define ROOMROM_SPR_TILE_COUNT_PER_PAL  287u    /* common(238)+walk(32)+attack(16)=286 used, 1 slack. Reduced from 312 (-25 tiles) to make ITEM bank fit at 56 tiles/sub-pal x4 = 224 tiles after 8x16 mode parity work (bomb +1 tile, explosion +6 tiles for 16x16 mirrored). Post-Phase-B (2026-05-18): ITEM bank shrank to 70 tiles single-copy, leaving 140 headroom tiles (1382..1521) for future SPR expansion without VDP table relocation. */
#define ROOMROM_SPR_SUBPAL_COUNT        1u      /* SPR bank stays 1x (sub-pal 0 only) physically. Phase D unblock (2026-05-18): future sprites needing sub-pal 1/2 can use this bank at 1x VRAM cost — Genesis OAM pal field selects PAL2/PAL3 (loaded with NES SPR sub-pal 1/2 colors by roomrom_bg_palette_load_palram_full per src/game/world/bg_palette.h CRAM target). Helper: ROOMROM_SUBPAL_PAL(s) in sprite_render.c maps sub_pal 0/1/2 -> PAL1/PAL2/PAL3 for any sprite renderer. No new tile copies required. */

/* Phase J (2026-05-18): macro neutralized — sparse atlas has no per-
 * sub-pal stride. All sub-pals collapse to BG_TILE_BASE; the actual
 * VRAM tile is selected via bg_sparse_tile_lut[raw_tile][sub_pal] +
 * BG_TILE_BASE. Existing call sites compile unchanged; new code uses
 * roomrom_bg_sparse_tile_word() helper. */
#define ROOMROM_BG_TILE_BASE_PAL(s)  \
    (ROOMROM_BG_TILE_BASE + 0u * (unsigned short)(s))
#define ROOMROM_SPR_TILE_BASE_PAL(s) \
    (ROOMROM_SPR_TILE_BASE + (unsigned short)(s) * ROOMROM_SPR_TILE_COUNT_PER_PAL)
/* HUD tiles live in the BG bank (same NES BG content, same sub-pal stride). */
#define ROOMROM_HUD_TILE_BASE_PAL(s) \
    ROOMROM_BG_TILE_BASE_PAL(s)

/* Item atlas sub-bank: per-category 4x sub-pal expansion. NES Z1 draws
 * bomb / explosion with sprite sub-pal 1; future sword-level upgrades
 * use sub-pal 1 / 2 (Items inventory). Other persistent sprites
 * (Link, sword, beam, common) only ever use sub-pal 0 -- they stay in
 * the 1x SPR bank above.
 *
 * VRAM math: ITEM bank starts immediately after the SPR bank end and
 * holds 4 copies of the item atlas (sub-pal 0..3), each ITEM_TILE_COUNT
 * tiles wide. Total = 4 * ITEM_TILE_COUNT.
 *
 * ITEM_TILE_COUNT_PER_PAL is sourced from the generated header
 * atlas/items_chr_x4.h (ROOMROM_ATLAS_ITEMS_X4_TILE_COUNT). The verifier
 * tools/verify_vram_budget.py confirms ITEM bank does not overlap
 * with VDP table region or any other VRAM consumer. */
#define ROOMROM_ITEM_TILE_BASE          (ROOMROM_SPR_TILE_BASE + ROOMROM_SPR_TILE_COUNT_PER_PAL)
#define ROOMROM_ITEM_TILE_COUNT_PER_PAL ROOMROM_ATLAS_ITEMS_X4_TILE_COUNT
/* Sub-pal expansion factor. History:
 *   4 (PR-3 baseline)
 *   3 on 2026-05-08 (drop sub-pal 3 unused, fund 8x16 fixes)
 *   1 on 2026-05-18 Phase B (drop pixel-bias replication entirely; sub-pal
 *     now routes via Genesis OAM pal field PAL1/PAL2/PAL3 per
 *     src/game/world/bg_palette.h CRAM target). Frees 140 tiles / 4480 B. */
#define ROOMROM_ITEM_SUBPAL_COUNT       1u
/* Phase B: macro neutralized. All sub-pal copies collapsed to one; returns
 * ROOMROM_ITEM_TILE_BASE regardless of s. Existing call sites pass sub_pal
 * harmlessly; the renderer now selects sub-pal via OAM pal arg (see
 * ROOMROM_SUBPAL_PAL in src/game/world/render/sprite_render.c). */
#define ROOMROM_ITEM_TILE_BASE_PAL(s)   (ROOMROM_ITEM_TILE_BASE + 0u * (unsigned short)(s))

/* HUD backdrop sprite-strip retired 2026-05-15. Opaque black HUD underlay
 * now comes from BG_A tile 0 (PAL0 color 0), driven by
 * clear_hud_underlay_for_row_base() in engine/src/main.c. The 8 tiles
 * previously reserved here are freed back into post-item-bank headroom. */

/* PR-5 CHR-BOSSES: per-level boss CHR. NES Z1 mirrors this exactly --
 * z_03.asm:91 FetchPatternBlockUWBoss writes the boss bank into PPU
 * $0C00, which is the same 4 KB sprite half as the per-level enemy bank
 * at $09E0; on Genesis we reuse the SCENE_OBJ slot at
 * (SPR_TILE_BASE + 44u) because boss rooms have no enemies (NES parity).
 * Boss banks are 64 NES tiles, 1x sub-pal -- well under the 136-tile
 * SCENE_OBJ slot capacity. Three banks dispatched by CurLevel per
 * z_03.asm:24-34 BossPatternBlockSrcAddrs:
 *   UWSPBoss1257 -> L1, L2, L5, L7   (Aquamentus / Dodongo)
 *   UWSPBoss3468 -> L3, L4, L6, L8   (Manhandla / Gleeok / Digdogger / Gohma-style)
 *   UWSPBoss9    -> L9               (Ganon)
 */
/* Common fireball art ($44/$45) must survive the scene/boss overlay.
 * 1300..1305 are the existing cloud bank; 1306..1307 are reserved here. */
#define ROOMROM_CLOUD_TILE_BASE 1300u
#define ROOMROM_CLOUD_TILE_COUNT 6u
#define ROOMROM_FIREBALL_TILE_BASE 1306u
#define ROOMROM_FIREBALL_TILE_COUNT 2u
#define ROOMROM_SPARK_TILE_BASE 1308u
#define ROOMROM_SPARK_TILE_COUNT 4u
/* HUD position marker: NES common sprite pair $3E/$3F (8x16). The copy at
 * SPR_TILE_BASE+$3E lies inside the SCENE_OBJ overlay (SPR_TILE_BASE+44,
 * 136 tiles) and was overwritten by OW enemy CHR: the marker showed half an
 * enemy in the minimap (lockstep hud_marker, VRAM tile $2BC). */
#define ROOMROM_HUD_MARKER_TILE_BASE 1312u
#define ROOMROM_HUD_MARKER_TILE_COUNT 2u
/* T-195: Magical Shield item $1C uses common pair $56/$57. Its original
 * SPR copy is inside the scene overlay, and the legacy item atlas omits
 * this pair. Keep it resident beside the protected HUD marker. */
#define ROOMROM_SHIELD_TILE_BASE 1314u
#define ROOMROM_SHIELD_TILE_COUNT 2u
/* T-187: common BG ladder $6F and brick $FA, biased to sub-pal2. */
#define ROOMROM_CELLAR_BG_TILE_BASE 1316u
#define ROOMROM_CELLAR_BG_TILE_COUNT 2u
/* T-229: common compass $6A/$6B must survive scene/boss CHR swaps.
 * The legacy atlas's named compass entry is $2E/$2F, unrelated art. */
#define ROOMROM_COMPASS_TILE_BASE 1318u
#define ROOMROM_COMPASS_TILE_COUNT 2u
/* T-011: Link lifting an item, NES common sprite pair $78/$79 (8x16). Its
 * SPR_TILE_BASE+$78 copy lies inside the SCENE_OBJ overlay (SPR+44) and
 * holds enemy art in play. The 1376..1440 gap was unused in OW / cave /
 * UW / boss captures (t011, t131, t129); T-138 reserves 1378..1425 for
 * Link hurt-flash poses; fireball/spark palette-3 copies now use
 * 1426..1431, leaving 1432..1440 free. */
#define ROOMROM_LINK_LIFT_TILE_BASE 1376u
#define ROOMROM_LINK_LIFT_TILE_COUNT 2u
/* T-138: 8 walk poses x 4 tiles, color indices 1..3 biased to 13..15.
 * PAL1[13..15] holds NES sprite sub-palette 3; the unmodified walk bank
 * covers sub-palettes 0..2 via PAL1/PAL2/PAL3. 1378..1409 is reserved
 * below the VDP tables and does not overlap the lift pair. */
#define ROOMROM_LINK_FLASH3_TILE_BASE 1378u
#define ROOMROM_LINK_FLASH3_TILE_COUNT 32u
#define ROOMROM_LINK_ATTACK_FLASH3_TILE_BASE 1410u
#define ROOMROM_LINK_ATTACK_FLASH3_TILE_COUNT 16u
/* T-160: NES fireball $44/$45 with OAM sub-pal 3 uses PAL1[13..15]. */
#define ROOMROM_FIREBALL_SUBPAL3_TILE_BASE 1426u
#define ROOMROM_FIREBALL_SUBPAL3_TILE_COUNT 2u
/* T-161: death-spark $62..$65 also flashes through NES sub-pal 3. */
#define ROOMROM_SPARK_SUBPAL3_TILE_BASE 1428u
#define ROOMROM_SPARK_SUBPAL3_TILE_COUNT 4u
/* T-169: NES 8x16 sprites with an odd tile id take pattern table 1 (the
 * background tiles), e.g. the UW push block (DrawBlock: $B1/$B3, attr 3).
 * enemy_render.c copies the BG pair (top, top+1) for the sprite's sub-pal
 * here on demand: 4 cached pairs, drawn with PAL1. 1432..1439 (free). */
#define ROOMROM_PT1_PAIR_TILE_BASE 1432u
#define ROOMROM_PT1_PAIR_TILE_COUNT 8u
/* The pause inventory owns 1280..1295. All 16 live-extracted tiles are
 * also uploaded at gameplay boot: the marker pair (14/15) for the Original
 * HUD, raft $6C (4/5) and ladder $76 (6/7) for the world sprites (T-056). */
#define ROOMROM_SUBSCREEN_SPRITE_TILE_BASE 1280u
#define ROOMROM_SUBSCREEN_SPRITE_TILE_COUNT 16u
#define ROOMROM_HUD_COMPASS_MARKER_TILE 1294u

#define ROOMROM_BOSS_TILE_BASE          (ROOMROM_SPR_TILE_BASE + 44u)
#define ROOMROM_BOSS_TILE_COUNT         64u
#define ROOMROM_BOSS_SUBPAL_COUNT       1u

/* Palette 3 boss sprites use PAL1[12..15]. A biased copy preserves
 * all four NES palettes without changing palettes used by Link/FX. */
#define ROOMROM_BOSS_SUBPAL3_TILE_BASE  (ROOMROM_ITEM_TILE_BASE + ROOMROM_ITEM_TILE_COUNT_PER_PAL * ROOMROM_ITEM_SUBPAL_COUNT)
#define ROOMROM_BOSS_SUBPAL3_TILE_COUNT 64u

/* T-170/T-171: NES sprite sub-palette 3 for every other sprite (Zora,
 * Armos, Ghini, gels, ...): enemy_render copies the 8x16 tile pair with
 * opaque pixels +12 into this cache on demand (32 pairs), drawn with PAL1
 * colors 12..15. Tiles 1125..1188, unused in every gameplay VRAM dump
 * (798 lockstep snapshots, 2026-10-01). */
#define ROOMROM_SUBPAL3_PAIR_TILE_BASE  (ROOMROM_BOSS_SUBPAL3_TILE_BASE + ROOMROM_BOSS_SUBPAL3_TILE_COUNT)
#define ROOMROM_SUBPAL3_PAIR_TILE_COUNT 32u

#endif
