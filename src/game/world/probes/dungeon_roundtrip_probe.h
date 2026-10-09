/* Phase F (2026-05-25) — Dungeon round-trip synthetic verifier.
 *
 * Boot-time self-test: for each of the 18 (level, quest) tuples,
 * looks up the start_room from the Phase B manifest, scans the UW
 * blob for a stair tile, pre-latches a canonical OW source, drives
 * detect_warp_uw_to_ow via the Phase F probe forwarder, captures the
 * outcome, and records pass_flags.
 *
 * Verifies Phase B (manifest hit) + Phase C (UW→OW dispatch +
 * source replay) end-to-end without joypad scripting / savestates.
 *
 * Block layout @ $FF7DB0 (80 bytes; sits in the free region between
 * the Phase E warp_routes probe block ending at $FF7DAF and the
 * Phase 9 options probe block starting at $FF7E80):
 *   [0]   = 'W'  (0x57) magic
 *   [1]   = 'F'  (0x46) magic
 *   [2]   = version (1)
 *   [3]   = rows_ok count (= 18 on full pass)
 *   [4..7]  = reserved
 *   [8..79] = 18 rows × 4 bytes = 72 bytes; row index = (level-1)*2 + (quest-1)
 *      [0] = outcome.dest_scene  (expected SCENE_OW = 0)
 *      [1] = outcome.dest_level  (expected 0)
 *      [2] = outcome.dest_room_id (expected = source_room_id = start_room)
 *      [3] = pass_flags
 *              bit 0 = manifest hit
 *              bit 1 = stair tile found
 *              bit 2 = detect_warp_uw_to_ow fired
 *              bit 3 = outcome.dest_link_x/y matched source latch
 */

#ifndef SRC_GAME_WORLD_PROBES_DUNGEON_ROUNDTRIP_PROBE_H
#define SRC_GAME_WORLD_PROBES_DUNGEON_ROUNDTRIP_PROBE_H

#define DUNGEON_ROUNDTRIP_PROBE_BASE     0x00FF7DB0UL
#define DUNGEON_ROUNDTRIP_PROBE_VERSION  0x01u
#define DUNGEON_ROUNDTRIP_PROBE_ROWS     18u
#define DUNGEON_ROUNDTRIP_PROBE_HEADER   8u
#define DUNGEON_ROUNDTRIP_PROBE_ROW_BYTES 4u

void dungeon_roundtrip_probe_run(void);

#endif
