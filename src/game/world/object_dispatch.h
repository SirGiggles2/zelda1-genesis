/* object_dispatch.h — native object subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/world/object_runtime.c. Both ROMs link.
 * Pure C, no shims (drain references RAM/OBJ macros only).
 *
 * Phase 4 first object port: bound_direction_* family + bound_by_room
 * variants — used by Phase 4 collision detection + enemy AI.
 */

#ifndef OBJECT_DISPATCH_H
#define OBJECT_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Test the object's X coord against RoomBoundLeft / RoomBoundRight.
 * For non-Link objects in slots >= $0D or with ObjType == $5C
 * (boomerang), the test point is shifted by +$0B (left bound) or
 * -$17 (right bound) to account for the sprite's collision body.
 *
 * If the X coord crosses a bound AND the corresponding direction bit
 * (left=$02, right=$01) is set in NES_OBJ_DIR ($000F = direction
 * scratch param, NOT per-slot ObjDir), clear NES_OBJ_DIR. The
 * direction byte is left untouched if no bound was crossed.
 *
 * Mirrors NES BoundDirectionHorizontally (Z_01.asm:3312). */
void object_bound_direction_horizontally(unsigned int slot);

/* Mirror of `object_bound_direction_horizontally` for the Y axis.
 * Slot/type shifts are +$0F (top) and -$21 (bottom). Mirrors NES
 * BoundDirectionVertically (Z_01.asm:3382-onward). */
void object_bound_direction_vertically(unsigned int slot);

/* Run both bound checks then return the resulting NES_OBJ_DIR (0 if
 * any bound cleared it, else the original direction). Mirrors NES
 * BoundByRoom (Z_01.asm:3457). */
unsigned char object_bound_by_room(unsigned int slot);

/* As `object_bound_by_room` but accepts the direction param directly
 * (callsite-friendly wrapper that writes NES_OBJ_DIR before testing). */
unsigned char object_bound_by_room_with_dir(unsigned char direction,
                                            unsigned int slot);

/* Move an object slot one frame's worth, decoded across the 4 dir
 * bits (right $01, left $02, down $04, up $08). Each axis is a
 * fractional position accumulator (NES_OBJ_POS_FRAC) advanced by the
 * speed (NES_OBJ_QSPD_FRAC); on overflow/underflow the object steps
 * one pixel in the direction. Grid offset (NES_OBJ_GRID_OFFSET)
 * clamps stepping at room cell boundaries. Mirrors NES Z_01.asm
 * MoveObject. Drain at object_runtime.c:8-72. */
void object_move_object(unsigned short slot);

/* Returns CARRY_SET if the fractional add carried into the integer
 * step (i.e. one pixel of movement). Mirrors NES
 * AddQSpeedToPositionFraction (Z_01.asm:3470). */
unsigned int object_add_q_speed_to_position_fraction(unsigned int slot);

/* Mirror of add for negative direction; returns CARRY_SET when no
 * borrow occurred AND grid offset wasn't at limit. */
unsigned int object_sub_q_speed_from_position_fraction(unsigned int slot);

/* Move a "shot" (arrow/boomerang/etc.) one frame, with collision-
 * sensitive grid-offset accounting. If bound_by_room kills the
 * direction, sets NES_SHOT_COLLISION_FLAG = $80 and returns. Else
 * runs move_object with grid offset zeroed, then restores or merges
 * grid offset based on shot collision flag. Mirrors NES MoveShot
 * (Z_01.asm). */
void object_move_shot(unsigned char direction, unsigned int slot);

#ifdef __cplusplus
}
#endif

#endif /* OBJECT_DISPATCH_H */
