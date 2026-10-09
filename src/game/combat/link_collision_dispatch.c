/* link_collision_dispatch.c — native link-collision subsystem dispatch
 * (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; calls native core_* + room_*.
 * Drain provenance: src/oracle/combat/link_collision_runtime.c.
 */

#include "link_collision_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "combat_state.h"      /* MON_TYPE, ROOM_KILL_COUNT,
                                * ROOM_CHAIN_KILL_COUNT, ROOM_CHAIN_KILL_BONUS,
                                * COMBAT_THRESHOLD_X/Y, LINK_HEARTS,
                                * LINK_PARTIAL_HEART, LINK_RING_LEVEL,
                                * LINK_ACTION_TIMER, LINK_DIR,
                                * COMBAT_HARM_FLAG, COMBAT_DAMAGE_TYPE,
                                * COMBAT_HITBOX_X/Y, COMBAT_SHOVE_DIR,
                                * COMBAT_ABS_DY, COMBAT_PART_INDEX,
                                * COMBAT_WEAPON_SLOT, MON_INVINCIBILITY,
                                * MON_STATUS_FLAGS, MON_HIT_REACTION,
                                * MON_SHOVE_DIR, MON_SHOVE_TIMER, MON_SUBSTATE,
                                * LINK_STUN_TIMER, OBJ_GRID_OFFSET */
#include "object_state.h"      /* OBJ_X, OBJ_Y, OBJ_DIR */
#include "progress_state.h"    /* MODE_VALUE */
#include "link_state.h"              /* LINK_HALT_FLAG */
#include "core/core_dispatch.h"      /* core_play_sample, core_get_opposite_dir */
#include "room/room_dispatch.h"      /* room_end_game_mode */
#include "combat/collision_dispatch.h" /* collision_do_objects_collide_with_thresholds,
                                        * collision_check_monster_*_collision battery */
#include "world/world_dispatch.h"      /* world_get_object_middle */
/* COMBAT_COLLIDED, MON_STUN_TIMER, LINK_DAMAGE_DISABLE_FLAG,
 * LINK_SHIELD_BLOCK_FLAG, SFX_COMBAT, ROOM_MONSTER_COLLISION_COUNT —
 * already in combat_state.h. */

/* Z_01.asm ObjTypeToDamagePoints[93] (line 2577). Used by HarmLink to
 * look up damage threshold per monster type. */
static const unsigned char k_obj_type_to_damage_points[93] = {
    0x60u, 0x02u, 0x01u, 0x80u, 0x80u, 0x01u, 0x80u, 0x80u,
    0x80u, 0x80u, 0x80u, 0x01u, 0x02u, 0x80u, 0x80u, 0x01u,
    0x80u, 0x80u, 0x01u, 0x01u, 0x80u, 0x80u, 0x02u, 0x01u,
    0x02u, 0x00u, 0x80u, 0x80u, 0x80u, 0x80u, 0x01u, 0x80u,
    0x80u, 0x01u, 0x01u, 0x02u, 0x01u, 0x02u, 0x02u, 0x80u,
    0x80u, 0x80u, 0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x02u, 0x01u, 0x01u, 0x02u, 0x02u, 0x00u, 0x00u, 0x00u,
    0x02u, 0x02u, 0x02u, 0x02u, 0x01u, 0x01u, 0x04u, 0x80u,
    0x80u, 0x80u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x02u,
    0x02u, 0x01u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x80u, 0x80u, 0x80u, 0x01u, 0x02u,
    0x02u, 0x04u, 0x04u, 0x80u, 0x01u
};

void link_collision_link_be_harmed(unsigned int monster_slot)
{
    /* drain at link_collision_runtime.c:6-46. */
    if ((unsigned char)MON_TYPE(monster_slot) != 0x2Eu) {
        core_play_sample(8u);
    }
    {
        unsigned char rings = (unsigned char)LINK_RING_LEVEL;
        while (rings--) {
            const unsigned char carry =
                (unsigned char)((unsigned char)COMBAT_THRESHOLD_X & 1u);
            COMBAT_THRESHOLD_X =
                (uint8_t)((unsigned char)COMBAT_THRESHOLD_X >> 1);
            COMBAT_THRESHOLD_Y =
                (uint8_t)(((carry) << 7) |
                          ((unsigned char)COMBAT_THRESHOLD_Y >> 1));
        }
    }
    ROOM_KILL_COUNT = 0u;
    ROOM_CHAIN_KILL_COUNT = 0u;
    ROOM_CHAIN_KILL_BONUS = 0u;
    for (;;) {
        const unsigned char partial = (unsigned char)LINK_PARTIAL_HEART;
        const unsigned char dmg_lo = (unsigned char)COMBAT_THRESHOLD_Y;
        if (partial >= dmg_lo) {
            LINK_PARTIAL_HEART = (uint8_t)(partial - dmg_lo);
            if ((unsigned char)COMBAT_THRESHOLD_X >
                ((unsigned char)LINK_HEARTS & 0x0Fu)) {
                break;
            }
            LINK_HEARTS = (uint8_t)((unsigned char)LINK_HEARTS -
                                    (unsigned char)COMBAT_THRESHOLD_X);
            return;
        }
        COMBAT_THRESHOLD_Y = (uint8_t)(dmg_lo - partial);
        if (((unsigned char)LINK_HEARTS & 0x0Fu) == 0u) {
            break;
        }
        LINK_HEARTS = (uint8_t)((unsigned char)LINK_HEARTS - 1u);
        LINK_PARTIAL_HEART = 0xFFu;
    }
    LINK_HEARTS = (uint8_t)((unsigned char)LINK_HEARTS & 0xF0u);
    (void)room_end_game_mode();
    LINK_PARTIAL_HEART = 0u;
    LINK_ACTION_TIMER = 0u;
    MODE_VALUE = 17u;
    LINK_DIR = 4u;
}

void link_collision_begin_shove(unsigned int monster_slot)
{
    /* drain at link_collision_runtime.c:130-179. */
    const unsigned int weapon_slot = (unsigned int)COMBAT_WEAPON_SLOT;
    if (monster_slot < 0x0Du) {
        if ((unsigned char)MON_INVINCIBILITY(monster_slot) &
            (unsigned char)COMBAT_DAMAGE_TYPE) {
            return;
        }
    }
    COMBAT_SHOVE_DIR = 8u;
    COMBAT_HITBOX_X = (uint8_t)OBJ_Y(monster_slot);
    COMBAT_HITBOX_Y = (uint8_t)OBJ_Y(weapon_slot);
    {
        unsigned char check_h;
        if (weapon_slot == 0u && (unsigned char)OBJ_GRID_OFFSET(0)) {
            check_h = ((unsigned char)LINK_DIR & 0x03u) ? 1u : 0u;
        } else {
            check_h = ((unsigned char)COMBAT_ABS_DY < 4u) ? 1u : 0u;
        }
        if (check_h) {
            COMBAT_SHOVE_DIR = 2u;
            COMBAT_HITBOX_X = (uint8_t)OBJ_X(monster_slot);
            COMBAT_HITBOX_Y = (uint8_t)OBJ_X(weapon_slot);
        }
    }
    if ((unsigned char)COMBAT_HITBOX_X < (unsigned char)COMBAT_HITBOX_Y) {
        COMBAT_SHOVE_DIR =
            (uint8_t)((unsigned char)COMBAT_SHOVE_DIR >> 1);
    }
    if (weapon_slot != 0u) {
        COMBAT_SHOVE_DIR = (uint8_t)OBJ_DIR(weapon_slot);
        if ((unsigned char)MON_STATUS_FLAGS(monster_slot) & 0x80u) {
            COMBAT_SHOVE_DIR =
                (uint8_t)((unsigned char)COMBAT_SHOVE_DIR | 0x40u);
        }
        if ((unsigned char)MON_HIT_REACTION(monster_slot)) {
            return;
        }
        const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
        if (mtype == 0x33u || mtype == 0x34u) {
            const unsigned char part = (unsigned char)COMBAT_PART_INDEX;
            if (part != 3u && part != 4u) {
                return;
            }
            if ((unsigned char)MON_SUBSTATE(monster_slot) != 3u) {
                return;
            }
        }
        MON_SHOVE_DIR(monster_slot) =
            (uint8_t)((unsigned char)COMBAT_SHOVE_DIR | 0x80u);
        MON_SHOVE_TIMER(monster_slot) = 64u;
        MON_HIT_REACTION(monster_slot) = 16u;
    } else {
        if ((unsigned char)LINK_STUN_TIMER) {
            return;
        }
        MON_SHOVE_DIR(0) =
            (uint8_t)((unsigned char)COMBAT_SHOVE_DIR | 0x80u);
        LINK_STUN_TIMER = 24u;
        MON_SHOVE_TIMER(0) = 32u;
        if (monster_slot >= 0x0Du) {
            return;
        }
        if ((unsigned char)MON_STATUS_FLAGS(monster_slot) & 0x80u) {
            return;
        }
        if ((unsigned char)MON_TYPE(monster_slot) == 0x12u) {
            return;
        }
        OBJ_DIR(monster_slot) = (uint8_t)core_get_opposite_dir(
            (unsigned int)(unsigned char)OBJ_DIR(monster_slot));
    }
}

void link_collision_harm_link(unsigned int monster_slot)
{
    /* drain at link_collision_runtime.c:48-58. */
    link_collision_begin_shove(monster_slot);
    COMBAT_HARM_FLAG =
        (uint8_t)((unsigned char)COMBAT_HARM_FLAG + 1u);
    const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
    /* drain reads ObjTypeToDamagePoints[mtype] without bounds-check;
     * MON_TYPE values are constrained to defined enemy types <= 92. */
    const unsigned char tbl = k_obj_type_to_damage_points[mtype];
    COMBAT_THRESHOLD_X = (uint8_t)(tbl & 0x0Fu);
    COMBAT_THRESHOLD_Y = (uint8_t)(tbl & 0xF0u);
    link_collision_link_be_harmed(monster_slot);
}

void link_collision_check_link_collision_preinit(unsigned int monster_slot)
{
    /* drain at link_collision_runtime.c:60-87. */
    if ((unsigned char)LINK_ACTION_TIMER == 0x40u) {
        return;
    }
    if ((unsigned char)LINK_DAMAGE_DISABLE_FLAG) {
        return;
    }
    const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
    if (mtype >= 0x53u &&
        ((unsigned char)OBJ_STATE(monster_slot) & 0xF0u) != 0x10u) {
        return;
    }
    COMBAT_HITBOX_X = (uint8_t)((unsigned char)OBJ_X(0) + 8u);
    COMBAT_HITBOX_Y = (uint8_t)((unsigned char)OBJ_Y(0) + 8u);
    COMBAT_THRESHOLD_X = 9u;
    COMBAT_THRESHOLD_Y = 9u;
    if (!collision_do_objects_collide_with_thresholds()) {
        return;
    }
    if (mtype < 0x53u) {
        link_collision_harm_link(monster_slot);
        return;
    }
    ROOM_MONSTER_COLLISION_COUNT =
        (uint8_t)((unsigned char)ROOM_MONSTER_COLLISION_COUNT + 1u);
    if (mtype == 0x56u || mtype == 0x5Au) {
        link_collision_harm_link(monster_slot);
        return;
    }
    if ((unsigned char)LINK_ACTION_TIMER & 0xF0u) {
        link_collision_harm_link(monster_slot);
        return;
    }
    {
        const unsigned char or_dirs =
            (unsigned char)((unsigned char)LINK_DIR |
                            (unsigned char)OBJ_DIR(monster_slot));
        if ((or_dirs & 0x0Cu) != 0x0Cu && (or_dirs & 0x03u) != 0x03u) {
            link_collision_harm_link(monster_slot);
            return;
        }
    }
    if (mtype >= 0x55u && mtype <= 0x5Au) {
        if (!(unsigned char)LINK_SHIELD_BLOCK_FLAG) {
            link_collision_harm_link(monster_slot);
            return;
        }
    }
    SFX_COMBAT = 1u;
    COMBAT_COLLIDED = 0u;
}

void link_collision_check_link_collision(unsigned int monster_slot)
{
    /* drain at link_collision_runtime.c:89-98. */
    world_get_object_middle(monster_slot);
    ROOM_MONSTER_COLLISION_COUNT = 0u;
    COMBAT_COLLIDED = 0u;
    COMBAT_DAMAGE_TYPE = 0u;
    COMBAT_HARM_FLAG = 0u;
    COMBAT_WEAPON_SLOT = 0u;
    if ((unsigned char)LINK_STUN_TIMER ||
        (unsigned char)LINK_HALT_FLAG ||
        (unsigned char)MON_STUN_TIMER(0) ||
        (unsigned char)MON_STUN_TIMER(monster_slot)) {
        return;
    }
    link_collision_check_link_collision_preinit(monster_slot);
}

void link_collision_check_monster_collisions(unsigned int monster_slot)
{
    /* drain at link_collision_runtime.c:100-128. */
    world_get_object_middle(monster_slot);
    if (!((unsigned char)MON_STATUS_FLAGS(monster_slot) & 0x20u)) {
        if ((unsigned char)MON_HIT_REACTION(monster_slot)) {
            return;
        }
        /* NES source: CheckMonsterCollisions + weapon-state rejection in
         * Z_01. Drained C: the same battery below; prior all-inactive gate.
         * Coverage: sword/beam, boomerang/food, bomb/fire named consumers.
         * Stance: EXTEND; skip inactive slots individually. Their writes
         * only prepare scratch for a rejected action; the final arrow/rod
         * check and Link collision still establish the consumed result.
         * Read current state per monster: a previous hit may clear a shot. */
        if ((unsigned char)OBJ_STATE(15u))
            collision_check_monster_boomerang_or_food_collision(monster_slot, 15u);
        if ((unsigned char)OBJ_STATE(14u))
            collision_check_monster_sword_shot_or_magic_shot_collision(monster_slot, 14u);
        if ((unsigned char)OBJ_STATE(16u))
            collision_check_monster_bomb_or_fire_collision(monster_slot, 16u);
        if ((unsigned char)OBJ_STATE(17u))
            collision_check_monster_bomb_or_fire_collision(monster_slot, 17u);
        if ((unsigned char)OBJ_STATE(13u) == 2u)
            collision_check_monster_sword_collision(monster_slot, 13u);
        /* The final check also establishes the NES scratch-cell result
         * consumed by later object work, even when its slot is empty. */
        collision_check_monster_arrow_or_rod_collision(monster_slot, 18u);
    }
    link_collision_check_link_collision(monster_slot);
    {
        const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
        const unsigned char dying = (unsigned char)MON_METASTATE(monster_slot);
        if (!dying) {
            if ((mtype == 0x27u || mtype == 0x17u) &&
                (unsigned char)COMBAT_HARM_FLAG) {
                MON_BOUNCE_TURNS(monster_slot) =
                    (uint8_t)((unsigned char)MON_BOUNCE_TURNS(monster_slot) + 1u);
            }
            return;
        }
        if (mtype == 0x05u || mtype == 0x06u) {
            if ((unsigned char)OBJ_STATE(monster_slot) & 0x80u) {
                const unsigned char bslot =
                    (unsigned char)MON_BOUNCE_TURNS(monster_slot);
                MON_TYPE(bslot) = 0u;
            }
        }
    }
}
