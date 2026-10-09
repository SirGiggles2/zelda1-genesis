/* link_collision_dispatch.h — native link-collision subsystem dispatch
 * (Phase 4).
 *
 * Native rewrite of select leaves from
 * src/oracle/combat/link_collision_runtime.c. Both ROMs link.
 *
 * Phase 4 first batch: link_be_harmed (the heart-damage core). Bigger
 * functions (harm_link, check_link_collision_*, check_monster_collisions,
 * begin_shove) defer until colrt_*, c_*, and z07_anim_* land native.
 */

#ifndef LINK_COLLISION_DISPATCH_H
#define LINK_COLLISION_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Apply Link damage: rings shift threshold, deduct heart slack +
 * partial, on death set MODE_VALUE=17 + clear stun. NES LinkBeHarmed.
 * drain at link_collision_runtime.c:6-46. */
void link_collision_link_be_harmed(unsigned int monster_slot);

/* Set up shove direction + advance hit-reaction depending on whether
 * weapon is a sword (slot 0 axis) or shot (per-axis dy). NES
 * BeginShove. drain at link_collision_runtime.c:130-179. */
void link_collision_begin_shove(unsigned int monster_slot);

/* Look up object damage points + dispatch begin_shove + link_be_harmed.
 * NES HarmLink. drain at link_collision_runtime.c:48-58. */
void link_collision_harm_link(unsigned int monster_slot);

/* Inner half of CheckLinkCollision: hitbox + threshold setup, dispatch
 * collision check, gate harm by Link's parry/shield posture per
 * monster type. NES CheckLinkCollision_PreInit. drain at
 * link_collision_runtime.c:60-87. */
void link_collision_check_link_collision_preinit(unsigned int monster_slot);

/* Outer wrapper: get object middle, clear collision scratch + flags,
 * gate by stun/halt timers, then preinit. NES CheckLinkCollision.
 * drain at link_collision_runtime.c:89-98. */
void link_collision_check_link_collision(unsigned int monster_slot);

/* Top-level monster-vs-Link/weapon dispatch. Runs the colrt_* battery
 * (boomerang, sword shot, bomb x2, sword, arrow) gated by
 * MON_STATUS_FLAGS bit $20 (some monsters skip weapon collisions).
 * Then runs check_link_collision. Then bookkeeping for bounce-turn
 * monsters ($27/$17) and dying-monster cleanup ($05/$06). NES
 * CheckMonsterCollisions. drain at link_collision_runtime.c:100-128. */
void link_collision_check_monster_collisions(unsigned int monster_slot);

#ifdef __cplusplus
}
#endif

#endif /* LINK_COLLISION_DISPATCH_H */
