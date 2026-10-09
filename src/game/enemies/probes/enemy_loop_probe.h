#ifndef SRC_GAME_ENEMIES_PROBES_ENEMY_LOOP_PROBE_H
#define SRC_GAME_ENEMIES_PROBES_ENEMY_LOOP_PROBE_H

/* Phase 7 Task 7.2 step 2 — in-ROM verification probe.
 *
 * Runs at boot AFTER enemy_loop_room_init() + a deterministic
 * enemy_loop_force_spawn_slow_octorock() seed. Publishes (actual,
 * expected) u16 pairs to ENEMY_LOOP_PROBE_BASE so
 * tools/debug/probes/probe_walker_parity.lua can verify:
 *   - room_init clears slots
 *   - force_spawn writes the right cells (TYPE/X/Y/DIR/STATE_TIMER/ALIVE)
 *   - enemy_loop_alive_count + enemy_loop_get_type accessors agree
 *
 * Block layout @ ENEMY_LOOP_PROBE_BASE = 0xFF7E00 (free per
 * RoomRom Debug RAM Map; $FF7400..$FF77CF is OW raw-tile + UW door
 * persistence — DO NOT collide):
 *   [0]  = 'E'                        (0x45) magic
 *   [1]  = 'L'                        (0x4C) magic
 *   [2]  = check_count (count of u16 pairs that follow)
 *   [3]  = 0 (reserved)
 *   [4..] = check_count * 4 bytes: actual_be_u16 || expected_be_u16
 */

#define ENEMY_LOOP_PROBE_BASE   0x00FF7E00UL
#define ENEMY_LOOP_PROBE_COUNT  14u

/* Step 4 live-tick block. Published every frame at end of
 * enemy_loop_tick() so probe_walker_tick_trace.lua can sample slot 1
 * cell evolution and prove the UPDATE chain is firing per frame even
 * when no sprite is visible (oam_router gap separately tracked).
 *
 * Layout @ ENEMY_LOOP_TICK_PROBE_BASE = 0xFF7F00:
 *   [0]  = 'T' (0x54) magic
 *   [1]  = 'K' (0x4B) magic
 *   [2..3] = frame_counter (be u16, increments per tick call)
 *   [4]  = ENEMY_ALIVE_FLAG(1)
 *   [5]  = ENEMY_TYPE(1)
 *   [6]  = ENEMY_X(1)
 *   [7]  = ENEMY_Y(1)
 *   [8]  = ENEMY_DIR(1)
 *   [9]  = ENEMY_ANIM_TIMER(1)
 *   [10] = ENEMY_DRAW_FRAME(1)
 *   [11] = ENEMY_MOVE_TIMER(1)
 *   [12] = ENEMY_STATE_TIMER(1)
 *   [13] = ENEMY_WALK_SPEED(1)
 *   [14] = LINK_X (slot 0)
 *   [15] = LINK_Y (slot 0)
 */
#define ENEMY_LOOP_TICK_PROBE_BASE 0x00FF7F00UL

/* Step 4 pre-tick block at $FF7F40 — same byte layout as live block but
 * published BEFORE the slot iterator runs each frame. If pre-tick TYPE
 * is $07 but post-tick TYPE is $00, the dispatch / update body zeroed
 * the cell. If pre-tick is already $00, something between init and the
 * first tick wiped it. */
#define ENEMY_LOOP_TICK_PRE_PROBE_BASE 0x00FF7F40UL

/* Step 8 multi-slot block at $FF7F80. 5 slots * 8 bytes = 40 bytes.
 * Per-slot layout (offset = (slot_idx-1) * 8 within block):
 *   [0] = ENEMY_ALIVE_FLAG
 *   [1] = ENEMY_TYPE
 *   [2] = ENEMY_X
 *   [3] = ENEMY_Y
 *   [4] = ENEMY_DIR
 *   [5] = ENEMY_ANIM_TIMER
 *   [6] = ENEMY_DRAW_FRAME
 *   [7] = ENEMY_WALK_SPEED
 *
 * Slot 1=octorock $07, 2=moblin $03, 3=goriya $05, 4=stalfos $2A,
 *      5=darknut $0B (step 11).
 * Used by step-8/11 multi-slot trace to verify walker dispatch rows
 * tick + move without crashing. */
#define ENEMY_LOOP_MULTI_SLOT_BASE 0x00FF7F80UL

/* Step 14 shot-scan block at $FF7FA8 (after multi-slot 5*8=40 bytes
 * ending at $FF7FA7). Scans slots 1..15 every frame for any ENEMY_TYPE
 * in $53..$5C (shot/arrow/boomerang). Records first 8 hits.
 * Layout:
 *   [0]   = 'S' (0x53) magic
 *   [1]   = 'H' (0x48) magic
 *   [2]   = ActiveMonsterShots ($034C)
 *   [3]   = scan_count (number of shot slots found, 0..8)
 *   [4..] = up to 8 entries x 4 bytes: [slot, type, x, y]
 * Total = 4 + 32 = 36 bytes. Block end = $FF7FCB.
 *
 * Used by step-14 probe extension to verify shot UPDATE rows fire on
 * dynamically-spawned shot slots. */
#define ENEMY_LOOP_SHOT_SCAN_BASE 0x00FF7FA8UL

/* Step 17 collision-viz block at $FF7FCC (after shot-scan 36-byte block
 * ending at $FF7FCB). Publishes the per-call counters incremented from
 * c_check_monster_collisions / c_check_link_collision wrappers in
 * src/game/enemies/enemy_walker_bridge.c.
 *
 * Layout (10 bytes):
 *   [0]    = 'C' (0x43) magic
 *   [1]    = 'V' (0x56) magic
 *   [2..5] = g_check_monster_collisions_calls (be u32)
 *   [6..9] = g_check_link_collision_calls    (be u32)
 *
 * Counters > 0 across the trace prove c_walker_move's downstream
 * c_check_monster_collisions invocation actually fires per-slot per-tick
 * (movement+collision walker checklist item). */
#define ENEMY_LOOP_COLLISION_VIZ_BASE 0x00FF7FCCUL

/* Step 19 damage block at $FF7FD8 (after collision-viz 10-byte block
 * ending at $FF7FD5; pad 2 to 8-byte align). Captures the slot 1
 * monster's damage-path cells. Probe init seeds a stationary sword in
 * slot 13 (OBJ_STATE=2) at the slot 1 octorok's spawn coords + sets
 * MON_HP(1) to a known value so the drained
 * link_collision_check_monster_collisions chain can fire visible damage
 * via collision_check_monster_sword_collision -> stabbing -> deal_damage.
 *
 * Layout (16 bytes):
 *   [0]   = 'D' (0x44) magic
 *   [1]   = 'M' (0x4D) magic
 *   [2]   = MON_HP(1) seed (constant — written once in probe init)
 *   [3]   = MON_HP(1) live
 *   [4]   = MON_HIT_REACTION(1) live (16 set on damage)
 *   [5]   = MON_SHOVE_DIR(1) live (top bit set on shove)
 *   [6]   = MON_SHOVE_TIMER(1) live (64 set on damage)
 *   [7]   = MON_METASTATE(1) live (16 set on death)
 *   [8]   = MON_TYPE(1) live (clears to 0x60 when drop spawns)
 *   [9]   = DEATH_FRAME_COUNTER live (32 set on death)
 *   [10]  = ROOM_KILL_COUNT live (++ on each kill)
 *   [11]  = OBJ_STATE(13) live (sword swing state, expect stays 2)
 *   [12]  = COMBAT_HARM_FLAG live (++ on harm; from begin_shove)
 *   [13]  = ROOM_OW_CUR_KILL_TOTAL ($034F NES RoomKillCount, step 20)
 *   [14..15] = reserved
 *
 * Block end = $FF7FE7. Used by step-19 probe to gate "damage+death+drop"
 * walker checklist line. */
#define ENEMY_LOOP_DAMAGE_VIZ_BASE 0x00FF7FD8UL

/* Phase 7 Task 7.3 step 8 — flyer/jumper/zol/gel/rope/vire family-73
 * multi-slot block at $FF7E40 (free between probe-pairs block ending
 * $FF7E3B and tick block at $FF7F00). 6 slots * 8 bytes + 4 header.
 *
 * Layout (52 bytes):
 *   [0]   = 'F' (0x46) magic
 *   [1]   = 'M' (0x4D) magic
 *   [2]   = (reserved)
 *   [3]   = (reserved)
 *   [4..] = 6 entries x 8 bytes (per-slot):
 *     [0] ALIVE_FLAG  [1] TYPE        [2] X       [3] Y
 *     [4] DIR         [5] ANIM_TIMER  [6] MOVE_TIMER  [7] FLAP_PHASE
 *
 * Slot mapping (by entry index):
 *   entry 0 -> slot  6 = $13 Zol
 *   entry 1 -> slot  7 = $15 Gel
 *   entry 2 -> slot  8 = $1A Peahat
 *   entry 3 -> slot  9 = $1B BlueKeese
 *   entry 4 -> slot 10 = $28 Rope
 *   entry 5 -> slot 11 = $12 Vire
 *
 * Used by step-8 family-functional probe to gate 6 wired UPDATE rows. */
#define ENEMY_LOOP_FAMILY73_BASE 0x00FF7E40UL

/* Opt-in control for the heavy in-ROM enemy stress probe. Normal debug
 * gameplay must not run enemy_loop_probe_run(), because that force-spawns
 * an 11-slot stress harness and makes A+B+C debug mode crawl. Probe Lua
 * scripts that need the harness write "EP" here before entering RoomRom. */
#define ENEMY_LOOP_PROBE_CONTROL_BASE 0x00FF73FCUL
#define ENEMY_LOOP_PROBE_ARM0         0x45u  /* 'E' */
#define ENEMY_LOOP_PROBE_ARM1         0x50u  /* 'P' */

#define DEBUG_PROBE_CONTROL_BASE      0x00FF73F8UL
#define DEBUG_PROBE_ARM0              0x52u  /* 'R' */
#define DEBUG_PROBE_ARM1              0x50u  /* 'P' */
#define DEBUG_PROBE_FLAGS_OFF         2u
#define DEBUG_PROBE_ENEMY_STRESS      0x02u

#ifdef __cplusplus
extern "C" {
#endif

unsigned char enemy_loop_probe_is_armed(void);
void enemy_loop_probe_run(void);
void enemy_loop_probe_publish_live(void);
void enemy_loop_probe_publish_pre(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_ENEMIES_PROBES_ENEMY_LOOP_PROBE_H */
