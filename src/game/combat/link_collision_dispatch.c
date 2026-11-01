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
#include "player_state.h"
#include "enemy_state.h"
#include "object_state.h"      /* OBJ_X, OBJ_Y, OBJ_DIR */
#include "progress_state.h"    /* MODE_VALUE */
#include "link_state.h"              /* LINK_HALT_FLAG */
#include "enemies/enemy_dispatch.h"
#include "enemies/bosses/boss_patra.h"
#include "enemies/bosses/boss_gohma.h"
#include "enemies/bosses/boss_gleeok.h"
#include "enemies/bosses/boss_dodongo.h"
#include "core/core_dispatch.h"      /* core_play_sample, core_get_opposite_dir */
#include "room/room_dispatch.h"      /* room_end_game_mode */
#include "combat/collision_dispatch.h" /* collision_do_objects_collide_with_thresholds,
                                        * collision_check_monster_*_collision battery */
#include "world/world_dispatch.h"      /* world_get_object_middle */
#include "../items/sword_shot.h"
#include "../../oracle/enemies/enemy_runtime.h"
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

/* T-172 option A: inlined into its per-enemy callers (call overhead). */
static inline __attribute__((always_inline)) void check_link_collision_body(unsigned int monster_slot)
{
    /* drain at link_collision_runtime.c:89-98. */
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

/* T-418: UpdateBubble's world/P1 entry gets the explicit wand check.
 * Party body-only contexts retain unrelated P1 weapon cells and must
 * never inspect or commit them. Party weapons use their own battery. */
__attribute__((always_inline)) void link_collision_check_link_collision(unsigned int monster_slot)
{
    world_get_object_middle(monster_slot);
    if (collision_check_upgraded_wand_bubble(monster_slot, 0u)) {
        sword_shot_commit_collision(0u);
        COMBAT_COLLIDED = 0u;
        return;
    }
    check_link_collision_body(monster_slot);
}

/* NES source: Z_04.asm:UpdateWallmaster/Burrower_AnimateDrawAndCheckCollisions.
 * Drained C: enrt_update_wallmaster / enemy_burrower_collisions_enabled.
 * Coverage: extra body/full/melee/beam collision admission.
 * Stance: EXTEND owner phase gates before any collision effects. */
static inline unsigned char party_monster_phase_is_inactive(unsigned int slot,
                                                           unsigned char player)
{
    const unsigned char type = MON_TYPE(slot);
    if (player == 0u) return 0u;
    if (!((type == 0x27u && (OBJ_STATE(slot) == 0u ||
                            enemy_capture_player(slot) == player)) ||
          ((type == 0x0Fu || type == 0x10u || type == 0x11u) &&
           !enemy_burrower_collisions_enabled(slot)))) return 0u;
    ROOM_MONSTER_COLLISION_COUNT = 0u;
    COMBAT_COLLIDED = 0u;
    COMBAT_HARM_FLAG = 0u;
    return 1u;
}

/* NES source: projectile-specific Link-contact phase gates.
 * Drained C: enemy_projectile_runtime.c held arrow / boomerang spin gate.
 * Coverage: PARTIAL extra body checks; P1 retains its world updater.
 * Stance: EXTEND the per-player collision boundary without a second tick. */
static inline void check_party_link_collision(unsigned int slot, unsigned char player)
{
    const unsigned char type = MON_TYPE(slot);
    if (party_monster_phase_is_inactive(slot, player)) return;
    if (player != 0u &&
        ((type == 0x5Bu && F_ram_0028_O(slot) != 0u) ||
         (type == 0x5Cu && OBJ_ANIM_TIMER(slot) == 1u))) {
        ROOM_MONSTER_COLLISION_COUNT = 0u;
        COMBAT_COLLIDED = 0u;
        COMBAT_HARM_FLAG = 0u;
        return;
    }
    world_get_object_middle(slot);
    check_link_collision_body(slot);
    if (player != 0u) enemy_finish_party_projectile_contact(slot);
}

/* T-172 option A: inlined into its callers (call overhead). */
/* NES source: Z_01.asm:CheckMonsterCollisions weapon battery.
 * Drained C: the existing battery below, unchanged call order.
 * Coverage: world/party weapons plus user-approved carrying-hand rescue.
 * Stance: EXTEND composition; weapon-only rescue never touches a captive. */
static inline __attribute__((always_inline)) unsigned char check_monster_weapon_collisions(
    unsigned int monster_slot, unsigned char melee_only, unsigned char player)
{
    world_get_object_middle(monster_slot);
    /* T-418: extra owners reach Bubbles through this battery. Other
     * weapons retain the monster's ordinary status-bit immunity. */
    if (collision_check_upgraded_wand_bubble(monster_slot, melee_only)) {
        sword_shot_commit_collision(player);
        COMBAT_COLLIDED = 0u;
        return 0u;
    }
    if (!((unsigned char)MON_STATUS_FLAGS(monster_slot) & 0x20u)) {
        if ((unsigned char)MON_HIT_REACTION(monster_slot)) {
            return 0u;
        }
        if (melee_only != 1u) {
        /* NES source: CheckMonsterCollisions + weapon-state rejection in
         * Z_01. Drained C: the same battery below; prior all-inactive gate.
         * Coverage: sword/beam, boomerang/food, bomb/fire named consumers.
         * Stance: EXTEND; skip inactive slots individually. Their writes
         * only prepare scratch for a rejected action; the final arrow/rod
         * check and Link collision still establish the consumed result.
         * Read current state per monster: a previous hit may clear a shot. */
        /* Idle slots still run the NES routine prologue (CheckMonsterCollisions
         * calls all six): boomerang sets $0D/$0E=$0A, the shot check $0D/$0E=$0C
         * (CheckMonsterSlenderWeaponCollision copies $0D to $0E), bomb/fire $0D=$0E.
         * Replay those threshold stores in call order so the scratch matches NES
         * (T-292 C5: rod windup tick leaves $0D=$0E, $0E=$0C). */
        /* Shared boomerang/food collision runs once in the world/P1 battery,
         * including P2 throws. Extra batteries own only their weapons. */
        if (melee_only == 0u && player == 0u && (unsigned char)OBJ_STATE(15u)) {
            collision_check_monster_boomerang_or_food_collision(monster_slot, 15u);
        } else {
            COMBAT_THRESHOLD_X = 10u;
            COMBAT_THRESHOLD_Y = 10u;
        }
        if ((unsigned char)OBJ_STATE(14u)) {
            collision_check_monster_sword_shot_or_magic_shot_collision(monster_slot, 14u);
            /* NES source: Z_01:CheckMonsterCollisions/HandleShotBlocked.
             * Drained C: the call immediately above. Coverage: state/dir
             * collision outputs; Stance: EXTEND native owner handoff.
             * Commit before any later slot/battery can observe the result. */
            sword_shot_commit_collision(player);
        } else {
            COMBAT_THRESHOLD_X = 12u;
            COMBAT_THRESHOLD_Y = 12u;
        }
        if (melee_only == 0u && player == 0u && (unsigned char)OBJ_STATE(16u))
            collision_check_monster_bomb_or_fire_collision(monster_slot, 16u);
        else
            COMBAT_THRESHOLD_X = 14u;
        if (melee_only == 0u && player == 0u && (unsigned char)OBJ_STATE(17u))
            collision_check_monster_bomb_or_fire_collision(monster_slot, 17u);
        else
            COMBAT_THRESHOLD_X = 14u;
        }
        if ((unsigned char)OBJ_STATE(13u) == 2u)
            collision_check_monster_sword_collision(monster_slot, 13u);
        /* The final check also establishes the NES scratch-cell result
         * consumed by later object work, even when its slot is empty. */
        if (!melee_only)
            collision_check_monster_arrow_or_rod_collision(monster_slot, 18u);
    }
    return 1u;
}

static inline __attribute__((always_inline)) void check_monster_collisions(
    unsigned int monster_slot, unsigned char melee_only, unsigned char player)
{
    /* drain at link_collision_runtime.c:100-128. */
    if (party_monster_phase_is_inactive(monster_slot, player)) return;
    /* NES source: Z_04.asm:UpdatePeahat/UpdateFlyingGhini.
     * Drained C: enemy_flyer_bridge.c:enrt_update_peahat/flying_ghini.
     * Coverage: FULL party weapon/body admission; P1 uses world tails.
     * Stance: EXTEND the existing battery with the same state-5 gate. */
    if (player != 0u &&
        (MON_TYPE(monster_slot) == 0x22u ||
         (MON_TYPE(monster_slot) == 0x1Au && ENEMY_AI_STATE(monster_slot) != 5u))) {
        check_party_link_collision(monster_slot, player);
        return;
    }
    /* NES source: Z_04.asm:UpdatePatra @LoopChildren/@CheckLinkCollision.
     * Drained C: boss_patra_update / boss_patra_has_children.
     * Coverage: extra-player full, melee and beam collision entry points.
     * Stance: EXTEND admission to retain the boss-owned protected phase. */
    if (player != 0u &&
        ((unsigned char)MON_TYPE(monster_slot) == 0x47u ||
         (unsigned char)MON_TYPE(monster_slot) == 0x48u) &&
        boss_patra_has_children()) {
        check_party_link_collision(monster_slot, player);
        return;
    }
    /* NES source: UpdateMoldorm preserves dir/timer around its battery,
     * then transfers a lethal segment hit to its tail. UpdateLamnola preserves
     * direction only and uses its own tail. Drained C: enrt_update_moldorm /
     * enrt_update_lamnola and their shared finish_collisions. Coverage: party
     * full/melee/beam entries. Stance: EXTEND the original world tail. */
    if (player != 0u && (MON_TYPE(monster_slot) == 0x41u ||
        MON_TYPE(monster_slot) == 0x3Au || MON_TYPE(monster_slot) == 0x3Bu)) {
        const unsigned char kind = MON_TYPE(monster_slot);
        const unsigned char dir = OBJ_DIR(monster_slot);
        const unsigned char timer = ENEMY_MOVE_TIMER(monster_slot);
        if (!check_monster_weapon_collisions(monster_slot, melee_only, player)) return;
        check_party_link_collision(monster_slot, player);
        if (kind == 0x41u) ENEMY_MOVE_TIMER(monster_slot) = timer;
        OBJ_DIR(monster_slot) = dir;
        if (kind == 0x41u) enrt_moldorm_finish_collisions(monster_slot);
        else enrt_lamnola_finish_collisions(monster_slot);
        return;
    }
    if (!check_monster_weapon_collisions(monster_slot, melee_only, player)) return;
    check_party_link_collision(monster_slot, player);
    {
        const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
        const unsigned char dying = (unsigned char)MON_METASTATE(monster_slot);
        if (!dying) {
            if ((mtype == 0x27u || mtype == 0x17u) &&
                (unsigned char)COMBAT_HARM_FLAG) {
                enemy_capture_contact(monster_slot, player);
            }
            return;
        }
        if (player != 0u && mtype == 0x21u)
            enemy_ghini_finish_collisions(monster_slot);
        if (mtype == 0x05u || mtype == 0x06u) {
            if ((unsigned char)OBJ_STATE(monster_slot) & 0x80u) {
                const unsigned char bslot =
                    (unsigned char)MON_BOUNCE_TURNS(monster_slot);
                MON_TYPE(bslot) = 0u;
            }
        }
    }
}

/* P1 and item contexts retain the full source battery. Sword-only contexts
 * share every damage/contact/bookkeeping rule while leaving P1's unrelated
 * weapon cells untouched. Skipped inactive prologues expose no gameplay output;
 * the body path establishes its own scratch, flags and object center. */
/* NES source: UpdateWallmaster captured draw path; explicit multiplayer rescue extension.
 * Drained C: existing weapon battery and capture/death lifecycle.
 * Coverage: P1 rescuing an extra captive; extra rescuers use their normal battery.
 * Stance: EXTEND only while another party member is carried; 1P unchanged. */
void link_collision_check_wallmaster_rescue(unsigned int slot)
{
    unsigned char owner;
    if (g_player_count <= 1u || MON_TYPE(slot) != 0x27u) return;
    owner = enemy_capture_player(slot);
    if (owner == PLAYER_REF_NONE || owner == 0u) return;
    (void)check_monster_weapon_collisions(slot, 0u, 0u);
}

__attribute__((always_inline))
void link_collision_check_monster_collisions(unsigned int monster_slot)
{
    check_monster_collisions(monster_slot, 0u, 0u);
}
/* NES source: Z_04.asm:Ganon_CheckCollisions/ScenePhase2.
 * Drained C: enemy_ganon_runtime.c boss-owned collision tail.
 * Coverage: extra-player body, sword, beam-only and owned slot18 contexts.
 * Stance: EXTEND only party entry points; retain the shared P1 helper. */
static inline __attribute__((always_inline)) void check_party_monster_weapons(
    unsigned int slot, unsigned char melee_only, unsigned char player)
{
    if (player >= 4u) return;
    /* NES source: Z_04.asm:UpdateGleeokHead/reset tail.
     * Drained C: boss_gleeok.c head collision owner.
     * Coverage: PARTIAL extra loaded weapon contexts, preserved HP effects.
     * Stance: EXTEND tail admission before world metastate dispatch. */
    if (player != 0u && (unsigned char)MON_TYPE(slot) == 0x46u) {
        boss_gleeok_check_head_party_collisions(slot, player, melee_only);
        return;
    }
    /* NES source: Z_04.asm:Dodongo_CheckCollisionsStandardSize.
     * Drained C: boss_dodongo.c party collision composition.
     * Coverage: PARTIAL loaded extra sword/beam/owned-ranged contexts.
     * Stance: EXTEND boss admission; shared P1 battery unchanged. */
    if (player != 0u &&
        ((unsigned char)MON_TYPE(slot) == 0x31u ||
         (unsigned char)MON_TYPE(slot) == 0x32u)) {
        boss_dodongo_check_party_collisions(slot, player, melee_only);
        return;
    }
    if (player != 0u && (unsigned char)MON_TYPE(slot) == 0x3Eu) {
        enemy_ganon_check_party_collisions(slot, melee_only == 0u ? 3u : 1u);
        return;
    }
    if (player != 0u &&
        ((unsigned char)MON_TYPE(slot) == 0x33u ||
         (unsigned char)MON_TYPE(slot) == 0x34u)) {
        boss_gohma_check_party_collisions(slot, player, melee_only);
        return;
    }
    check_monster_collisions(slot, melee_only, player);
}

void link_collision_check_monster_collisions_for_player(unsigned int monster_slot,
                                                       unsigned char player)
{
    check_party_monster_weapons(monster_slot, 0u, player);
}
void link_collision_check_monster_melee_collisions(unsigned int monster_slot)
{
    check_monster_collisions(monster_slot, 1u, 0u);
}
void link_collision_check_monster_melee_for_player(unsigned int slot, unsigned char player)
{
    check_party_monster_weapons(slot, 1u, player);
}
void link_collision_check_player_body(unsigned int slot, unsigned char player)
{
    if (player != 0u && (unsigned char)MON_TYPE(slot) == 0x46u) {
        boss_gleeok_check_head_party_collisions(slot, player, 3u);
        return;
    }
    /* NES source: Z_04.asm:Dodongo_CheckCollisions.
     * Drained C: boss_dodongo.c:boss_dodongo_check_party_body.
     * Coverage: PARTIAL extra idle body contexts; shared P1 battery unchanged.
     * Stance: EXTEND party dispatch to the existing boss shape. */
    if (player != 0u &&
        ((unsigned char)MON_TYPE(slot) == 0x31u ||
         (unsigned char)MON_TYPE(slot) == 0x32u)) {
        boss_dodongo_check_party_body(slot, player);
        return;
    }
    if (player != 0u && (unsigned char)MON_TYPE(slot) == 0x3Eu) {
        enemy_ganon_check_party_collisions(slot, 0u);
        return;
    }
    if (player != 0u &&
        ((unsigned char)MON_TYPE(slot) == 0x33u ||
         (unsigned char)MON_TYPE(slot) == 0x34u)) {
        boss_gohma_check_party_collisions(slot, player, 3u);
        return;
    }
    check_party_link_collision(slot, player);
    if (MON_METASTATE(slot) == 0u && COMBAT_HARM_FLAG != 0u)
        enemy_capture_contact(slot, player);
}
void link_collision_check_monster_sword_beam_collisions(unsigned int monster_slot,
                                                       unsigned char player)
{
    check_party_monster_weapons(monster_slot, 2u, player);
}

/* NES source: Z_04.asm:Gohma_CheckCollisions/CheckMonsterCollisions.
 * Drained C: native battery and body helper in this file.
 * Coverage: one already-selected Gohma part, loaded party context.
 * Stance: EXTEND callback boundary; do not re-enter boss dispatch. */
void link_collision_check_gohma_part_for_player(unsigned int slot, unsigned char player,
                                              unsigned char context)
{
    if (player >= 4u || context > 3u) return;
    if (context == 3u) check_party_link_collision(slot, player);
    else check_monster_collisions(slot, context, player);
}

/* Boss-selected part: context3 never reads unrelated weapon cells. */
void link_collision_check_dodongo_part_for_player(unsigned int slot, unsigned char player,
                                                 unsigned char context)
{
    if (player == 0u || player >= 4u || context > 3u) return;
    if (context == 3u) check_party_link_collision(slot, player);
    else check_monster_collisions(slot, context, player);
}

/* Already-selected flying-head part; no recursive boss dispatch. */
void link_collision_check_gleeok_head_part(unsigned int slot, unsigned char player,
                                           unsigned char context)
{
    if (player == 0u || player >= 4u || context > 3u) return;
    if (context == 3u) check_party_link_collision(slot, player);
    else check_monster_collisions(slot, context, player);
}
