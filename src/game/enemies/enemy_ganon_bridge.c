/* enemy_ganon_bridge.c — Phase 8 Task 8.10 Ganon ($3E) shim bridge.
 *
 * NES source: Z_04.asm:Ganon_CheckCollisions, Ganon_DrawBurst.
 * Drained C: src/oracle/enemies/enemy_ganon_runtime.c and
 *            src/oracle/combat/{collision,link_collision}_runtime.c.
 * Coverage: PARTIAL (Ganon-specific attack/death integration pending).
 * Stance: EXTEND the already-linked native collision/sprite dispatchers.
 *
 * Hard rule WT-5: lives at src/game/enemies/, not engine/.
 *
 * Symbol map (resolves Ganon runtime undefined refs):
 *   GanonStartXs                 -> NES Z_04.asm line 10488. $30,$B0.
 *                                   k_ganon_start_xs is static in
 *                                   enemy_dispatch.c; expose under
 *                                   the NES name expected by the
 *                                   private header.
 *   z01_animate_world_fading     -> world_animate_world_fading
 *                                   (world_dispatch.c:46).
 *   z01_get_room_flag_uw_item_state
 *                                -> progress_get_room_flag_uw_item_state
 *                                   (progress_dispatch.c).
 *   sprrt_anim_fetch_obj_pos     -> sprite_anim_fetch_obj_pos.
 *   lcrt_check_link_collision_preinit -> link_collision_check_link_collision_preinit.
 *   colrt_check_monster_sword_collision -> collision_check_monster_sword_collision.
 *   colrt_check_monster_arrow_or_rod_collision -> collision_check_monster_arrow_or_rod_collision.
 */

#include "world/world_dispatch.h"     /* world_animate_world_fading */
#include "world/progress_dispatch.h"  /* progress_get_room_flag_uw_item_state */
#include "world/sprite_dispatch.h"
#include "combat/collision_dispatch.h"
#include "combat/link_collision_dispatch.h"

/* GanonStartXs — NES Z_04.asm:10488 (table consumed by InitGanon
 * randomize_location at offset $30 / $B0 by sprite-attr-row & 1). */
const unsigned char GanonStartXs[2] = { 0x30u, 0xB0u };

unsigned int z01_animate_world_fading(void)
{
    return world_animate_world_fading();
}

unsigned char z01_get_room_flag_uw_item_state(void)
{
    return progress_get_room_flag_uw_item_state();
}

unsigned char sprrt_anim_fetch_obj_pos(unsigned int slot)
{
    return sprite_anim_fetch_obj_pos(slot);
}

void lcrt_check_link_collision_preinit(unsigned int monster_slot)
{
    link_collision_check_link_collision_preinit(monster_slot);
}

void colrt_check_monster_sword_collision(unsigned int monster_slot,
                                         unsigned int weapon_slot)
{
    collision_check_monster_sword_collision(monster_slot, weapon_slot);
}

void colrt_check_monster_arrow_or_rod_collision(unsigned int monster_slot,
                                                unsigned int weapon_slot)
{
    collision_check_monster_arrow_or_rod_collision(monster_slot, weapon_slot);
}
