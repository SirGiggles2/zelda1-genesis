/* progress_dispatch.h — native progress subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/world/progress_runtime.c. Both ROMs
 * link. Pure C, no transpile shims. Phase 4 first batch focuses on
 * the room-flag persistence pair that unblocks Phase 3 cave stubs:
 *
 *   set_room_flag_uw_item_state — mark current room's UW-item-taken bit.
 *   get_room_flag_uw_item_state — read current room's UW-item-taken bit.
 *
 * Both pair up to gate cave's "this item has already been taken" path
 * (cave_runtime.c branches on the result).
 */

#ifndef PROGRESS_DISPATCH_H
#define PROGRESS_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Set bit $10 ("UW item taken / cave object destroyed") on the
 * current room's flag byte in the active save slot. Mirrors NES
 * SetRoomFlagUWItemState (drain at progress_runtime.c:32-38). */
void progress_set_room_flag_uw_item_state(void);

/* Read bit $10 of the current room's flag byte from the active save
 * slot. Returns the bit value masked (0 or $10). Mirrors NES
 * GetRoomFlagUWItemState (drain at progress_runtime.c:40-48). */
unsigned char progress_get_room_flag_uw_item_state(void);

/* Replace a 7th palette row in the dynamic transfer buffer (8-byte
 * record + 3-byte color triple at RAM($0306..$0308)). NES uses 3
 * variants seeded by GanonColorTriples offset:
 *   brown -> color_index = 2 (GanonColorTriples[0..2])
 *   blue  -> color_index = 5 (GanonColorTriples[3..5])
 *   ashes -> color_index = 8 (GanonColorTriples[6..8])
 *
 * Drain at progress_runtime.c:8-19, 21-23. Mirrors NES Z_01.asm
 * ReplaceGanonBrownPaletteRow / ReplaceGanonBluePaletteRow /
 * ReplaceAshesPaletteRow. */
void progress_replace_ganon_brown_palette_row(void);
void progress_replace_ganon_blue_palette_row(void);
void progress_replace_ashes_palette_row(void);

/* Zero ROOM_TILE_OBJ_0/1/2 ($052B..$052D) and return 0. NES
 * ResetRoomTileObjInfo. drain at progress_runtime.c:25-30. */
unsigned char progress_reset_room_tile_obj_info(void);

/* Bomb-flash mask animation: shifts CUR_INV_TILE left/right based on
 * the bomb's OBJ_MOVE_TIMER phase. Mirrors NES UpdateBombFlashEffect.
 * drain at progress_runtime.c:50-65. */
void progress_update_bomb_flash_effect(unsigned int slot);

/* Scan slots 12..1 for blocking tile-object monsters (types $68/$62/$65/$66
 * with OBJ_STATE == 1) within $10 px of Link. On hit, clear
 * COMBAT_PART_INDEX. drain at progress_runtime.c:134-152. */
void progress_check_tile_objects_blocking(void);

/* Per-frame Power Triforce fanfare driver: when curtain timer expires,
 * replace ashes palette + trigger SFX + redraw HUD + clear Link's
 * action timer + clear the fanfare flag. While timer ticks, alternates
 * the room transfer buf selector based on phase. drain at
 * progress_runtime.c:154-168. */
void progress_check_power_triforce_fanfare(void);

/* Update one map-marker slot (idx) with the screen position of the
 * given room_id. Decodes room_id grid into (row, col), writes
 * MAP_MARKER_Y/X/TILE/ATTR. idx==0 is Link (always full-bright);
 * idx>0 is a per-level item marker (flashing if not yet found).
 * Mirrors NES UpdatePositionMarker. drain at progress_runtime.c:67-96. */
void progress_update_position_marker(unsigned char room_id, unsigned int idx);

/* No-arg variant: Link's marker for the current room. Skipped if
 * MODE_VALUE == 9 (game over) or PLAYER_MARKER_DISABLE set. drain
 * at progress_runtime.c:98-102. */
void progress_update_player_position_marker(void);

/* Copy the 14-byte save-file pointer set for the current save slot
 * (SAVE_SLOT_INDEX) into ZP RAM($00..$0D), backwards from the end
 * of the block. Then write RAM($0E)=$7F, RAM($0F)=$06 as the
 * "items pointer" trailer. Mirrors NES FetchFileAAddressSet (Z_01.asm:3030). */
void progress_fetch_file_a_address_set(void);

/* If CURTAIN_TIMER == 0, advance one column-pair through the curtain
 * effect: copy two columns, decrement CURTAIN_LEFT_COL, increment
 * CURTAIN_RIGHT_COL, reset CURTAIN_TIMER=5. NES UpdateWorldCurtainEffect.
 * drain at progress_runtime.c:104-117. */
void progress_update_world_curtain_effect(void);

/* Forward to progress_update_world_curtain_effect — separate symbol
 * exists because the NES dispatch table calls it through a different
 * bank entry. NES UpdateWorldCurtainEffect_Bank2.
 * drain at progress_runtime.c:119-121. */
void progress_update_world_curtain_effect_bank2(void);

#ifdef __cplusplus
}
#endif

#endif /* PROGRESS_DISPATCH_H */
