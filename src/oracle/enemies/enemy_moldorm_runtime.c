/* Phase 8 Task 8.9 — Moldorm drain.
 *
 * NES source: reference/aldonunez/Z_04.asm
 *               4763 InitMoldorm
 *               4907 UpdateMoldorm
 *               4999 ControlMoldormFlight (+ JT @5002)
 *               5008 Moldorm_Chase
 *               5025 Moldorm_ChangeFlyingState
 *               5054 Moldorm_Wander
 *               5085 Moldorm_PropagateDirs
 *               5133 Flyer_MoldormDecideState  (already drained)
 * Drained C:  NONE (this file).
 * Coverage:   FULL — InitMoldorm + UpdateMoldorm composed from already-
 *             drained primitives (c_flyer_chase, c_flyer_wander,
 *             c_move_flyer, c_check_monster_collisions,
 *             c_anim_write_sprite, c_reset_obj_metastate,
 *             enrt_check_boss_hit_reaction, enrt_flyer_moldorm_decide_state)
 *             plus three local helpers (control_moldorm_flight,
 *             moldorm_chase, moldorm_wander, moldorm_change_flying_state,
 *             moldorm_propagate_dirs).
 * Stance:     ADOPT — per-line port of NES Moldorm body. Head-only
 *             slot gating preserved (slots 5 and $A run flight/decision;
 *             body segments only animate + collision-check + propagate
 *             from head's OldDir).
 */

#include "enemy_runtime_private.h"
#include "platform_abi.h"

extern void c_flyer_chase(unsigned int slot);
extern void c_flyer_wander(unsigned int slot);

static void moldorm_propagate_dirs(unsigned int slot);

/* Moldorm_PropagateDirs (Z_04.asm:5085).
 *   timer != $10 → exit.
 *   timer == $10 → BounceDir != 0 → ObjDir = BounceDir, BounceDir = 0.
 *   Then 4-iteration loop indexed by Y starting at 0 (slot==5) or 5
 *   (slot==$A). Each iter copies OldDir@(Y+2) → OldDir@(Y+1) AND
 *   ObjDir@(Y+1). Tail: copy ObjDir@head → OldDir@head. */
static void moldorm_propagate_dirs(unsigned int slot)
{
    unsigned int loop;
    unsigned int y;

    if (ENEMY_MOVE_TIMER(slot) != 0x10u)
        return;

    if (ENEMY_MOLDORM_BOUNCE_DIR(slot) != 0u) {
        ENEMY_DIR(slot) = ENEMY_MOLDORM_BOUNCE_DIR(slot);
        ENEMY_MOLDORM_BOUNCE_DIR(slot) = 0u;
    }

    y = (slot == 5u) ? 0u : 5u;
    for (loop = 0u; loop < 4u; ++loop, ++y) {
        unsigned char d = ENEMY_MOLDORM_OLD_DIR(y + 2u);
        ENEMY_MOLDORM_OLD_DIR(y + 1u) = d;
        ENEMY_DIR(y + 1u) = d;
    }

    ENEMY_MOLDORM_OLD_DIR(slot) = ENEMY_DIR(slot);
}

/* Moldorm_ChangeFlyingState (Z_04.asm:5025).
 *   Flyer_MoldormDecideState + ObjTimer = $10. If ObjDir@(slot-1) != 0,
 *   propagate dirs. */
static void moldorm_change_flying_state(unsigned int slot)
{
    enrt_flyer_moldorm_decide_state(slot);
    ENEMY_MOVE_TIMER(slot) = 0x10u;

    if (slot == 0u)
        return;
    if (ENEMY_DIR(slot - 1u) != 0u)
        moldorm_propagate_dirs(slot);
}

/* Moldorm_Chase (Z_04.asm:5008). */
static void moldorm_chase(unsigned int slot)
{
    if (slot != 5u && slot != 0x0Au)
        return;

    c_flyer_chase(slot);

    if (ENEMY_MOVE_TIMER(slot) != 0u) {
        moldorm_propagate_dirs(slot);
        return;
    }
    moldorm_change_flying_state(slot);
}

/* Moldorm_Wander (Z_04.asm:5054). */
static void moldorm_wander(unsigned int slot)
{
    if (slot != 5u && slot != 0x0Au)
        return;

    c_flyer_wander(slot);

    if (ENEMY_MOVE_TIMER(slot) == 0u) {
        moldorm_change_flying_state(slot);
        return;
    }
    moldorm_propagate_dirs(slot);
}

/* ControlMoldormFlight (Z_04.asm:4999) — JT over Flyer_ObjFlyingState.
 *   0,1,2 → Moldorm_Chase
 *   3     → Moldorm_Wander */
static void control_moldorm_flight(unsigned int slot)
{
    const unsigned char st = ENEMY_AI_STATE(slot);
    switch ((unsigned int)(st & 0x03u)) {
    case 0u:
    case 1u:
    case 2u:
        moldorm_chase(slot);
        break;
    case 3u:
        moldorm_wander(slot);
        break;
    default:
        break;
    }
}

/* InitMoldorm (Z_04.asm:4763). */
void enrt_init_moldorm(unsigned int slot)
{
    unsigned int y;
    unsigned char rnd;

    (void)slot;

    /* Seed segments 10..1 (NES Y=9..0 with +1 indexing). */
    for (y = 1u; y <= 10u; ++y) {
        ENEMY_X(y) = 0x80u;
        ENEMY_Y(y) = 0x70u;
        ENEMY_DIR(y) = 0u;
        ENEMY_MOLDORM_BOUNCE_DIR(y) = 0u;
        ENEMY_METASTATE(y) = 0u;
        ENEMY_ALIVE_FLAG(y) = 0u;
        ENEMY_ATTR(y) = ENEMY_ATTR(1);
        ENEMY_HP(y) = ENEMY_HP(1);
        ENEMY_AIR_SPEED(y) = 0x80u;
        ENEMY_AI_STATE(y) = 0x02u;
        ENEMY_TYPE(y) = 0x41u;
    }

    /* Random head direction for slot 5. */
    rnd = (unsigned char)(RAM(0x001Du) & 0x07u);
    ENEMY_DIR(5) = Directions8[rnd];
    ENEMY_MOLDORM_OLD_DIR(5) = Directions8[rnd];

    /* Random head direction for slot $A. */
    rnd = (unsigned char)(RAM(0x0022u) & 0x07u);
    ENEMY_DIR(0x0A) = Directions8[rnd];
    ENEMY_MOLDORM_OLD_DIR(0x0A) = Directions8[rnd];

    /* Min turns = 1 so first flight call falls through to ChangeFlyingState. */
    ENEMY_TURN_TIMER(5) = 1u;
    ENEMY_TURN_TIMER(0x0A) = 1u;

    ENEMY_MAX_AIR_SPEED = 0x80u;
    ENEMY_ROOM_OBJ_COUNT = 0x08u;
}

/* UpdateMoldorm (Z_04.asm:4907). */
void enrt_update_moldorm(unsigned int slot)
{
    unsigned char saved_timer;
    unsigned char saved_dir;
    unsigned int y;

    if (ENEMY_DIR(slot) == 0u)
        return;

    /* Magic clock gates flight/movement (NES InvClock = $066C, which is
     * the same RAM cell as ENEMY_PAUSE_FLAG in this drain set). */
    if (ENEMY_PAUSE_FLAG == 0u) {
        control_moldorm_flight(slot);
        c_move_flyer(slot);
    }

    /* Draw fireball tile $44, palette row 2 (red). */
    RAM(0x0003u) = 0x02u;
    c_anim_write_sprite(0x44u, slot);

    /* Save dir + timer across collision check (NES preserves both
     * regardless of collision outcome). */
    saved_dir = ENEMY_DIR(slot);
    saved_timer = ENEMY_MOVE_TIMER(slot);
    c_check_monster_collisions(slot);
    ENEMY_MOVE_TIMER(slot) = saved_timer;
    ENEMY_DIR(slot) = saved_dir;

    /* Segment alive → done. */
    if (ENEMY_METASTATE(slot) == 0u)
        return;

    /* Dead-segment swap with tail. */
    ENEMY_HP(slot) = 0x20u;
    enrt_check_boss_hit_reaction(slot);

    /* Find tail: search up from slot 1 (chain 1) or slot 6 (chain 2)
     * for the first slot with ObjType == $41. */
    y = (slot < 6u) ? 0u : 5u;
    while (1) {
        ++y;
        if (ENEMY_TYPE(y) == 0x41u)
            break;
    }

    /* Arm a forced-death timer + copy invincibility/X/Y onto tail. */
    ENEMY_MOVE_TIMER(y) = 0x11u;
    /* NES ObjInvincibilityTimer ($4F0), not the mask at $4B2 (T-171). */
    ENEMY_HIT_REACTION(y) = ENEMY_HIT_REACTION(slot);
    ENEMY_X(y) = ENEMY_X(slot);
    ENEMY_Y(y) = ENEMY_Y(slot);

    /* If tail IS a head segment, last segment died — exit. */
    if (y == 5u || y == 0x0Au)
        return;

    /* Otherwise convert tail into dead-dummy and revive this slot. */
    ENEMY_TYPE(y) = 0x5Du;
    c_reset_obj_metastate(slot);
}
