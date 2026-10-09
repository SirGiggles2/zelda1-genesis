/* enemy_lamnola_bridge.c — Phase 8 Task 8.9 Lamnola+Moldorm shim bridge.
 *
 * Drain Rule D1 stance: EXTEND. All forwarders to dispatcher entry points
 * + drained boss-runtime entries already linked into Debug.md. No NES asm
 * linkage. No vasm/gas dialect bridge.
 *
 * Hard rule WT-5: lives at src/game/enemies/, not RoomRom/.
 *
 * Symbol map (resolves Lamnola+Moldorm runtime undefined refs):
 *   c_anim_write_sprite           -> stub (Z_01.asm:5365 single-OAM
 *                                   writer; OAM router for boss family
 *                                   not wired this phase — call sites
 *                                   in lamnola/moldorm draw a per-tick
 *                                   single sprite that won't be visible
 *                                   until OAM router lands. Boot-smoke
 *                                   ok: stub keeps slot ticking).
 *   c_get_opposite_dir            -> core_get_opposite_dir (low byte)
 *   c_bound_by_room               -> object_bound_by_room
 *   c_get_colliding_tile_moving   -> collision_get_colliding_tile_moving
 *   z04_play_boss_death_cry_if_needed
 *                                 -> enrt_play_boss_death_cry_if_needed
 *                                    (enemy_boss_runtime.c:699)
 *   z07_set_shove_info_with0      -> core_set_shove_info_with0
 *                                    (core_dispatch.c:429)
 */

#include "core/core_dispatch.h"        /* core_get_opposite_dir,
                                          core_set_shove_info_with0 */
#include "world/object_dispatch.h"     /* object_bound_by_room */
#include "combat/collision_dispatch.h" /* collision_get_colliding_tile_moving */
#include "../../oracle/enemies/enemy_runtime.h" /* enrt_play_boss_death_cry_if_needed */

/* c_anim_write_sprite moved to src/game/enemies/enemy_render.c 2026-05-15.
 * Real implementation: drained Anim_WriteSprite (Z_01.asm:5365) writes
 * NES OAM mirror $0200..$02FF. enemy_render_sweep_oam_to_sat() pushes
 * mirror to Genesis SAT slots 32-71 once per tick. */

unsigned char c_get_opposite_dir(unsigned char dir)
{
    return (unsigned char)(core_get_opposite_dir((unsigned int)dir) & 0xFFu);
}

unsigned char c_bound_by_room(unsigned int slot)
{
    return object_bound_by_room(slot);
}

unsigned char c_get_colliding_tile_moving(unsigned int slot)
{
    return collision_get_colliding_tile_moving(slot);
}

void z04_play_boss_death_cry_if_needed(unsigned int slot)
{
    enrt_play_boss_death_cry_if_needed(slot);
}

void z07_set_shove_info_with0(unsigned int val, unsigned int slot)
{
    core_set_shove_info_with0(val, slot);
}
