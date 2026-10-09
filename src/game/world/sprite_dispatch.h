/* sprite_dispatch.h — native sprite subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/world/sprite_runtime.c. Both ROMs link.
 * Pure C, no transpile shims (cross-subsystem core helper
 * z01_reset_cur_sprite_index inlined per NES semantics).
 *
 * Phase 4 first sprite batch: rolling sprite index helpers + OAM hide
 * + Link priority-drop for horizontal door transitions.
 */

#ifndef SPRITE_DISPATCH_H
#define SPRITE_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Bump the rolling sprite-write cursor (RollingSpriteIndex at $0341).
 * Wraps from $27 -> $00 (40 sprite slots used per frame). NES
 * CycleCurSpriteIndex (Z_01.asm). */
void sprite_cycle_cur_sprite_index(void);
/* Two cycles when no producer consumes the intermediate cursor. */
void sprite_cycle_cur_sprite_index_twice(void);

/* As above but takes the previous index as a parameter (= "in A"
 * NES register-passing convention) and returns the new value. */
unsigned char sprite_cycle_sprite_index_in_a(unsigned char idx);

/* Hide all 24 dynamic-object OAM slots by writing $F8 to their Y byte
 * (off-screen). Then bump the high-priority OAM cursor at $0342.
 * Mirrors NES HideObjectSprites. */
void sprite_hide_object_sprites(void);

/* During a side-doorway scroll, force Link's top-half OAM sprites
 * (slot 18 left half + 19 right half) into "behind background"
 * priority (OAM attr bit $20) so the door arch tiles render over
 * his head/torso. Mirrors NES ShowLinkSpritesBehindHorizontalDoors
 * (Z_01.asm:1594-ish). */
void sprite_show_link_sprites_behind_horizontal_doors(void);

/* --- Animation cluster (NES Z_07.asm Plan-C drained subset) ----------- */

/* Reset OBJ_ANIM_CNTR for a slot to COMBAT_WEAPON_SLOT (current sprite
 * descriptor count) and toggle OBJ_HFLIP. Mirrors NES
 * RollOverAnimCounter. */
void sprite_roll_over_anim_counter(unsigned int slot);

/* Capture the slot's tile-grid (X, Y) into the animation working
 * registers (COMBAT_WEAPON_SLOT, ENEMY_SCRATCH_Y) and clear
 * ENEMY_FRAME_FLAGS. Returns 0 (NES preserves A=0 for chain). */
unsigned char sprite_anim_fetch_obj_pos(unsigned int slot);

/* Set ENEMY_FRAME_FLAGS = OBJ_HFLIP[slot] (mirror flip flag for
 * horizontal-walk frames). Mirrors NES AnimSetObjHFlip. */
void sprite_anim_set_obj_hflip(unsigned int slot);

/* Combo: stash count, decrement OBJ_ANIM_CNTR, roll over on zero,
 * then fetch the slot's anim position. Mirrors NES
 * AnimAdvanceAndFetch. */
void sprite_anim_advance_and_fetch(unsigned int val, unsigned int slot);

/* Per-frame walk-cycle update for a moving object slot: advance anim
 * counter, on zero roll over (and animate Link state if slot 0),
 * fetch pos, set hflip flag based on direction. Mirrors NES
 * AnimateObjectWalking. */
void sprite_animate_object_walking(unsigned int slot);

#ifdef __cplusplus
}
#endif

#endif /* SPRITE_DISPATCH_H */
