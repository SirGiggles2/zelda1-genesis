/* Phase 7 Task 7.2 step 3 in-ROM probe — verifies enemy slot iterator
 * + INIT dispatch row $07 (slow octorok / ghini). See enemy_loop_probe.h
 * for block layout.
 *
 * Step-3 wiring: enemy_init_fns[$07] = enrt_init_slow_octorock_or_ghini.
 * After force_spawn the init body runs: enrt_octorock_common(slot, 32)
 * → sets WALK_SPEED, MOVE_TIMER, OBJ_STATE=0, DRAW_FRAME=0, ANIM_TIMER=6,
 * → enrt_init_walker(slot) computes DIR from LINK_X/LINK_Y vs OBJ_X/Y.
 *
 * Probe pre-sets LINK_X = LINK_Y = $80 so DIR computation is
 * deterministic (link == obj → h_dir = 2, ties resolve to h_dir).
 *
 * Probe publishes 14 (actual, expected) u16 pairs into
 * ENEMY_LOOP_PROBE_BASE. BizHawk reads the 68K RAM domain at offset
 * 0x7E00 to inspect.
 */

#include "enemy_loop_probe.h"
#include "../enemy_loop.h"
#include "../../../../RoomRom/src/roomrom_enemy_state.h"
#include "object_state.h"   /* OBJ_STATE for step-3 forwarder check */
#include "platform_abi.h"   /* RAM($034C) ActiveMonsterShots — step 14 */
#include "combat_state.h"   /* MON_HP / MON_HIT_REACTION / MON_SHOVE_*
                             * / MON_METASTATE / ROOM_KILL_COUNT
                             * / COMBAT_HARM_FLAG / ITEM_SWORD_LEVEL
                             * / OBJ_STATE / OBJ_X / OBJ_Y / OBJ_DIR
                             * / LINK_DIR — step 19 damage probe */
#include "link_state.h"     /* DEATH_FRAME_COUNTER — step 19 */
#include "room_state.h"     /* ROOM_OW_CUR_KILL_TOTAL — step 20 (NES RoomKillCount $034F) */

/* Step 17: counters live in enemy_walker_bridge.c. Read-only here. */
extern volatile unsigned long g_check_monster_collisions_calls;
extern volatile unsigned long g_check_link_collision_calls;

/* Step 19 damage probe — seed value for slot 1 octorok HP. Sword level 1
 * deals $10 dmg per stab; pick $08 so the very first damage tick kills
 * the octorok (MON_HP < dmg -> combat_handle_monster_died fires + drop
 * conversion path can be observed within trace window). */
#define STEP19_OCTOROCK_HP_SEED 0x08u

unsigned char enemy_loop_probe_is_armed(void)
{
    volatile unsigned char *legacy =
        (volatile unsigned char *)ENEMY_LOOP_PROBE_CONTROL_BASE;
    volatile unsigned char *ctrl =
        (volatile unsigned char *)DEBUG_PROBE_CONTROL_BASE;
    if (ctrl[0] == DEBUG_PROBE_ARM0 &&
        ctrl[1] == DEBUG_PROBE_ARM1 &&
        (ctrl[DEBUG_PROBE_FLAGS_OFF] & DEBUG_PROBE_ENEMY_STRESS) != 0u) {
        return 1u;
    }
    return (legacy[0] == ENEMY_LOOP_PROBE_ARM0 &&
            legacy[1] == ENEMY_LOOP_PROBE_ARM1) ? 1u : 0u;
}

static void put_u16_be(volatile unsigned char *p, unsigned short v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)(v & 0xFFu);
}

static void put_pair(volatile unsigned char *block, unsigned char idx,
                     unsigned short actual, unsigned short expected)
{
    volatile unsigned char *slot = &block[4u + (unsigned short)idx * 4u];
    put_u16_be(slot,     actual);
    put_u16_be(slot + 2, expected);
}

void enemy_loop_probe_run(void)
{
    volatile unsigned char *block =
        (volatile unsigned char *)ENEMY_LOOP_PROBE_BASE;
    unsigned int alive_before;
    unsigned int alive_after;

    /* Stage 1 — start clean. enemy_loop_room_init was called from
     * roomrom_debug_enter just before this probe; verify it cleared
     * slot state.
     *
     * Phase 7 Task 7.7 step 1 (2026-05-10): enemy_loop_room_init now
     * also runs the NES room matrix loader, which can populate slots
     * 1..N from LBA_C/D + LevelInfo_FoeCounts when those substrate
     * cells are non-zero. Re-clear all slots here so the probe's
     * deterministic force-spawn checks remain valid regardless of
     * boot-time substrate state. */
    {
        unsigned int slot;
        for (slot = ENEMY_LOOP_SLOT_FIRST; slot <= ENEMY_LOOP_SLOT_LAST; ++slot) {
            ENEMY_TYPE(slot) = 0u;
            ENEMY_ALIVE_FLAG(slot) = 0u;
            ENEMY_X(slot) = 0u;
            ENEMY_Y(slot) = 0u;
        }
    }
    alive_before = enemy_loop_alive_count();

    /* Stage 2 — pin LINK position so the wired enrt_init_walker DIR
     * computation is deterministic. With LINK at the same cell as the
     * spawn target, h_dir=2 (link_x >= obj_x), v_dir=4, diff_x=diff_y=0,
     * tie resolves to ENEMY_SCRATCH_Y (= h_dir). Final DIR = 2. */
    LINK_X = 0x80u;
    LINK_Y = 0x80u;

    /* Stage 3 — deterministic seed. Slot 1, SlowOctorock at $80,$80.
     * Pre-init dir arg overwritten by enrt_init_walker — value here is
     * irrelevant after step-3 wiring. Kept at 0 for clear_slot_scratch
     * sequencing parity. */
    enemy_loop_force_spawn_slow_octorock(1u, 0x80u, 0x80u, 0u);

    /* Step 8 — seed slots 2/3/4 with moblin/goriya/stalfos to verify
     * step-7 dispatch rows tick. Spread X positions so collision
     * heuristics don't pin them.
     * Step 11 — extend with slot 5 = $0B BlueDarknut to verify the new
     * native enrt_update_darknut UPDATE row. */
    enemy_loop_force_spawn_typed(2u, 0x03u, 0x40u, 0x60u, 0u); /* BlueMoblin */
    enemy_loop_force_spawn_typed(3u, 0x05u, 0x40u, 0xA0u, 0u); /* BlueGoriya */
    enemy_loop_force_spawn_typed(4u, 0x2Au, 0xC0u, 0x60u, 0u); /* Stalfos */
    enemy_loop_force_spawn_typed(5u, 0x0Bu, 0xC0u, 0xA0u, 0u); /* BlueDarknut */

    /* Step 8 (Task 7.3) — seed slots 6..11 with the 6 flyer/jumper-family
     * UPDATE rows wired in steps 3-7. Spread across the play-field so
     * collision heuristics don't pin them. Pre-seeded DIR=$01 to satisfy
     * walker-derived rows that use ENEMY_DIR for movement. */
    enemy_loop_force_spawn_typed( 6u, 0x13u, 0x30u, 0x30u, 0x01u); /* Zol     */
    enemy_loop_force_spawn_typed( 7u, 0x15u, 0x30u, 0x70u, 0x01u); /* Gel     */
    enemy_loop_force_spawn_typed( 8u, 0x1Au, 0x30u, 0xB0u, 0x08u); /* Peahat  */
    enemy_loop_force_spawn_typed( 9u, 0x1Bu, 0xD0u, 0x30u, 0x05u); /* Keese   */
    enemy_loop_force_spawn_typed(10u, 0x28u, 0xD0u, 0x70u, 0x01u); /* Rope    */
    enemy_loop_force_spawn_typed(11u, 0x12u, 0xD0u, 0xB0u, 0x01u); /* Vire    */

    alive_after = enemy_loop_alive_count();

    /* Header. */
    block[0] = 0x45u;                          /* 'E' */
    block[1] = 0x4Cu;                          /* 'L' */
    block[2] = (unsigned char)ENEMY_LOOP_PROBE_COUNT;
    block[3] = 0u;

    /* check[0]: alive_count was 0 after room_init (all slots cleared). */
    put_pair(block, 0, (unsigned short)alive_before, 0x0000u);

    /* check[1]: alive_count is 5 after step-11 force_spawn (octorok +
     * moblin + goriya + stalfos + darknut). */
    put_pair(block, 1, (unsigned short)alive_after, 0x0005u);

    /* check[2]: ENEMY_TYPE(1) == 0x07 (RedSlowOctorock). */
    put_pair(block, 2, (unsigned short)ENEMY_TYPE(1), 0x0007u);

    /* check[3]: ENEMY_X(1) == 0x80. */
    put_pair(block, 3, (unsigned short)ENEMY_X(1), 0x0080u);

    /* check[4]: ENEMY_Y(1) == 0x80. */
    put_pair(block, 4, (unsigned short)ENEMY_Y(1), 0x0080u);

    /* check[5]: ENEMY_DIR(1) == 2 — enrt_init_walker computes h_dir
     * (=2 since link_x>=obj_x) and the diff_y(0)<diff_x(0) tie resolves
     * to ENEMY_SCRATCH_Y which holds h_dir. Pre-step-3 expected $00. */
    put_pair(block, 5, (unsigned short)ENEMY_DIR(1), 0x0002u);

    /* check[6]: ENEMY_STATE_TIMER(1) == 0 — overlapping NES cell.
     * ENEMY_STATE_TIMER and OBJ_STATE both map to OBJ($AC,slot). NES
     * InitObject preamble stores slot index into $AC, then octorok
     * init's z07_reset_obj_state zeroes the same byte. Pre-step-3
     * expected $01 (no init was wired); post-step-3 the forwarder
     * fires and the cell ends at 0. */
    put_pair(block, 6, (unsigned short)ENEMY_STATE_TIMER(1), 0x0000u);

    /* check[7]: ENEMY_ALIVE_FLAG(1) == 1. */
    put_pair(block, 7, (unsigned short)ENEMY_ALIVE_FLAG(1), 0x0001u);

    /* check[8]: enemy_loop_get_type(1) == 0x07 (accessor agrees with
     * direct macro read). */
    put_pair(block, 8, (unsigned short)enemy_loop_get_type(1u), 0x0007u);

    /* check[9]: enemy_loop_get_type(2) == 0x03 (BlueMoblin seeded
     * step 8). Pre-step-8 expected 0 (slot empty). */
    put_pair(block, 9, (unsigned short)enemy_loop_get_type(2u), 0x0003u);

    /* Step-3 INIT-side checks. enrt_octorock_common(slot, 32) writes
     * these cells before delegating to enrt_init_walker. All four prove
     * the dispatch row $07 actually fired. */

    /* check[10]: ENEMY_WALK_SPEED(1) == 0x20 — speed arg passed
     * to enrt_octorock_common from enrt_init_slow_octorock_or_ghini. */
    put_pair(block, 10, (unsigned short)ENEMY_WALK_SPEED(1), 0x0020u);

    /* check[11]: ENEMY_MOVE_TIMER(1) == 0x20 — (slot+1)<<4 with slot=1
     * is 2<<4 = $20. NES seeds varied per slot to avoid tick-sync. */
    put_pair(block, 11, (unsigned short)ENEMY_MOVE_TIMER(1), 0x0020u);

    /* check[12]: ENEMY_ANIM_TIMER(1) == 6 — fixed cadence per
     * enrt_octorock_common. */
    put_pair(block, 12, (unsigned short)ENEMY_ANIM_TIMER(1), 0x0006u);

    /* check[13]: OBJ_STATE(1) == 0 — z07_reset_obj_state forwarder
     * dispatched to core_reset_obj_state and zeroed the cell. Proves
     * the forwarder linked + the drained body executed. */
    put_pair(block, 13, (unsigned short)OBJ_STATE(1), 0x0000u);

    /* Step 4 hand-off: move LINK far from the octorok before the
     * gameplay tick takes over. With both at $80,$80 the per-frame
     * c_check_monster_collisions call inside enrt_update_rope sees a
     * collision every tick — the NES collision body clears
     * ENEMY_TYPE(slot), the dispatch finds enemy_update_fns[0] = NULL,
     * and the slot freezes. Park LINK at $00,$00 (off-camera corner;
     * gameplay tick is debug-spawn idle so position is harmless) so the
     * trace probe can capture animation cadence. */
    LINK_X = 0u;
    LINK_Y = 0u;

    /* Step 19 — seed a stationary sword in slot 13 next to slot 1
     * octorok and tee MON_HP(1) low so the drained
     * link_collision_check_monster_collisions chain fires visible damage
     * + death.
     *
     * Slot 13 is OUTSIDE the enemy_loop iterator (1..11), so nothing
     * touches the cells we set here per-frame except the sword's own
     * OBJ_STATE check inside collision_check_monster_sword_collision.
     *
     * collision_check_monster_sword_collision requires:
     *   - ITEM_SWORD_LEVEL >= 1 (otherwise damage = sword_damage_points[0])
     *   - OBJ_STATE(13) == 2 (sword in swing state)
     *   - LINK_DIR drives bbox shape via dir & 0x0C check; non-zero =>
     *     vertical-narrow (12x16), zero => horizontal (16x12)
     *
     * The bbox check then reads OBJ_X(13)/OBJ_Y(13) +6/+8 (or +8/+6
     * depending on dir). With sword at (128,128) and slot 1 octorok at
     * (128,128), both centers coincide and the threshold check passes
     * trivially.
     *
     * MON_HP(1)=$08, sword damage=$10 (level 1) ⇒ HP < dmg ⇒ first hit
     * routes through combat_handle_monster_died ⇒ ROOM_KILL_COUNT++,
     * MON_METASTATE(1)=16, DEATH_FRAME_COUNTER=32. */
    ITEM_SWORD_LEVEL = 1u;
    LINK_DIR = 1u;
    OBJ_STATE(13) = 2u;
    OBJ_X(13)     = 0x80u;
    OBJ_Y(13)     = 0x80u;
    OBJ_DIR(13)   = 1u;
    MON_HP(1)     = STEP19_OCTOROCK_HP_SEED;
}

/* Step 4 live-tick publisher. Called from end of enemy_loop_tick() so
 * Lua can sample slot 1 cell evolution per frame. Proves the UPDATE
 * chain (enrt_update_rope -> walker primitives) actually executes
 * even though no sprite is visible (oam_router NES OAM mirror -> SAT
 * router not yet built; tracked separately). */
/* Step 8 multi-slot publisher. Same call-site as the slot-1 publisher
 * (end of enemy_loop_tick), publishes 4 slots * 8 bytes at $FF7F80 so
 * step-8 probe can verify $03/$05/$2A dispatch rows actually tick.
 *
 * Static helper kept private — only the caller below uses it. */
static void publish_multi_slot(volatile unsigned char *base,
                               unsigned int probe_idx,
                               unsigned int slot)
{
    volatile unsigned char *p = &base[probe_idx * 8u];
    p[0] = (unsigned char)ENEMY_ALIVE_FLAG(slot);
    p[1] = (unsigned char)ENEMY_TYPE(slot);
    p[2] = (unsigned char)ENEMY_X(slot);
    p[3] = (unsigned char)ENEMY_Y(slot);
    p[4] = (unsigned char)ENEMY_DIR(slot);
    p[5] = (unsigned char)ENEMY_ANIM_TIMER(slot);
    p[6] = (unsigned char)ENEMY_DRAW_FRAME(slot);
    p[7] = (unsigned char)ENEMY_WALK_SPEED(slot);
}

/* Phase 7 Task 7.3 step 8 — family-73 publisher. Drops slot 6..11
 * snapshots into the FAMILY73 block so the step-8 lua probe can gate
 * each newly-wired UPDATE row (zol/gel/peahat/keese/rope/vire). */
static void publish_family73(volatile unsigned char *base)
{
    static const unsigned char fam73_slots[6] = { 6u, 7u, 8u, 9u, 10u, 11u };
    base[0] = 0x46u;                                  /* 'F' */
    base[1] = 0x4Du;                                  /* 'M' */
    base[2] = 0u;
    base[3] = 0u;
    for (unsigned int i = 0u; i < 6u; i++) {
        const unsigned int slot = fam73_slots[i];
        volatile unsigned char *p = &base[4u + i * 8u];
        p[0] = (unsigned char)ENEMY_ALIVE_FLAG(slot);
        p[1] = (unsigned char)ENEMY_TYPE(slot);
        p[2] = (unsigned char)ENEMY_X(slot);
        p[3] = (unsigned char)ENEMY_Y(slot);
        p[4] = (unsigned char)ENEMY_DIR(slot);
        p[5] = (unsigned char)ENEMY_ANIM_TIMER(slot);
        p[6] = (unsigned char)ENEMY_MOVE_TIMER(slot);
        p[7] = (unsigned char)ENEMY_FLAP_PHASE(slot);
    }
}

/* Step 14 shot scanner. Walks slots 1..15, records first 8 with
 * ENEMY_TYPE in $53..$5C (any shot/arrow/boomerang). Publishes
 * ActiveMonsterShots ($034C) so probe can verify decrement after
 * shot dies. */
static void publish_shot_scan(volatile unsigned char *base)
{
    base[0] = 0x53u;                                          /* 'S' */
    base[1] = 0x48u;                                          /* 'H' */
    base[2] = (unsigned char)RAM(0x034Cu);                    /* ActiveMonsterShots */

    unsigned int found = 0u;
    for (unsigned int slot = 1u; slot < 16u && found < 8u; slot++) {
        const unsigned char t = (unsigned char)ENEMY_TYPE(slot);
        if (t >= 0x53u && t <= 0x5Cu) {
            volatile unsigned char *e = &base[4u + found * 4u];
            e[0] = (unsigned char)slot;
            e[1] = t;
            e[2] = (unsigned char)ENEMY_X(slot);
            e[3] = (unsigned char)ENEMY_Y(slot);
            found++;
        }
    }
    base[3] = (unsigned char)found;
    /* Zero unused slots so a shrinking found_count is visible. */
    for (unsigned int i = found; i < 8u; i++) {
        volatile unsigned char *e = &base[4u + i * 4u];
        e[0] = 0u; e[1] = 0u; e[2] = 0u; e[3] = 0u;
    }
}

/* Step 19 damage-viz publisher. See header comment at
 * ENEMY_LOOP_DAMAGE_VIZ_BASE for the layout. Captures the slot 1
 * octorok's damage-path cells per-frame so the lua reader can gate
 * "first hit drops HP" / "death anim seen" / "drop spawn observed". */
static void publish_damage_viz(volatile unsigned char *base)
{
    base[0]  = 0x44u;                                      /* 'D' */
    base[1]  = 0x4Du;                                      /* 'M' */
    base[2]  = STEP19_OCTOROCK_HP_SEED;
    base[3]  = (unsigned char)MON_HP(1);
    base[4]  = (unsigned char)MON_HIT_REACTION(1);
    base[5]  = (unsigned char)MON_SHOVE_DIR(1);
    base[6]  = (unsigned char)MON_SHOVE_TIMER(1);
    base[7]  = (unsigned char)MON_METASTATE(1);
    base[8]  = (unsigned char)MON_TYPE(1);
    base[9]  = (unsigned char)DEATH_FRAME_COUNTER;
    base[10] = (unsigned char)ROOM_KILL_COUNT;
    base[11] = (unsigned char)OBJ_STATE(13);
    base[12] = (unsigned char)COMBAT_HARM_FLAG;
    base[13] = (unsigned char)ROOM_OW_CUR_KILL_TOTAL;  /* step 20: NES RoomKillCount $034F */
    base[14] = 0u;
    base[15] = 0u;
}

/* Step 17 collision-viz publisher. Drops the live counter values from
 * c_check_monster_collisions / c_check_link_collision wrappers into the
 * collision-viz block. Counter > 0 + monotonic growth proves the call
 * path runs every tick across the seeded slot set. */
static void publish_collision_viz(volatile unsigned char *base)
{
    unsigned long mc = g_check_monster_collisions_calls;
    unsigned long lc = g_check_link_collision_calls;

    base[0] = 0x43u;                                  /* 'C' */
    base[1] = 0x56u;                                  /* 'V' */
    base[2] = (unsigned char)((mc >> 24) & 0xFFu);
    base[3] = (unsigned char)((mc >> 16) & 0xFFu);
    base[4] = (unsigned char)((mc >>  8) & 0xFFu);
    base[5] = (unsigned char)( mc        & 0xFFu);
    base[6] = (unsigned char)((lc >> 24) & 0xFFu);
    base[7] = (unsigned char)((lc >> 16) & 0xFFu);
    base[8] = (unsigned char)((lc >>  8) & 0xFFu);
    base[9] = (unsigned char)( lc        & 0xFFu);
}

void enemy_loop_probe_publish_live(void)
{
    volatile unsigned char *block =
        (volatile unsigned char *)ENEMY_LOOP_TICK_PROBE_BASE;
    volatile unsigned char *multi =
        (volatile unsigned char *)ENEMY_LOOP_MULTI_SLOT_BASE;
    volatile unsigned char *shot_scan =
        (volatile unsigned char *)ENEMY_LOOP_SHOT_SCAN_BASE;
    volatile unsigned char *coll_viz =
        (volatile unsigned char *)ENEMY_LOOP_COLLISION_VIZ_BASE;
    volatile unsigned char *dmg_viz =
        (volatile unsigned char *)ENEMY_LOOP_DAMAGE_VIZ_BASE;
    volatile unsigned char *fam73 =
        (volatile unsigned char *)ENEMY_LOOP_FAMILY73_BASE;
    static unsigned short frame_counter = 0u;
    frame_counter++;

    /* Step 8 multi-slot block. Slot 1 octorok ($07) + 2 moblin ($03) +
     * 3 goriya ($05) + 4 stalfos ($2A) + 5 darknut ($0B, step 11). */
    publish_multi_slot(multi, 0u, 1u);
    publish_multi_slot(multi, 1u, 2u);
    publish_multi_slot(multi, 2u, 3u);
    publish_multi_slot(multi, 3u, 4u);
    publish_multi_slot(multi, 4u, 5u);

    publish_shot_scan(shot_scan);
    publish_collision_viz(coll_viz);
    publish_damage_viz(dmg_viz);
    publish_family73(fam73);

    block[0]  = 0x54u;                                /* 'T' */
    block[1]  = 0x4Bu;                                /* 'K' */
    block[2]  = (unsigned char)(frame_counter >> 8);
    block[3]  = (unsigned char)(frame_counter & 0xFFu);
    block[4]  = (unsigned char)ENEMY_ALIVE_FLAG(1);
    block[5]  = (unsigned char)ENEMY_TYPE(1);
    block[6]  = (unsigned char)ENEMY_X(1);
    block[7]  = (unsigned char)ENEMY_Y(1);
    block[8]  = (unsigned char)ENEMY_DIR(1);
    block[9]  = (unsigned char)ENEMY_ANIM_TIMER(1);
    block[10] = (unsigned char)ENEMY_DRAW_FRAME(1);
    block[11] = (unsigned char)ENEMY_MOVE_TIMER(1);
    block[12] = (unsigned char)ENEMY_STATE_TIMER(1);
    block[13] = (unsigned char)ENEMY_WALK_SPEED(1);
    block[14] = (unsigned char)LINK_X;
    block[15] = (unsigned char)LINK_Y;
}

void enemy_loop_probe_publish_pre(void)
{
    volatile unsigned char *block =
        (volatile unsigned char *)ENEMY_LOOP_TICK_PRE_PROBE_BASE;
    /* Raw absolute pointer to the byte ENEMY_TYPE(1) maps to.
     * Debug.md A4 = $FF8000 (set by src/debug/a4_probe_asm.s entry).
     * NES OBJ_TYPE+1 offset = $034F + 1 = $0350 → physical $FF8350.
     * Bypasses the A4-pinned `nes_ram` register binding to confirm the
     * macro path and the absolute path agree. */
    volatile unsigned char *raw_type1 =
        (volatile unsigned char *)0x00FF8350UL;
    static unsigned short pre_counter = 0u;
    pre_counter++;

    block[0]  = 0x50u;                                /* 'P' */
    block[1]  = 0x52u;                                /* 'R' */
    block[2]  = (unsigned char)(pre_counter >> 8);
    block[3]  = (unsigned char)(pre_counter & 0xFFu);
    block[4]  = (unsigned char)ENEMY_ALIVE_FLAG(1);
    block[5]  = (unsigned char)ENEMY_TYPE(1);
    block[6]  = (unsigned char)ENEMY_X(1);
    block[7]  = (unsigned char)ENEMY_Y(1);
    block[8]  = (unsigned char)ENEMY_DIR(1);
    block[9]  = (unsigned char)ENEMY_ANIM_TIMER(1);
    block[10] = (unsigned char)ENEMY_DRAW_FRAME(1);
    block[11] = (unsigned char)ENEMY_MOVE_TIMER(1);
    block[12] = (unsigned char)ENEMY_STATE_TIMER(1);
    block[13] = (unsigned char)ENEMY_WALK_SPEED(1);
    block[14] = (unsigned char)LINK_X;
    block[15] = (unsigned char)LINK_Y;
    /* [16] raw byte at $FF0350 (what ENEMY_TYPE(1) should read).
     * If raw == $07 but block[5] (ENEMY_TYPE via A4) == $00, A4 is
     * broken. If both $00, the cell genuinely got cleared. */
    block[16] = *raw_type1;
}
