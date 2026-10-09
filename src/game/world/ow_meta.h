/* OW LevelBlock attribute accessor (engine-side).
 *
 * Reads the 6 sub-tables of the overworld LevelBlock from the
 * `rooms_overworld[]` blob (see data/rooms/overworld.c) using the
 * canonical layout pinned in data/rooms/overworld_offsets.h.
 *
 * Pure metadata access. No rendering, no scene state, no game-flow
 * decisions. The warp coordinator (roomrom_world_transition) consumes
 * these for selector resolution per NES Z_05.asm:HandleWarpOW.
 */

#ifndef ROOMROM_OW_ROOM_META_H
#define ROOMROM_OW_ROOM_META_H

unsigned char roomrom_ow_meta_attr_a(unsigned char room_id);
unsigned char roomrom_ow_meta_attr_b(unsigned char room_id);

/* selector = attr_b & 0xFC. Only meaningful for warp dispatch. */
unsigned char roomrom_ow_meta_level_selector(unsigned char room_id);

/* Per NES HandleWarpOW: selector < 0x40 routes to a level load,
 * selector >= 0x40 routes to a cave (slice 1: caves deferred). */
unsigned char roomrom_ow_meta_is_level_selector(unsigned char selector);

/* level number (1..9) derived from a level-class selector via >> 2.
 * Caller must verify roomrom_ow_meta_is_level_selector(selector) first. */
unsigned char roomrom_ow_meta_level_from_selector(unsigned char selector);

/* Per NES HandleWarpOW (Z_05.asm:7344-7353): selector $50 routes to
 * Mode $0C (shortcut cave); all other selectors >= $40 route to
 * Mode $0B (regular cave). Returns 1 if selector is the shortcut, 0
 * if regular cave or non-cave. */
unsigned char roomrom_ow_meta_is_shortcut_cave_selector(unsigned char selector);

/* Cave-id derivation per NES (cave_idx 0..19 = $6A..$7D):
 *   cave_idx = (selector - $40) >> 2
 *   cave_id  = $6A + cave_idx
 * Caller must verify selector >= $40 (i.e., NOT a level selector).
 * Returns $6A..$7D. */
unsigned char roomrom_ow_meta_cave_id_from_selector(unsigned char selector);

#endif /* ROOMROM_OW_ROOM_META_H */
