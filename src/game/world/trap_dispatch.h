/* trap_dispatch.h — native trap subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/world/trap_runtime.c. Both ROMs link.
 * Phase 4 first batch: trivial leaves (advance_teleporting_level_index).
 * Larger functions (whirlwind init/draw/update, summon, init_mode_b_enter_cave,
 * check_passive_tile_objects, update_trap, update_rupee_stash) defer to
 * future batches because they reach into transpile shims +
 * cross-subsystem state (link_collision, c_draw_object,
 * c_go_to_next_mode_from_play, z01_check_link_collision, etc.).
 */

#ifndef TRAP_DISPATCH_H
#define TRAP_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Increment TELEPORT_LEVEL_INDEX, then back off by 2 if the LINK_DIR
 * vertical component (bits $09 = up + right) is clear. Effectively
 * keeps the teleport cycle on horizontal moves. Mirrors NES
 * AdvanceTeleportingLevelIndex. drain at trap_runtime.c:81-86. */
void trap_advance_teleporting_level_index(void);

/* If TELEPORT_ACTIVE_FLAG is non-zero, advance teleport state +
 * spawn a whirlwind at slot 9 with LINK_Y from TeleportYs[]. Then
 * SUBMODE_VALUE = 0; MODE_TIMER++. NES CheckInitWhirlwindAndBeginUpdate.
 * drain at trap_runtime.c:69-79. */
void trap_check_init_whirlwind_and_begin_update(void);
/* Whirlwind half of CheckInitWhirlwindAndBeginUpdate; 1 = teleporting. */
unsigned char trap_init_whirlwind_at_destination(void);

/* If MODE_VALUE == 5, walk LevelMasks[] to find an unconquered
 * level mask; if found and no whirlwind/teleport active, spawn
 * whirlwind in an empty enemy slot. NES SummonWhirlwind.
 * drain at trap_runtime.c:88-110. */
void trap_summon_whirlwind(void);

/* Initialize a 4 or 6-trap cluster from TrapXs/TrapYs tables (count
 * 4 normally, 6 if MON_TYPE(slot) == TRAP_OBJ_TYPE). Each new trap
 * gets its slot derived from TRAP_BASE_SLOT. NES InitTrap_Full.
 * drain at trap_runtime.c:4-17. */
void trap_init_trap_full(unsigned int slot);

/* If Link is on a tile-aligned grid + PASSIVE_OBJ_FLAG is set, scan
 * for tile types $BC..$C3, then spawn a passive bumped object adjacent
 * to Link via empty enemy slot. NES CheckPassiveTileObjects. drain
 * at trap_runtime.c:140-188. */
void trap_check_passive_tile_objects(void);

/* Animate + draw whirlwind sprite. Calls anim_advance_and_fetch +
 * anim_set_sprite_desc_attrs (palette = FRAME_COUNTER & 3) +
 * anim_set_obj_hflip + draw_object_not_mirrored_with_frame. NES
 * DrawWhirlwind. drain at trap_runtime.c:19-24. */
void trap_draw_whirlwind(unsigned int slot);

/* Move whirlwind 2px right; teleport-mode override of LINK_X if
 * teleport active; check link collision when not in teleport
 * mode; despawn at $F0 and dispatch go-to-next-mode if teleport
 * was active. NES UpdateWhirlwind_Full. drain at
 * trap_runtime.c:26-67. */
void trap_update_whirlwind_full(unsigned int slot);

/* If Link is within 9px of slot, take 1 rupee + destroy monster +
 * clear stash flag. Else fetch sprite-descriptor pos + draw rupee
 * (item slot 22). NES UpdateRupeeStash_Full. drain at
 * trap_runtime.c:112-122. */
void trap_update_rupee_stash_full(unsigned int slot);

/* Trap state machine: if state==0, sense Link's bbox and decide
 * whether to charge along axis (per TrapAllowedDirs gate); if
 * state!=0, move object + check collisions + reverse at edge.
 * Always falls through to person_draw_and_check_collisions.
 * NES UpdateTrap_Full. drain at trap_runtime.c:190-252. */
void trap_update_trap_full(unsigned int slot);

/* Mode-B cellar entry init: save submode; init_mode_enter_room (STAGE-1
 * stub); reset bank-5 inv obj state (STAGE-1 stub); set Link to
 * cellar entry coords ($70, $DD, dir DOWN); link_end_move_and_animate
 * + run_cross_room_tasks (both STAGE-1 stubs); restore submode + 1;
 * MODE_TIMER=0; OBJ_GRID_OFFSET(0)=48; LINK_CELLAR_FLAG=1.
 * NES InitMode_B_EnterCave_Bank5. drain at trap_runtime.c:124-138.
 *
 * STAGE-1: 4 heavy NES asm chains stubbed (InitMode_EnterRoom,
 * z05_reset_inv_obj_state, Link_EndMoveAndAnimate, RunCrossRoomTasks).
 * Native cellar entry sets coords + flags only. Room reload + Link
 * animation deferred to native port of those substantial chains. */
void trap_init_mode_b_enter_cave_bank5(void);

#ifdef __cplusplus
}
#endif

#endif /* TRAP_DISPATCH_H */
