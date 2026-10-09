/* PR-4a CHR-TRANSIENT-SCENE state machine — framework.
 *
 * See level_chr_swap.h for state semantics.
 *
 * Stance (D1): EXTEND — the upload steps reuse SGDK VDP_loadTileData,
 * but the multi-VBlank scheduling + BLANK collapse + ready-gate are
 * Genesis-native (no NES analog; NES PPU CHR-RAM banks swap in a single
 * scanline). Drained-C reference: src/game/world/draw_dispatch.c +
 * src/game/enemies/enemy_dispatch.c sub-pal attr lookup is consumed
 * here only as the eventual *content* selector; the residency state
 * machine itself is a Genesis-side resource manager.
 */
#include <genesis.h>
#include "level_chr_swap.h"
#include "enemy_chr.h"
#include "boss_chr.h"
#include "../vram_layout.h"

/* Per-scene contract resolver. PR-4b wires UW enemy banks via
 * PatternBlockUWSP{127,358,469}. OW NPC / cave-dweller hooks land
 * in PR-4c. */
static const roomrom_vram_contract_t *contract_for_scene(roomrom_scene_id_t s)
{
    switch (s) {
    case ROOMROM_SCENE_UW_L1: return &roomrom_vram_contract_uw_l1_enemies;
    case ROOMROM_SCENE_UW_L2: return &roomrom_vram_contract_uw_l2_enemies;
    case ROOMROM_SCENE_UW_L3: return &roomrom_vram_contract_uw_l3_enemies;
    case ROOMROM_SCENE_UW_L4: return &roomrom_vram_contract_uw_l4_enemies;
    case ROOMROM_SCENE_UW_L5: return &roomrom_vram_contract_uw_l5_enemies;
    case ROOMROM_SCENE_UW_L6: return &roomrom_vram_contract_uw_l6_enemies;
    case ROOMROM_SCENE_UW_L7: return &roomrom_vram_contract_uw_l7_enemies;
    case ROOMROM_SCENE_UW_L8: return &roomrom_vram_contract_uw_l8_enemies;
    case ROOMROM_SCENE_UW_L9: return &roomrom_vram_contract_uw_l9_enemies;
    case ROOMROM_SCENE_OVERWORLD: return &roomrom_vram_contract_overworld_enemies;
    default: return 0;
    }
}

/* Per-scene CHR blob resolver. NES UW levels share three banks per
 * z_03.asm:67-89 dispatch:
 *   UWSP127 → L1, L2, L7
 *   UWSP358 → L3, L5, L8
 *   UWSP469 → L4, L6, L9
 * PR-4c: OW NPC + cave-dweller bank = single OWSP block (z_03.asm:42),
 * 1x sub-pal (sprites use NES sprite sub-pal 0..3 via per-OBJ ObjAttr,
 * not via CHR replication). */
static const unsigned char *enemy_blob_for_scene(roomrom_scene_id_t s)
{
    switch (s) {
    case ROOMROM_SCENE_UW_L1:
    case ROOMROM_SCENE_UW_L2:
    case ROOMROM_SCENE_UW_L7:
        return roomrom_atlas_enemy_uwsp127;
    case ROOMROM_SCENE_UW_L3:
    case ROOMROM_SCENE_UW_L5:
    case ROOMROM_SCENE_UW_L8:
        return roomrom_atlas_enemy_uwsp358;
    case ROOMROM_SCENE_UW_L4:
    case ROOMROM_SCENE_UW_L6:
    case ROOMROM_SCENE_UW_L9:
        return roomrom_atlas_enemy_uwsp469;
    case ROOMROM_SCENE_OVERWORLD:
        return roomrom_atlas_enemy_owsp;
    default:
        return 0;
    }
}

/* SCENE_OBJ slot sized for the largest active contract.
 * Post-Phase-F (2026-05-18): UWSP 34, OWSP 114, BOSS 64. Max = 114
 * (OWSP NPC/cave-dweller bank). Phase M (2026-05-18) shrunk from
 * 136 (legacy 4x UWSP) to 114; recovers 22 tiles back to common_chr
 * range within the SPR bank.
 * BLANK clears full slot before every DMA so smaller contracts
 * (UWSP 34, BOSS 64) don't leave stale tail bytes from prior scene. */
#define SCENE_OBJ_SLOT_TILES 114u

static level_chr_swap_state_t s_state = LEVEL_CHR_SWAP_IDLE;
static roomrom_scene_id_t      s_target = ROOMROM_SCENE_BOOT;
static roomrom_scene_id_t      s_active = ROOMROM_SCENE_BOOT;
static const roomrom_vram_contract_t *s_target_contract = 0;

static unsigned long  s_total_bytes_dma = 0u;
static unsigned short s_request_count = 0u;
/* Phase M cont. (2026-05-19): track high-water tile count we've ever
 * blanked into the SCENE_OBJ slot. BLANK fills max(last_used, current)
 * so a steady-state UWSP→UWSP swap only zeros 34 tiles instead of the
 * full 114-tile slot, recovering ~2560 B of CPU-fill work per swap.
 * Tail (last_used..max_capacity) stays zero across same-size swaps. */
static unsigned short s_last_used_tiles = 0u;

/* PR-5 boss state machine — declared up here so level_chr_swap_init()
 * can reset both at once. Definitions of *_for_scene helpers + the
 * tick body live below the enemy machine. */
static level_chr_swap_state_t s_boss_state = LEVEL_CHR_SWAP_IDLE;
static roomrom_scene_id_t      s_boss_target = ROOMROM_SCENE_BOOT;
static roomrom_scene_id_t      s_boss_active = ROOMROM_SCENE_BOOT;
static const roomrom_vram_contract_t *s_boss_target_contract = 0;

static unsigned long  s_boss_total_bytes_dma = 0u;
static unsigned short s_boss_request_count = 0u;
/* Phase M cont. (2026-05-19): per-boss-state-machine high-water tracker
 * for the same BLANK shrink optimization. Boss banks are typically
 * smaller (64 tiles vs OWSP 114) so this state machine benefits from
 * targeted BLANK instead of always-full-slot fill. */
static unsigned short s_boss_last_used_tiles = 0u;

void level_chr_swap_init(void)
{
    s_state = LEVEL_CHR_SWAP_IDLE;
    s_target = ROOMROM_SCENE_BOOT;
    s_active = ROOMROM_SCENE_BOOT;
    s_target_contract = 0;
    s_total_bytes_dma = 0u;
    s_request_count = 0u;
    s_last_used_tiles = 0u;

    /* PR-5: boss state machine shares the SCENE_OBJ slot. */
    s_boss_state = LEVEL_CHR_SWAP_IDLE;
    s_boss_target = ROOMROM_SCENE_BOOT;
    s_boss_active = ROOMROM_SCENE_BOOT;
    s_boss_target_contract = 0;
    s_boss_total_bytes_dma = 0u;
    s_boss_request_count = 0u;
    s_boss_last_used_tiles = 0u;
}

void level_chr_swap_request(roomrom_scene_id_t scene)
{
    /* A resident boss bank overlaps this bank, even in the same level. */
    if (s_active == scene && s_state == LEVEL_CHR_SWAP_READY &&
        s_boss_state == LEVEL_CHR_SWAP_IDLE) {
        return;
    }

    s_boss_state = LEVEL_CHR_SWAP_IDLE;
    s_boss_active = ROOMROM_SCENE_BOOT;
    s_target = scene;
    s_target_contract = contract_for_scene(scene);
    s_state = LEVEL_CHR_SWAP_REQUESTED;
    s_request_count = (unsigned short)(s_request_count + 1u);
}

void level_chr_swap_tick(void)
{
    const roomrom_vram_contract_t *c = s_target_contract;

    switch (s_state) {
    case LEVEL_CHR_SWAP_IDLE:
    case LEVEL_CHR_SWAP_READY:
        return;

    case LEVEL_CHR_SWAP_REQUESTED: {
        if (c == 0 || c->tile_count == 0u || c->blob_bytes == 0u) {
            /* Empty contract: collapse to READY in one tick. */
            s_active = s_target;
            s_state = LEVEL_CHR_SWAP_READY;
            return;
        }
        s_state = LEVEL_CHR_SWAP_BLANK;
        return;
    }

    case LEVEL_CHR_SWAP_BLANK: {
        /* Phase M cont. (2026-05-19): BLANK only max(last_used,
         * current). Codex P0-2 originally cleared full 114-tile slot
         * to guard against sub-pal aliasing tail; post-Phase-F UWSP is
         * 1x (no aliasing) so safe to scope BLANK to actual content.
         * High-water tracker s_last_used_tiles ensures we still clear
         * stale tail when shrinking from a larger prior scene. */
        if (c != 0 && c->tile_count > 0u) {
            unsigned short blank_tiles =
                (c->tile_count > s_last_used_tiles)
                    ? c->tile_count
                    : s_last_used_tiles;
            /* VDP fill owns the data port until complete. HUD/room CPU
             * writes later this frame must not become its fill data. */
            VDP_fillTileData(0u, c->tile_base, blank_tiles, TRUE);
            s_total_bytes_dma += (unsigned long)blank_tiles * 32ul;
            s_last_used_tiles = c->tile_count;
        }
        s_state = LEVEL_CHR_SWAP_DMA_SCENE_A;
        return;
    }

    case LEVEL_CHR_SWAP_DMA_SCENE_A: {
        const unsigned char *blob = enemy_blob_for_scene(s_target);
        if (c != 0 && blob != 0 && c->tile_count > 0u) {
            unsigned short half_a = (unsigned short)(c->tile_count >> 1);
            if (half_a > 0u) {
                VDP_loadTileData((const u32 *)(blob + c->blob_offset),
                                 c->tile_base, half_a, TRUE);
                s_total_bytes_dma += (unsigned long)half_a * 32ul;
            }
        }
        s_state = LEVEL_CHR_SWAP_DMA_SCENE_B;
        return;
    }

    case LEVEL_CHR_SWAP_DMA_SCENE_B: {
        const unsigned char *blob = enemy_blob_for_scene(s_target);
        if (c != 0 && blob != 0 && c->tile_count > 0u) {
            unsigned short half_a = (unsigned short)(c->tile_count >> 1);
            unsigned short half_b = (unsigned short)(c->tile_count - half_a);
            if (half_b > 0u) {
                unsigned long off = (unsigned long)c->blob_offset
                                  + (unsigned long)half_a * 32ul;
                VDP_loadTileData((const u32 *)(blob + off),
                                 (unsigned short)(c->tile_base + half_a),
                                 half_b, TRUE);
                s_total_bytes_dma += (unsigned long)half_b * 32ul;
            }
        }
        s_active = s_target;
        s_state = LEVEL_CHR_SWAP_READY;
        return;
    }

    default:
        s_state = LEVEL_CHR_SWAP_IDLE;
        return;
    }
}

level_chr_swap_state_t level_chr_swap_state(void)        { return s_state; }
roomrom_scene_id_t     level_chr_swap_active_scene(void) { return s_active; }
unsigned char level_chr_swap_is_ready(void)
{
    return (unsigned char)(s_state == LEVEL_CHR_SWAP_READY ? 1u : 0u);
}
unsigned long  level_chr_swap_total_bytes_dma(void) { return s_total_bytes_dma; }
unsigned short level_chr_swap_request_count(void)   { return s_request_count; }

/* ============================================================
 * PR-5 CHR-BOSSES: parallel state machine for boss bank.
 * Same DMA framework as enemy state machine, sharing the
 * SCENE_OBJ slot (NES parity per z_03.asm:91). Distinct state
 * vars + stats so probes can observe both independently.
 * ============================================================ */

/* Per-level boss bank resolver per z_03.asm:24-34
 * BossPatternBlockSrcAddrs:
 *   L1, L2, L5, L7 -> UWSPBoss1257
 *   L3, L4, L6, L8 -> UWSPBoss3468
 *   L9             -> UWSPBoss9
 * Non-UW scenes return 0 (no boss bank). */
static const unsigned char *boss_blob_for_scene(roomrom_scene_id_t s,
                                                unsigned char subpal3)
{
    switch (s) {
    case ROOMROM_SCENE_UW_L1:
    case ROOMROM_SCENE_UW_L2:
    case ROOMROM_SCENE_UW_L5:
    case ROOMROM_SCENE_UW_L7:
        return subpal3 ? roomrom_atlas_boss_uwspboss1257_subpal3
                       : roomrom_atlas_boss_uwspboss1257;
    case ROOMROM_SCENE_UW_L3:
    case ROOMROM_SCENE_UW_L4:
    case ROOMROM_SCENE_UW_L6:
    case ROOMROM_SCENE_UW_L8:
        return subpal3 ? roomrom_atlas_boss_uwspboss3468_subpal3
                       : roomrom_atlas_boss_uwspboss3468;
    case ROOMROM_SCENE_UW_L9:
        return subpal3 ? roomrom_atlas_boss_uwspboss9_subpal3
                       : roomrom_atlas_boss_uwspboss9;
    default:
        return 0;
    }
}

/* Boss contract resolver: same scene -> contract mapping as enemies,
 * but using the per-scene "bosses" contract row from gen_atlas.py
 * SCENE_CONTRACTS. */
static const roomrom_vram_contract_t *boss_contract_for_scene(roomrom_scene_id_t s)
{
    switch (s) {
    case ROOMROM_SCENE_UW_L1: return &roomrom_vram_contract_uw_l1_bosses;
    case ROOMROM_SCENE_UW_L2: return &roomrom_vram_contract_uw_l2_bosses;
    case ROOMROM_SCENE_UW_L3: return &roomrom_vram_contract_uw_l3_bosses;
    case ROOMROM_SCENE_UW_L4: return &roomrom_vram_contract_uw_l4_bosses;
    case ROOMROM_SCENE_UW_L5: return &roomrom_vram_contract_uw_l5_bosses;
    case ROOMROM_SCENE_UW_L6: return &roomrom_vram_contract_uw_l6_bosses;
    case ROOMROM_SCENE_UW_L7: return &roomrom_vram_contract_uw_l7_bosses;
    case ROOMROM_SCENE_UW_L8: return &roomrom_vram_contract_uw_l8_bosses;
    case ROOMROM_SCENE_UW_L9: return &roomrom_vram_contract_uw_l9_bosses;
    default:                  return 0;
    }
}

void level_chr_boss_request(roomrom_scene_id_t scene)
{
    if (s_boss_active == scene && s_boss_state == LEVEL_CHR_SWAP_READY) {
        return;
    }

    s_boss_target = scene;
    s_boss_target_contract = boss_contract_for_scene(scene);
    s_boss_state = LEVEL_CHR_SWAP_REQUESTED;
    s_boss_request_count = (unsigned short)(s_boss_request_count + 1u);
}

void level_chr_boss_tick(void)
{
    const roomrom_vram_contract_t *c = s_boss_target_contract;

    switch (s_boss_state) {
    case LEVEL_CHR_SWAP_IDLE:
    case LEVEL_CHR_SWAP_READY:
        return;

    case LEVEL_CHR_SWAP_REQUESTED: {
        /* Room load can request both banks before either has uploaded.
         * Finish the enemy bank first: their VRAM ranges overlap, so
         * interleaved halves corrupt the boss bank after its first DMA. */
        if (s_state != LEVEL_CHR_SWAP_IDLE && s_state != LEVEL_CHR_SWAP_READY) {
            return;
        }
        if (c == 0 || c->tile_count == 0u || c->blob_bytes == 0u) {
            s_boss_active = s_boss_target;
            s_boss_state = LEVEL_CHR_SWAP_READY;
            return;
        }
        s_boss_state = LEVEL_CHR_SWAP_BLANK;
        return;
    }

    case LEVEL_CHR_SWAP_BLANK: {
        /* Phase M cont. (2026-05-19): BLANK shrink optimization. Same
         * high-water tracker as enemy state machine — boss banks (64
         * tiles) follow enemy banks (typically 34); shrinking to
         * actual c->tile_count saves ~50 tile fill per boss entry. */
        if (c != 0 && c->tile_count > 0u) {
            unsigned short blank_tiles =
                (c->tile_count > s_boss_last_used_tiles)
                    ? c->tile_count
                    : s_boss_last_used_tiles;
            /* VDP fill owns the data port until complete. HUD/room CPU
             * writes later this frame must not become its fill data. */
            VDP_fillTileData(0u, c->tile_base, blank_tiles, TRUE);
            s_boss_total_bytes_dma += (unsigned long)blank_tiles * 32ul;
            s_boss_last_used_tiles = c->tile_count;
        }
        s_boss_state = LEVEL_CHR_SWAP_DMA_SCENE_A;
        return;
    }

    case LEVEL_CHR_SWAP_DMA_SCENE_A: {
        const unsigned char *blob = boss_blob_for_scene(s_boss_target, 0u);
        const unsigned char *subpal3 = boss_blob_for_scene(s_boss_target, 1u);
        if (c != 0 && blob != 0 && c->tile_count > 0u) {
            unsigned short half_a = (unsigned short)(c->tile_count >> 1);
            if (half_a > 0u) {
                VDP_loadTileData((const u32 *)(blob + c->blob_offset),
                                 c->tile_base, half_a, TRUE);
                VDP_loadTileData((const u32 *)(subpal3 + c->blob_offset),
                                 ROOMROM_BOSS_SUBPAL3_TILE_BASE, half_a, TRUE);
                s_boss_total_bytes_dma += (unsigned long)half_a * 64ul;
            }
        }
        s_boss_state = LEVEL_CHR_SWAP_DMA_SCENE_B;
        return;
    }

    case LEVEL_CHR_SWAP_DMA_SCENE_B: {
        const unsigned char *blob = boss_blob_for_scene(s_boss_target, 0u);
        const unsigned char *subpal3 = boss_blob_for_scene(s_boss_target, 1u);
        if (c != 0 && blob != 0 && c->tile_count > 0u) {
            unsigned short half_a = (unsigned short)(c->tile_count >> 1);
            unsigned short half_b = (unsigned short)(c->tile_count - half_a);
            if (half_b > 0u) {
                unsigned long off = (unsigned long)c->blob_offset
                                  + (unsigned long)half_a * 32ul;
                VDP_loadTileData((const u32 *)(blob + off),
                                 (unsigned short)(c->tile_base + half_a),
                                 half_b, TRUE);
                VDP_loadTileData((const u32 *)(subpal3 + off),
                                 (unsigned short)(ROOMROM_BOSS_SUBPAL3_TILE_BASE + half_a),
                                 half_b, TRUE);
                s_boss_total_bytes_dma += (unsigned long)half_b * 64ul;
            }
        }
        s_boss_active = s_boss_target;
        s_boss_state = LEVEL_CHR_SWAP_READY;
        return;
    }

    default:
        s_boss_state = LEVEL_CHR_SWAP_IDLE;
        return;
    }
}

level_chr_swap_state_t level_chr_boss_state(void)        { return s_boss_state; }
roomrom_scene_id_t     level_chr_boss_active_scene(void) { return s_boss_active; }
unsigned char level_chr_boss_is_ready(void)
{
    return (unsigned char)(s_boss_state == LEVEL_CHR_SWAP_READY ? 1u : 0u);
}
unsigned long  level_chr_boss_total_bytes_dma(void) { return s_boss_total_bytes_dma; }
unsigned short level_chr_boss_request_count(void)   { return s_boss_request_count; }
