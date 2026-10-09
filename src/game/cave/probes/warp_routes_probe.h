/* Phase E (2026-05-24) — Warp routes static dispatch probe.
 *
 * Boot-time self-test: calls cave_entrance_check(0x24, room_id) for
 * all 128 OW rooms + publishes results at WARP_ROUTES_PROBE_BASE for
 * tools/debug/probes/probe_warp_routes.lua to byte-diff against
 * tools/parity/warp_routes_expected.json.
 *
 * Does NOT require BizHawk joypad scripting: the dispatch is pure-
 * functional (reads rooms_overworld[] static, no Link/scene state),
 * so running once after level_info_install_ow() loads LBA_B is
 * sufficient.
 *
 * Block layout @ WARP_ROUTES_PROBE_BASE = 0xFF7C00 (free per RoomRom
 * Debug RAM Map; OW raw-tile + UW door at $7400..$77CF, UW walkability
 * cache at $7800..$7AC3, pushblock persist at $7B00..$7BFF, options
 * own $7E80, enemy own $7E00..$7FE7. The $7C00..$7DFF gap is free):
 *   [0]   = 'W'  (0x57) magic
 *   [1]   = 'R'  (0x52) magic
 *   [2]   = WARP_ROUTES_PROBE_VERSION (1)
 *   [3]   = no_warp count   (oracle expects 36)
 *   [4]   = dungeon count   (oracle expects 14)
 *   [5]   = cave count      (oracle expects 78 = 74 regular + 4 shortcut)
 *   [6]   = reserved
 *   [7]   = reserved
 *   [8..135]  = cave_id_t result per room_id (0..127). 0 = no_warp or
 *               dungeon (caller falls through to detect_warp_ow);
 *               non-zero = cave_id ($6A..$7D).
 *   [136..263] = raw attr_b read per room_id (diagnostic). Lets the
 *                differ confirm whether mismatches come from the blob
 *                read or the cave_entrance_check dispatch.
 *
 * Phase F follow-up: extends block with dungeon-exit detect_warp_uw_to_ow
 * results per (level, quest) at a separate offset. */

#ifndef SRC_GAME_CAVE_PROBES_WARP_ROUTES_PROBE_H
#define SRC_GAME_CAVE_PROBES_WARP_ROUTES_PROBE_H

#define WARP_ROUTES_PROBE_BASE      0x00FF7C00UL
#define WARP_ROUTES_PROBE_VERSION   0x02u
#define WARP_ROUTES_PROBE_ROOMS     128u
#define WARP_ROUTES_PROBE_RESULTS_OFFSET   8u
#define WARP_ROUTES_PROBE_ATTR_B_OFFSET    136u   /* 8 + 128 */

void warp_routes_probe_run(void);

#endif
