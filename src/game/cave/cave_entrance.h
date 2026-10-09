/* cave_entrance.h — OW→cave entrance detection per NES Z_05.asm:HandleWarpOW.
 *
 * NES source: reference/aldonunez/Z_05.asm:7313 HandleWarpOW.
 * Drained C:  NEW (src/game/cave/cave_entrance.c).
 * Coverage:   PARTIAL — tile-range + LBA_B selector + cave-id derivation;
 *             Mode B vs C distinction left to cave_dispatch internals.
 * Stance:     REPLACE (Tier 0 hardcoded cave_id $6A replaced with NES-
 *             aligned per-room dispatch from LevelBlockAttrsB).
 *
 * Returns:
 *   - non-zero cave_id_t when tile is a warp tile AND attr_b_fc routes to
 *     a cave (selector $40-$FC, excluding $00 and < $40 dungeons).
 *   - 0 when tile is non-warp, OR when warp routes to a dungeon
 *     (caller dispatches dungeon via detect_warp_ow in transition.c).
 */

#ifndef SRC_GAME_CAVE_CAVE_ENTRANCE_H
#define SRC_GAME_CAVE_CAVE_ENTRANCE_H

#include "cave_dispatch.h"  /* cave_id_t */

/* Tier 1: per-room cave_id derivation via LevelBlockAttrsB[room_id]. */
cave_id_t cave_entrance_check(unsigned char tile, unsigned char room_id);

/* NES CheckSubroom Mode $0C: return the OW room reached by one of the
 * three shortcut stairs, or $FF when Link is not on a stair. Reads the
 * four installed LevelInfo_CellarRoomIdArray entries at $6BB2. */
unsigned char cave_shortcut_destination(unsigned char source_room,
                                        unsigned char link_x,
                                        unsigned char link_y,
                                        unsigned char grid_offset);

#endif /* SRC_GAME_CAVE_CAVE_ENTRANCE_H */
