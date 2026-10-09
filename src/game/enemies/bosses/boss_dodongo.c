/* Phase 8 Task 8.3 — Dodongo bridge body.
 *
 * NES source: Z_04.asm:5856-6005 (UpdateDodongo +
 *             UpdateDodongoState_JumpTable + State0_Move +
 *             State1_Bloated_JumpTable + State1_Bloated_Sub_Wait +
 *             State1_Bloated_Sub_Die + State1_Bloated_Sub_End +
 *             State2_Stunned).
 * Drained C:  src/oracle/enemies/enemy_dodongo_runtime.c — INIT
 *             (enrt_init_dodongo, enrt_dodongo_dec_bloated_timer,
 *             enrt_update_dodongo_state2_stunned,
 *             enrt_update_dodongo_state1_bloated_sub_die,
 *             enrt_update_dodongo_bloated_sub_end,
 *             enrt_dodongo_check_collisions,
 *             enrt_dodongo_check_bomb_hit, enrt_dodongo_draw).
 * Coverage:   FULL — UpdateDodongo umbrella composes drained primitives
 *             + native-ported state dispatcher + native State0_Move +
 *             native State1_Bloated dispatcher + native Sub_Wait.
 * Stance:     EXTEND — fills the Dodongo branches Z_04 left out of the
 *             drain (state/bloated dispatchers, Move, Sub_Wait) while
 *             tail-calling drained pieces for the rest.
 *
 * Bloated substate cell aliases (per ObjVars.inc):
 *   $042C  ENEMY_TURN_TIMER       = Dodongo_ObjBloatedSubstate
 *   $0437  ENEMY_FLAP_PHASE       = Dodongo_ObjBombHits
 *   $045E  ENEMY_BLOATED_TIMER    = Dodongo_ObjBloatedTimer
 */

#include "boss_dodongo.h"
#include "platform_abi.h"
#include "enemy_state.h"
#include "world/world_dispatch.h"          /* world_get_object_middle */
#include "combat/collision_dispatch.h"     /* collision_check_monster_sword_collision */
#include "core/core_dispatch.h"            /* core_update_dead_dummy, core_reset_obj_metastate_and_timer */

/* Forward declarations of drained Dodongo primitives. */
extern void         enrt_init_dodongo(unsigned int slot);
extern void         enrt_update_dodongo_state2_stunned(unsigned int slot);
extern void         enrt_update_dodongo_state1_bloated_sub_die(unsigned int slot);
extern void         enrt_update_dodongo_bloated_sub_end(unsigned int slot);
extern void         enrt_dodongo_check_collisions(unsigned int slot);
extern void         enrt_dodongo_check_bomb_hit(unsigned int slot);
extern void         enrt_dodongo_draw(unsigned int slot);
extern void         enrt_dodongo_dec_bloated_timer(unsigned int slot);

/* Wanderer_TargetPlayer drain (called by State0_Move). */
extern void         enrt_wanderer_target_player(unsigned int slot);

/* Dodongo_ObjBombHits cell ($0437) — aliased through ENEMY_FLAP_PHASE
 * which is the existing alias for that address per enemy_state.h:25. */
#define DODONGO_OBJ_BOMB_HITS(slot)         ENEMY_FLAP_PHASE(slot)
/* Dodongo_ObjBloatedSubstate cell ($042C) = ENEMY_TURN_TIMER alias. */
#define DODONGO_OBJ_BLOATED_SUBSTATE(slot)  ENEMY_TURN_TIMER(slot)
/* Dodongo_ObjBloatedTimer cell ($045E) = ENEMY_BLOATED_TIMER alias. */
#define DODONGO_OBJ_BLOATED_TIMER(slot)     ENEMY_BLOATED_TIMER(slot)

/* DodongoBloatedWaitTimes per Z_04.asm:5867 — substate -> wait frames. */
static const unsigned char k_dodongo_bloated_wait_times[3] = {
    0x20u, 0x40u, 0x40u
};

/* ---- callee shims for the drained Dodongo body --------------------- */

/* enemy_dodongo_runtime.c references c_get_object_middle,
 * c_check_monster_sword_collision, z07_update_dead_dummy, and
 * z04_update_dodongo_bloated_sub_end via extern decls in
 * enemy_runtime_private.h. Resolve them to the dispatcher entry points
 * already present in the link (world_get_object_middle,
 * collision_check_monster_sword_collision, core_update_dead_dummy)
 * and to the drained twin in this TU. */

void c_get_object_middle(unsigned int slot)
{
    world_get_object_middle(slot);
}

void c_check_monster_sword_collision(unsigned int monster_slot,
                                     unsigned int weapon_slot)
{
    collision_check_monster_sword_collision(monster_slot, weapon_slot);
}

void z07_update_dead_dummy(unsigned int slot)
{
    core_update_dead_dummy(slot);
}

void z04_update_dodongo_bloated_sub_end(unsigned int slot)
{
    enrt_update_dodongo_bloated_sub_end(slot);
}

/* ---- native-ported Dodongo state machinery ------------------------- */

/* UpdateDodongoState1_Bloated_Sub_Wait (Z_04.asm:5945-5993). */
static void boss_dodongo_state1_bloated_sub_wait(unsigned int slot)
{
    unsigned char timer = (unsigned char)DODONGO_OBJ_BLOATED_TIMER(slot);
    /* DEY then BEQ AdvanceSubstate: was timer == 1 ? */
    if (timer == 1u) {
        /* AdvanceSubstate. */
        DODONGO_OBJ_BLOATED_SUBSTATE(slot) =
            (unsigned char)(DODONGO_OBJ_BLOATED_SUBSTATE(slot) + 1u);
        if ((unsigned char)DODONGO_OBJ_BLOATED_SUBSTATE(slot) >= 0x02u) {
            if ((unsigned char)DODONGO_OBJ_BOMB_HITS(slot) < 0x02u) {
                DODONGO_OBJ_BLOATED_SUBSTATE(slot) = 0x04u;
            }
        }
        enrt_dodongo_dec_bloated_timer(slot);
        return;
    }
    /* DEY then BPL DecBloated: timer 2..$80 (0 or >= $81 reloads, T-171). */
    if (timer >= 2u && timer <= 0x80u) {
        enrt_dodongo_dec_bloated_timer(slot);
        return;
    }
    /* timer == 0: reset from table indexed by current substate. */
    {
        unsigned char substate =
            (unsigned char)DODONGO_OBJ_BLOATED_SUBSTATE(slot);
        DODONGO_OBJ_BLOATED_TIMER(slot) =
            k_dodongo_bloated_wait_times[substate & 0x03u];
        if (substate != 0u) {
            enrt_dodongo_dec_bloated_timer(slot);
            return;
        }
        /* substate == 0: deactivate first bomb slot ($10) +
         * bomb_hits++. */
        OBJ(0x00ACu, 16u) = 0u;
        DODONGO_OBJ_BOMB_HITS(slot) =
            (unsigned char)(DODONGO_OBJ_BOMB_HITS(slot) + 1u);
        enrt_dodongo_dec_bloated_timer(slot);
    }
}

/* UpdateDodongoState1_Bloated dispatcher (Z_04.asm:5936-5944). */
static void boss_dodongo_state1_bloated(unsigned int slot)
{
    unsigned char substate =
        (unsigned char)DODONGO_OBJ_BLOATED_SUBSTATE(slot);
    switch (substate) {
        case 0u:
        case 1u:
        case 2u:
            boss_dodongo_state1_bloated_sub_wait(slot);
            return;
        case 3u:
            enrt_update_dodongo_state1_bloated_sub_die(slot);
            return;
        case 4u:
        default:
            enrt_update_dodongo_bloated_sub_end(slot);
            return;
    }
}

/* UpdateDodongoState0_Move (Z_04.asm:5876-5919). */
static void boss_dodongo_state0_move(unsigned int slot)
{
    unsigned char dir;
    unsigned char saved_offset;
    unsigned char saved_x_for_offset;

    /* If facing left (dir & $0D == 0): no X-shift. Otherwise pre-shift
     * X by $10 so Wanderer evaluates from the other side of the long
     * sprite, then unshift after the call. */
    dir = (unsigned char)ENEMY_DIR(slot);
    if ((unsigned char)(dir & 0x0Du) == 0u) {
        saved_offset = 0x00u;
    } else {
        ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot) + 0x10u);
        saved_offset = 0xF0u;  /* signed -$10 to undo the shift */
    }
    saved_x_for_offset = saved_offset;

    /* Turn rate $20 lives at ObjTurnRate ($041F) = ENEMY_AIR_SPEED. */
    ENEMY_AIR_SPEED(slot) = 0x20u;
    enrt_wanderer_target_player(slot);

    /* Restore X by adding the saved offset. */
    ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot) + saved_x_for_offset);

    /* If new X < $20, force right (dir = 1). NES: BCS skips the store. */
    if ((unsigned char)ENEMY_X(slot) < 0x20u) {
        ENEMY_DIR(slot) = 0x01u;
    }
}

/* UpdateDodongoState (Z_04.asm:5868-5874). */
static void boss_dodongo_update_state(unsigned int slot)
{
    unsigned char state = (unsigned char)ENEMY_STATE_TIMER(slot);
    switch (state) {
        case 0u:
            boss_dodongo_state0_move(slot);
            return;
        case 1u:
            boss_dodongo_state1_bloated(slot);
            return;
        case 2u:
        default:
            enrt_update_dodongo_state2_stunned(slot);
            return;
    }
}

/* UpdateDodongo (Z_04.asm:5856-5860). */
void boss_dodongo_update(unsigned int slot)
{
    boss_dodongo_update_state(slot);
    enrt_dodongo_check_collisions(slot);
    enrt_dodongo_check_bomb_hit(slot);
    enrt_dodongo_draw(slot);
}
