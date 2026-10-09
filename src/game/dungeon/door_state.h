/* uw_door_state.h — Ph5.3: UW door type lookup, per-room state, tile patching.
 *
 * D1 headers for all functions in uw_door_state.c:
 *   NES source:  Z_05.asm:FindDoorAttrByDoorBit, TouchDoor*, LayOutDoors,
 *                UpdateDoors, CheckShutters, SetDoorFlag/ResetDoorFlag
 *   Drained C:   NONE (room_dispatch.c has Z_05 drain stubs, but Phase 5
 *                active scope is engine/src only — no cross-scope import)
 *   Coverage:    NONE
 *   Stance:      GREENFIELD (drain_coverage.py reports 0 candidates in scope)
 */
#ifndef UW_DOOR_STATE_H
#define UW_DOOR_STATE_H

/* --- Door direction indices (match NES DoorBits: E=0, W=1, S=2, N=3) --- */
#define DOOR_DIR_E  0u
#define DOOR_DIR_W  1u
#define DOOR_DIR_S  2u
#define DOOR_DIR_N  3u
#define DOOR_DIR_COUNT 4u

/* --- Door type values (NES Z_05.asm:FindDoorAttrByDoorBit attribute bits) --- */
#define DOOR_TYPE_OPEN      0u  /* always passable — blob tiles already walkable */
#define DOOR_TYPE_WALL      1u  /* permanent solid — no state change ever */
#define DOOR_TYPE_FALSE     2u  /* walk-through: blocks $18 frames, then passes */
#define DOOR_TYPE_FALSE2    3u  /* false wall variant (same behaviour as FALSE) */
#define DOOR_TYPE_BOMBABLE  4u  /* solid until bomb detonates adjacent */
#define DOOR_TYPE_KEY       5u  /* solid until key consumed on touch */
#define DOOR_TYPE_KEY2      6u  /* boss-key variant (same key logic, Phase 5.3) */
#define DOOR_TYPE_SHUTTER   7u  /* solid until room cleared; opens on enemy death */

/* --- Opened-door bitmask (NES CurOpenedDoors: bit-per-direction) --- */
#define DOOR_BIT_E   0x01u
#define DOOR_BIT_W   0x02u
#define DOOR_BIT_S   0x04u
#define DOOR_BIT_N   0x08u

/* Map direction index to bit. */
#define DOOR_DIR_BIT(dir)  ((unsigned char)(1u << (dir)))

/* --- API ----------------------------------------------------------------- */

/* Initialise door state for a fresh room visit.
 * Reads door types from rooms_dungeons[] LevelBlock AttrsA/AttrsB.
 * Restores persisted KEY/BOMBABLE open state; SHUTTER starts closed. */
void uw_door_state_room_init(unsigned char level,
                              unsigned char quest,
                              unsigned char room_id);

/* Return door type (DOOR_TYPE_*) for the given direction in current room. */
unsigned char uw_door_state_get_type(unsigned char dir);

/* Return current opened bitmask (DOOR_BIT_* OR'd together). */
unsigned char uw_door_state_get_opened(void);

/* Returns 1 if door in given direction is currently passable. */
unsigned char uw_door_state_is_open(unsigned char dir);

/* Touch door in `dir` during movement.
 * `keys` — pointer to caller's key-count inventory.
 * Returns 1 = passable, 0 = blocked.
 * Side-effects:
 *   KEY/KEY2 — decrements *keys (if > 0) and marks opened + patches tiles.
 *   FALSE/FALSE2 — starts $18-frame block timer on first touch.
 *   SHUTTER — open only if bit already in opened mask.
 *   BOMBABLE — open only if bit already in opened mask.
 *   OPEN — always 1.
 *   WALL — always 0. */
unsigned char uw_door_state_touch(unsigned char dir, unsigned char *keys);

/* Tick false-wall timer (call once per frame when in UW).
 * Decrements s_false_timer toward 0; no-op when already 0. */
void uw_door_state_tick(void);

/* Open door(s) specified by bitmask (OR of DOOR_BIT_*).
 * Intended for shutter trigger (room-clear event) and bomb detonation.
 * Patches plane tiles and updates walkability for each newly-opened door. */
void uw_door_state_open_by_mask(unsigned char dir_mask);

/* Override walkability cells at door metatile positions.
 * Must be called AFTER blit_blob builds s_uw_walkable (or after any
 * state change that opens a door, since blit_blob doesn't know state). */
void uw_door_state_apply_walkability(void);

/* Patch plane tiles at a door's position to show open state.
 * Writes NES open-threshold tile IDs; palette looked up from blob attr table.
 * Safe to call multiple times (idempotent). */
void uw_door_state_patch_open_tiles(unsigned char dir);

/* Returns 1 if any shutter doors exist in the current room. */
unsigned char uw_door_state_has_shutters(void);

/* Open all shutter doors in the current room (call on room-clear event). */
void uw_door_state_trigger_shutters(void);

/* Clear all persisted state (call on game-reset / new-game). */
/* T-119: NES door-state machine (UpdateDoors / CheckShutters), run once
 * per UW play frame. */
void uw_door_state_update(void);
/* Scroll entry: Link's NES direction of travel; the next room_init sets
 * the entering doorway as CurOpenedDoors (InitMode7_Sub1). */
void uw_door_state_set_entering(unsigned char nes_dir);
/* TriggerOpenDoor for a direction bit (bombable wall hit). */
void uw_door_state_trigger_open(unsigned char dir_bit);
/* Re-lay all door faces after the plane is repainted. */
void uw_door_state_layout_all(void);

/* Task 5.5 accessors for the state-mirror publisher. Read-only views
 * of internal state needed by BizHawk Lua probes. */
unsigned char uw_door_state_false_timer(void);
unsigned char uw_door_state_current_level(void);
/* Copy persistence row (128 bytes) for the active level into `dst`. */
void uw_door_state_copy_persist_for_active_level(unsigned char *dst,
                                                  unsigned short dst_size);

#endif /* UW_DOOR_STATE_H */
