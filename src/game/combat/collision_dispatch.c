/* collision_dispatch.c — native collision subsystem dispatch (Phase 4).
 *
 * Drain MATCH per drain in src/oracle/combat/collision_runtime.c
 * (verified-by-use; in production via Zelda.md gameplay).
 */

#include "collision_dispatch.h"
#include "terrain_query.h"
#include "../../state/game_profile.h"
#include "../../state/weapon_state.h"
#include <stdint.h>
#include "platform_abi.h"
#include "combat_state.h"      /* COMBAT_*, COMBAT_HITBOX_X/Y, COMBAT_THRESHOLD_X/Y, COMBAT_COLLIDED, COMBAT_ABS_DX/DY, COMBAT_PART_INDEX */
#include "enemy_state.h"       /* ENEMY_GLEEOK_NECK_Y_PTR_LO/HI, ENEMY_COLLIDED_TILE, ENEMY_DARK_ROOM_FLAG, ENEMY_CANDLE_ROOM_ID, ENEMY_PLAYER_OBJ_X/Y */
#include "object_state.h"      /* OBJ_TILE_X/_Y, OBJ_X, OBJ_Y, OBJ_DIR, OBJ_STATE */
#include "world_state.h"       /* LINK_DIR */
#include "core/core_dispatch.h"        /* core_play_parry_tune, core_handle_shot_blocked */
#include "combat/combat_dispatch.h"    /* combat_play_parry_sound_for_damage_type, combat_deal_damage */
#include "combat/combat_runtime.h"     /* roomrom_combat_is_redux */
#include "combat/link_collision_dispatch.h" /* link_collision_begin_shove */
#include "enemies/enemy_dispatch.h"    /* enemy_gohma_handle_weapon_collision */

/* SwordDamagePoints[3] — sword level 1/2/3 damage. NES drain
 * at collision_runtime.c:6. */
static const unsigned char k_sword_damage_points[3] = {
    0x10u, 0x20u, 0x40u
};

/* T-358: 96-byte leaf hitbox compare with 228 static call sites, reached
 * multiple times per monster per frame. Inline it so each check does not pay
 * JSR/RTS + argument setup. The external definition stays for non-LTO users. */
__attribute__((always_inline))
unsigned char collision_do_objects_collide_with_thresholds(void)
{
    /* drain at collision_runtime.c:8-28. */
    COMBAT_COLLIDED = 0u;
    {
        const unsigned char dx =
            (unsigned char)(ENEMY_GLEEOK_NECK_Y_PTR_LO - COMBAT_HITBOX_X);
        const unsigned char abs_dx =
            (dx & 0x80u) ? (unsigned char)((~dx + 1u) & 0xFFu) : dx;
        COMBAT_ABS_DX = abs_dx;
        if (abs_dx >= COMBAT_THRESHOLD_X) {
            return COMBAT_COLLIDED;
        }
    }
    {
        const unsigned char dy =
            (unsigned char)(ENEMY_GLEEOK_NECK_Y_PTR_HI - COMBAT_HITBOX_Y);
        const unsigned char abs_dy =
            (dy & 0x80u) ? (unsigned char)((~dy + 1u) & 0xFFu) : dy;
        COMBAT_ABS_DY = abs_dy;
        if (abs_dy >= COMBAT_THRESHOLD_Y) {
            return COMBAT_COLLIDED;
        }
    }
    COMBAT_COLLIDED = (uint8_t)(COMBAT_COLLIDED + 1u);
    return COMBAT_COLLIDED;
}

unsigned char collision_do_objects_collide(unsigned int threshold)
{
    /* drain at collision_runtime.c:30-34. */
    COMBAT_THRESHOLD_X = (uint8_t)threshold;
    COMBAT_THRESHOLD_Y = (uint8_t)threshold;
    return collision_do_objects_collide_with_thresholds();
}

/* Query uses explicit coordinates. Only this remaining ABI bridge publishes
 * scratch and collided-tile cells for unmigrated enemy/ladder/speed consumers. */
static inline __attribute__((always_inline)) TerrainTileResult query_tile(
    unsigned char x, unsigned char y, unsigned char dir,
    unsigned char hotspot, unsigned char is_player)
{
    TerrainQueryView world = { &nes_ram[0x6530u], ENEMY_DARK_ROOM_FLAG, /* native_ram bulk:src/game/combat/collision_dispatch.c:75 */
                               ENEMY_CANDLE_ROOM_ID };
    return terrain_query_tile(&world, x, y, dir, hotspot, is_player);
}
static inline __attribute__((always_inline)) void publish_tile(
    TerrainTileResult result, unsigned int slot)
{
    unsigned short legacy_column = (unsigned short)(0x6530u + result.column * 22u);
    F_ram_0000_N2 = (unsigned char)legacy_column;
    F_ram_0001_N2 = (unsigned char)(legacy_column >> 8);
    ENEMY_COLLIDED_TILE(slot) = result.tile;
}
static inline __attribute__((always_inline)) unsigned char
collidable_tile(unsigned int hotspot_offset, unsigned int slot)
{
    TerrainTileResult result;
    COMBAT_HITBOX_X = (unsigned char)hotspot_offset;
    result = query_tile(OBJ_TILE_X(slot), OBJ_TILE_Y(slot), COMBAT_PART_INDEX,
                        (unsigned char)hotspot_offset, slot == 0u);
    publish_tile(result, slot);
    return result.tile;
}
unsigned char collision_get_player_moving_tile(unsigned char x, unsigned char y,
    unsigned char dir, unsigned char publish_legacy)
{
    unsigned char hotspot = 0xF8u;
    TerrainTileResult result;
    if (dir & 5u) hotspot = (dir & 4u) ? 8u : 16u;
    /* Retain the old live scratch output even for nonpublishing probes.
     * Player position/direction cells are never borrowed or overwritten. */
    COMBAT_HITBOX_X = hotspot;
    result = query_tile(x, y, dir, hotspot, 1u);
    if (publish_legacy) publish_tile(result, 0u);
    return result.tile;
}

unsigned char collision_get_collidable_tile(unsigned int hotspot_offset,
                                            unsigned int slot)
{
    return collidable_tile(hotspot_offset, slot);
}

unsigned char collision_get_collidable_tile_still(unsigned int slot)
{
    /* drain at collision_runtime.c:310-313. */
    COMBAT_PART_INDEX = 0u;
    return collision_get_collidable_tile(0u, slot);
}

/* NES GetCollidingTileMoving: hotspot -8 (Link) / -$10 for up, left or no
 * direction in [0F], 8 for down, $10 for right. */
/* T-172 option A: inlined into its callers (call overhead). */
__attribute__((always_inline)) unsigned char collision_get_colliding_tile_moving(unsigned int slot)
{
    /* drain at collision_runtime.c:315-330. */
    unsigned char hotspot = (slot == 0u) ? 0xF8u : 0xF0u;
    const unsigned char dir = COMBAT_PART_INDEX;
    if (dir & 0x05u) hotspot = (dir & 0x04u) ? 8u : 16u;
    return collidable_tile(hotspot, slot);
}

/* NES source: Redux gameplay/visible_secrets.asm:collision_tiles_sub.
 * Drained C: collision_dispatch.c collidable_tile; enemy_walker_bridge.c
 *            c_obj_shove / walker_check_tile_collision; main Link mover.
 * Coverage: PARTIAL (Original threshold; core secret obstacle range added).
 * Stance: EXTEND the comparison used by CheckTiles and CheckHitTiles. */
unsigned char collision_moving_tile_is_obstacle(unsigned char tile)
{
    if (tile >= 0x54u && tile < 0x60u && game_profile_is_remix())
        return 1u;
    return (unsigned char)(tile >= F_ram_034A_R2);
}

/* --------------------------------------------------------------- */
/* Monster-vs-weapon battery — drain at collision_runtime.c:36-235.*/
/* --------------------------------------------------------------- */

void collision_handle_monster_weapon_collision(unsigned int monster_slot,
                                               unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:36-63. */
    if ((unsigned char)MON_INVINCIBILITY(monster_slot) &
        (unsigned char)COMBAT_DAMAGE_TYPE) {
        combat_play_parry_sound_for_damage_type();
        return;
    }
    const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
    if (mtype == 0x33u || mtype == 0x34u) {
        enemy_gohma_handle_weapon_collision(monster_slot, weapon_slot);
        return;
    }
    if (mtype == 0x13u || mtype == 0x12u) {
        if (weapon_slot != 0x0Fu) {
            OBJ_DIR(monster_slot) = (uint8_t)OBJ_DIR(weapon_slot);
        }
        combat_deal_damage(monster_slot);
        return;
    }
    if (mtype == 0x0Bu || mtype == 0x0Cu) {
        const unsigned char combined =
            (unsigned char)((unsigned char)OBJ_DIR(weapon_slot) |
                            (unsigned char)OBJ_DIR(monster_slot));
        if (combined == 0x0Cu || combined == 0x03u) {
            combat_play_parry_sound_for_damage_type();
            return;
        }
    }
    combat_deal_damage(monster_slot);
}

/* T-172 option A: inlined into its callers (call overhead). */
__attribute__((always_inline)) void collision_check_monster_weapon_collision(unsigned int monster_slot,
                                              unsigned int weapon_y_mid)
{
    /* drain at collision_runtime.c:65-89. */
    COMBAT_HITBOX_Y = (uint8_t)weapon_y_mid;
    COMBAT_COLLIDED = 0u;
    const unsigned int weapon_slot = (unsigned int)COMBAT_WEAPON_SLOT;
    if ((unsigned char)OBJ_STATE(weapon_slot) == 0u) {
        return;
    }
    if (!collision_do_objects_collide_with_thresholds()) {
        return;
    }
    if (weapon_slot == 0x0Fu) {
        const unsigned char inv =
            (unsigned char)((unsigned char)MON_INVINCIBILITY(monster_slot) &
                            (unsigned char)COMBAT_DAMAGE_TYPE);
        if (inv) {
            core_play_parry_tune();
        }
        /* The player boomerang must retain the collision-selected return
         * state; its next native update must not republish outbound state. */
        g_player_boomerang.state = 80u;
        OBJ_STATE(weapon_slot) = 80u;
        if (inv) {
            return;
        }
        COMBAT_DAMAGE_AMOUNT = 0u;
        MON_STUN_TIMER(monster_slot) = 16u;
    }
    collision_handle_monster_weapon_collision(monster_slot, weapon_slot);
}

/* NES source: Redux sword_swing.asm:weapon_position/weapon_collide_check.
 * Drained C: collision_runtime.c:colrt_check_monster_slender_weapon_collision2
 * and colrt_check_monster_stabbing_collision.
 * Coverage: PARTIAL(live contact boundaries; broader weapon lifecycle pending).
 * Stance: EXTEND native collision owner; Original retains drained rules. */
/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_slender_weapon_collision2(unsigned int monster_slot)
{
    /* drain at collision_runtime.c:91-105. */
    const unsigned int weapon_slot = (unsigned int)COMBAT_WEAPON_SLOT;
    const unsigned char remix = game_profile_is_remix();
    const unsigned char dir = (unsigned char)((remix ?
        (unsigned char)OBJ_DIR(weapon_slot) : (unsigned char)LINK_DIR) & 0x0Cu);
    unsigned char wx;
    unsigned char wy;
    if (dir != 0u) {
        wx = (unsigned char)((unsigned char)OBJ_X(weapon_slot) + 6u);
        wy = (unsigned char)((unsigned char)OBJ_Y(weapon_slot) + 8u);
    } else {
        wx = (unsigned char)((unsigned char)OBJ_X(weapon_slot) + 8u);
        wy = (unsigned char)((unsigned char)OBJ_Y(weapon_slot) + (remix ? 9u : 6u));
    }
    COMBAT_HITBOX_X = wx;
    collision_check_monster_weapon_collision(monster_slot, (unsigned int)wy);
}

/* T-172 option A: inlined into its per-enemy callers (call overhead). */
__attribute__((always_inline)) void collision_check_monster_slender_weapon_collision(unsigned int monster_slot,
                                                      unsigned int damage_points)
{
    /* drain at collision_runtime.c:107-111. */
    COMBAT_DAMAGE_AMOUNT = (uint8_t)damage_points;
    COMBAT_THRESHOLD_Y = (uint8_t)COMBAT_THRESHOLD_X;
    collision_check_monster_slender_weapon_collision2(monster_slot);
}

void collision_parry_or_shove(unsigned int monster_slot,
                              unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:113-123. */
    const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
    if (mtype == 0x0Bu || mtype == 0x0Cu) {
        const unsigned char combined =
            (unsigned char)((unsigned char)OBJ_DIR(weapon_slot) |
                            (unsigned char)OBJ_DIR(monster_slot));
        if (combined == 0x0Cu || combined == 0x03u) {
            core_play_parry_tune();
            return;
        }
    }
    link_collision_begin_shove(monster_slot);
}

void collision_check_monster_stabbing_collision(unsigned int monster_slot,
                                                unsigned int damage_points)
{
    /* drain at collision_runtime.c:125-142. */
    COMBAT_DAMAGE_AMOUNT = (uint8_t)damage_points;
    {
        const unsigned char dir =
            (unsigned char)((game_profile_is_remix() ?
                (unsigned char)OBJ_DIR((unsigned int)COMBAT_WEAPON_SLOT) :
                (unsigned char)LINK_DIR) & 0x0Cu);
        if (dir != 0u) {
            COMBAT_THRESHOLD_X = 12u;
            COMBAT_THRESHOLD_Y = 16u;
        } else {
            COMBAT_THRESHOLD_X = 16u;
            COMBAT_THRESHOLD_Y = 12u;
        }
    }
    collision_check_monster_slender_weapon_collision2(monster_slot);
    if (!(unsigned char)COMBAT_COLLIDED) {
        return;
    }
    collision_parry_or_shove(monster_slot,
                             (unsigned int)(unsigned char)COMBAT_WEAPON_SLOT);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_sword_collision(unsigned int monster_slot,
                                             unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:144-149. */
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    COMBAT_DAMAGE_TYPE = 1u;
    if ((unsigned char)OBJ_STATE(weapon_slot) != 2u) {
        return;
    }
    const unsigned char level = (unsigned char)ITEM_SWORD_LEVEL;
    const unsigned char idx = (level >= 1u && level <= 3u) ? (level - 1u) : 0u;
    collision_check_monster_stabbing_collision(
        monster_slot, (unsigned int)k_sword_damage_points[idx]);
}

/* T-172 option A: inlined into its callers (call overhead). */
__attribute__((always_inline)) void collision_check_monster_shot_collision(unsigned int monster_slot,
                                            unsigned int weapon_slot,
                                            unsigned int damage_points)
{
    /* drain at collision_runtime.c:151-166. */
    collision_check_monster_slender_weapon_collision(monster_slot, damage_points);
    if (!(unsigned char)COMBAT_COLLIDED) {
        return;
    }
    if (weapon_slot != 0x12u) {
        collision_parry_or_shove(monster_slot, weapon_slot);
        return;
    }
    if ((unsigned char)MON_TYPE(monster_slot) == 0x16u) {
        MON_HP(monster_slot) = 0u;
        combat_deal_damage(monster_slot);
        return;
    }
    OBJ_STATE(weapon_slot) = 32u;
    OBJ_ANIM_TIMER(weapon_slot) = 3u;
    collision_parry_or_shove(monster_slot, weapon_slot);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_arrow_or_rod_collision(unsigned int monster_slot,
                                                    unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:168-181. */
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    const unsigned char state = (unsigned char)OBJ_STATE(weapon_slot);
    /* Redux misc.asm (bank 1 $B5D4, CMP #$32): the wand's windup states $30/$31 no
     * longer hit behind Link; they fall through to the arrow branch, which ignores
     * states >= $20. Original keeps CMP #$30. */
    if (state >= (roomrom_combat_is_redux() ? 0x32u : 0x30u)) {
        COMBAT_DAMAGE_TYPE = 1u;
        collision_check_monster_stabbing_collision(monster_slot, 32u);
        return;
    }
    if (state >= 0x20u) {
        return;
    }
    COMBAT_DAMAGE_TYPE = 4u;
    COMBAT_THRESHOLD_X = 11u;
    const unsigned int dmg =
        ((unsigned char)ITEM_ARROW_OR_ROD_LEVEL == 1u) ? 32u : 64u;
    collision_check_monster_shot_collision(monster_slot, weapon_slot, dmg);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_boomerang_or_food_collision(
    unsigned int monster_slot, unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:183-191. */
    if ((unsigned char)OBJ_STATE(weapon_slot) & 0x80u) {
        return;
    }
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    COMBAT_DAMAGE_TYPE = 2u;
    COMBAT_THRESHOLD_X = 10u;
    COMBAT_THRESHOLD_Y = 10u;
    COMBAT_HITBOX_X =
        (uint8_t)((unsigned char)OBJ_X(weapon_slot) + 4u);
    collision_check_monster_weapon_collision(
        monster_slot,
        (unsigned int)(unsigned char)((unsigned char)OBJ_Y(weapon_slot) + 8u));
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_sword_shot_or_magic_shot_collision(
    unsigned int monster_slot, unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:193-211. */
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    COMBAT_DAMAGE_TYPE = 16u;
    const unsigned char state = (unsigned char)OBJ_STATE(weapon_slot);
    if (state & 1u) {
        return;
    }
    COMBAT_THRESHOLD_X = 12u;
    unsigned int damage;
    if (state & 0x80u) {
        damage = 32u;
    } else {
        const unsigned char level = (unsigned char)ITEM_SWORD_LEVEL;
        COMBAT_DAMAGE_TYPE = 1u;
        damage = (level == 3u) ? 64u : (level == 2u) ? 32u : 16u;
    }
    collision_check_monster_shot_collision(monster_slot, weapon_slot, damage);
    if (!(unsigned char)COMBAT_COLLIDED) {
        return;
    }
    core_handle_shot_blocked(14u);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_bomb_or_fire_collision(
    unsigned int monster_slot, unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:213-235. */
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    COMBAT_DAMAGE_TYPE = 32u;
    COMBAT_DAMAGE_AMOUNT = 16u;
    COMBAT_THRESHOLD_X = 14u;
    const unsigned char state = (unsigned char)OBJ_STATE(weapon_slot);
    if (state >= 0x20u) {
        /* fall through to hitbox calc */
    } else if (state == 0x13u) {
        COMBAT_DAMAGE_TYPE = 8u;
        COMBAT_DAMAGE_AMOUNT = 64u;
        COMBAT_THRESHOLD_X = 24u;
    } else {
        return;
    }
    COMBAT_HITBOX_X =
        (uint8_t)((unsigned char)OBJ_X(weapon_slot) + 8u);
    COMBAT_HITBOX_Y =
        (uint8_t)((unsigned char)OBJ_Y(weapon_slot) + 8u);
    COMBAT_THRESHOLD_Y = (uint8_t)COMBAT_THRESHOLD_X;
    if (!collision_do_objects_collide_with_thresholds()) {
        return;
    }
    collision_handle_monster_weapon_collision(monster_slot, weapon_slot);
    if ((unsigned char)MON_INVINCIBILITY(monster_slot) &
        (unsigned char)COMBAT_DAMAGE_TYPE) {
        return;
    }
    link_collision_begin_shove(monster_slot);
}
