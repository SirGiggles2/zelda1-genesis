/* enemy_projectile_bridge.c -- Phase 7 Task 7.2 step 12 shot UPDATE
 * primitives bridge. Forwarders for c_/z01_/z07_ symbols pulled in by
 * src/oracle/enemies/enemy_projectile_runtime.c. Same model as
 * src/game/enemies/enemy_walker_bridge.c. Drain Rule D1 stance: EXTEND.
 * WT-5: lives at src/game/enemies/, not engine/.
 */

#include "core/core_dispatch.h"             /* core_get_opposite_dir, core_destroy_monster */
#include "combat/collision_dispatch.h"      /* collision_get_colliding_tile_moving */
#include "combat/link_collision_dispatch.h" /* link_collision_check_link_collision */
#include "combat/targeting_dispatch.h"      /* targeting_* */
#include "world/draw_dispatch.h"            /* draw_object_not_mirrored, draw_arrow, draw_boomerang, draw_sword_shot_or_magic_shot */
#include "world/object_dispatch.h"          /* object_bound_by_room, object_move_object, object_move_shot */
#include "world/sprite_dispatch.h"          /* sprite_anim_fetch_obj_pos */

void c_move_object(unsigned short slot)
{
    object_move_object(slot);
}

void c_move_shot(unsigned char direction, unsigned int slot)
{
    object_move_shot(direction, slot);
}

void c_draw_object_not_mirrored(unsigned int slot)
{
    /* Deferred: NES would pull A from RAM[$0D] (the cached frame byte
     * enrt_draw_shot just stored). For step 12 we pass frame=0 — same
     * stance as moblin/goriya UPDATE (ticks but does not draw). */
    draw_object_not_mirrored(0u, slot);
}

void c_draw_arrow(unsigned int slot)
{
    /* Step 13: native drain in src/game/world/draw_dispatch.c
     * (NES Z_07.asm:3908). */
    draw_arrow(slot);
}

void c_draw_boomerang(unsigned int slot)
{
    draw_boomerang(slot);
}

void c_draw_sword_shot_or_magic_shot(unsigned int slot)
{
    /* Step 13: native drain in src/game/world/draw_dispatch.c
     * (NES Z_07.asm:3437). */
    draw_sword_shot_or_magic_shot(slot);
}

unsigned char z01_bound_by_room(unsigned int slot)
{
    return object_bound_by_room(slot);
}

unsigned char z01_bound_by_room_with_a(unsigned char direction, unsigned int slot)
{
    return object_bound_by_room_with_dir(direction, slot);
}

void z01_check_link_collision(unsigned int slot)
{
    link_collision_check_link_collision(slot);
}

unsigned int z01_get_opposite_dir(unsigned int dir)
{
    return core_get_opposite_dir(dir);
}

void z01_get_directions_and_distances_to_target(unsigned char target_slot, unsigned int origin_slot)
{
    targeting_get_directions_and_distances_to_target(target_slot, origin_slot);
}

unsigned int z01_calc_diagonal_speed_index(unsigned int mid_speed_idx)
{
    return targeting_calc_diagonal_speed_index(mid_speed_idx);
}

void z07_destroy_monster(unsigned int slot)
{
    core_destroy_monster(slot);
}

unsigned char z07_get_colliding_tile_moving(unsigned int slot)
{
    return collision_get_colliding_tile_moving(slot);
}

/* Phase 7 Task 7.5 step 3 — z07_get_collidable_tile + _still forwarders.
 * Pulled in by enrt_pols_voice_get_colliding_tile / _is_square_walkable
 * (enemy_boss_runtime.c:494/514) once $16 PolsVoice UPDATE is wired.
 * Native bodies drained at collision_get_collidable_tile{,_still} in
 * src/game/combat/collision_dispatch.c. */
unsigned char z07_get_collidable_tile(unsigned int hotspot_offset,
                                      unsigned int slot)
{
    return collision_get_collidable_tile(hotspot_offset, slot);
}

unsigned char z07_get_collidable_tile_still(unsigned int slot)
{
    return collision_get_collidable_tile_still(slot);
}

unsigned char z07_anim_fetch_obj_pos(unsigned int slot)
{
    return sprite_anim_fetch_obj_pos(slot);
}

/* Phase 7 Task 7.4 step 8 — z07_animate_object_walking forwarder.
 * NES Z_07.asm:6657 c_animate_object_walking (legacy bank shim) just
 * called z07_animate_object_walking. The native body lives at
 * src/game/world/sprite_dispatch.c:115 (sprite_animate_object_walking).
 * Used by enrt_update_standing_fire ($40 GuardFire / $41 StandingFire). */
void z07_animate_object_walking(unsigned int slot)
{
    sprite_animate_object_walking(slot);
}
