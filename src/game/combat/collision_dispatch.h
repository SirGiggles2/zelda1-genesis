/* collision_dispatch.h — native collision subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/combat/collision_runtime.c collidable-tile
 * cluster + thresholds. Both ROMs link.
 */

#ifndef COLLISION_DISPATCH_H
#define COLLISION_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Compare COMBAT_HITBOX vs ENEMY_GLEEOK_NECK_Y_PTR (used as second
 * point) within COMBAT_THRESHOLD_X/Y. Sets COMBAT_COLLIDED 0/1.
 * NES DoObjectsCollideWithThresholds. */
unsigned char collision_do_objects_collide_with_thresholds(void);

/* Set both thresholds to threshold + run with-thresholds.
 * NES DoObjectsCollide. */
unsigned char collision_do_objects_collide(unsigned int threshold);

/* Check the room-tile under a slot's hitbox + offset. Reads
 * PlayAreaColumnAddrs / WalkableTiles tables. NES GetCollidableTile. */
unsigned char collision_get_collidable_tile(unsigned int hotspot_offset,
                                            unsigned int slot);

/* COMBAT_PART_INDEX = 0; collision_get_collidable_tile(0, slot).
 * NES GetCollidableTileStill. */
unsigned char collision_get_collidable_tile_still(unsigned int slot);

/* Compute hotspot offset from slot+dir, then collision_get_collidable_tile.
 * NES GetCollidingTileMoving; leaves the column address in [00:01]. */
unsigned char collision_get_colliding_tile_moving(unsigned int slot);

/* Dispatch monster-vs-weapon collision response: gohma special path,
 * direction-based parry for $0B/$0C, dirward-flip for $12/$13, deal
 * damage. NES HandleMonsterWeaponCollision (collision_runtime.c:36-63). */
void collision_handle_monster_weapon_collision(unsigned int monster_slot,
                                               unsigned int weapon_slot);

/* Bbox vs weapon collision check + dispatch handle. NES
 * CheckMonsterWeaponCollision (collision_runtime.c:65-89). */
void collision_check_monster_weapon_collision(unsigned int monster_slot,
                                              unsigned int weapon_y_mid);

/* Slender-weapon hitbox setup + check. NES
 * CheckMonsterSlenderWeaponCollision2 (collision_runtime.c:91-105). */
void collision_check_monster_slender_weapon_collision2(unsigned int monster_slot);

/* Set damage points + threshold-Y, dispatch slender2.
 * NES CheckMonsterSlenderWeaponCollision (collision_runtime.c:107-111). */
void collision_check_monster_slender_weapon_collision(
    unsigned int monster_slot, unsigned int damage_points);

/* Direction-based parry vs shove dispatch. NES
 * ParryOrShove (collision_runtime.c:113-123). */
void collision_parry_or_shove(unsigned int monster_slot,
                              unsigned int weapon_slot);

/* Sword stab axis-dependent threshold + slender check + parry/shove.
 * NES CheckMonsterStabbingCollision (collision_runtime.c:125-142). */
void collision_check_monster_stabbing_collision(unsigned int monster_slot,
                                                unsigned int damage_points);

/* Sword-hit dispatch with sword-level damage table.
 * NES CheckMonsterSwordCollision (collision_runtime.c:144-149). */
void collision_check_monster_sword_collision(unsigned int monster_slot,
                                             unsigned int weapon_slot);

/* Generic shot collision: slender check + parry/shove (or special
 * boomerang at slot $12 against vire $16 type).
 * NES CheckMonsterShotCollision (collision_runtime.c:151-166). */
void collision_check_monster_shot_collision(unsigned int monster_slot,
                                            unsigned int weapon_slot,
                                            unsigned int damage_points);

/* Arrow + rod state machine: sparked => stab; flying => shot.
 * NES CheckMonsterArrowOrRodCollision (collision_runtime.c:168-181). */
void collision_check_monster_arrow_or_rod_collision(unsigned int monster_slot,
                                                    unsigned int weapon_slot);

/* Boomerang/food shot collision (axis hitbox setup).
 * NES CheckMonsterBoomerangOrFoodCollision (collision_runtime.c:183-191). */
void collision_check_monster_boomerang_or_food_collision(
    unsigned int monster_slot, unsigned int weapon_slot);

/* Sword-shot or magic-shot collision (sword level damage).
 * NES CheckMonsterSwordShotOrMagicShotCollision
 * (collision_runtime.c:193-211). */
void collision_check_monster_sword_shot_or_magic_shot_collision(
    unsigned int monster_slot, unsigned int weapon_slot);

/* Bomb/fire collision with bomb fuse damage scaling.
 * NES CheckMonsterBombOrFireCollision (collision_runtime.c:213-235). */
void collision_check_monster_bomb_or_fire_collision(
    unsigned int monster_slot, unsigned int weapon_slot);

#ifdef __cplusplus
}
#endif

#endif /* COLLISION_DISPATCH_H */
