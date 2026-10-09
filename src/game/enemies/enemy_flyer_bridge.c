/* enemy_flyer_bridge.c -- Phase 7 Task 7.3 step 3 flyer UPDATE
 * primitives bridge.
 *
 * Per Task 7.2 step 4/5 walker bridge precedent + Drain Rule D1
 * (drain primary, NES asm secondary tiebreaker):
 *
 *   - Directions8                       — NATIVE const drain (NES
 *                                         Z_04.asm:11540).
 *   - c_move_flyer                      — forwards to enrt_move_flyer
 *                                         (already drained at
 *                                         enemy_flyer_runtime.c:175).
 *   - c_control_keese_flight            — NATIVE state-table dispatch
 *                                         (NES Z_04.asm:1201). 6-row
 *                                         ControlKeeseFlight_JumpTable;
 *                                         rows 0/1/4/5 forward to
 *                                         already-drained leaves; rows
 *                                         2/3 (Flyer_Chase /
 *                                         Flyer_Wander) NATIVE drain
 *                                         from NES Z_04.asm:11707 / 11844.
 *   - c_reset_shove_info                — NATIVE 2-cell zero (NES
 *                                         Z_07.asm:2333).
 *   - c_draw_object_mirrored_with_frame — forwards to native
 *                                         draw_object_mirrored_with_frame
 *                                         (world/draw_dispatch.c:421).
 *
 * Hard rule WT-5: lives at src/game/enemies/, not engine/.
 *
 * Primitives consumed by the keese chain (enrt_update_keese,
 * enrt_init_blue_keese, enrt_init_red_or_black_keese in
 * src/oracle/enemies/enemy_flyer_runtime.c). With these resolved the
 * Task 7.3 step 3 dispatch rows for $1B/$1C/$1D can wire without
 * pulling unresolved symbols past --gc-sections.
 */

#include <stdint.h>
#include "platform_abi.h"               /* RAM, OBJ */
#include "enemy_state.h"                /* ENEMY_*, CHASE_TARGET_X/Y aliases */
#include "world/draw_dispatch.h"        /* draw_object_mirrored_with_frame */
#include "core/core_dispatch.h"         /* core_reset_obj_metastate_and_timer */

/* Drained leaves in src/oracle/enemies/enemy_flyer_runtime.c. */
extern void enrt_move_flyer(unsigned int slot);
extern void enrt_flyer_speed_up(unsigned int slot);
extern void enrt_flyer_slow_down(unsigned int slot);
extern void enrt_flyer_fairy_decide_state(unsigned int slot);
extern void enrt_flyer_do_nothing(void);
extern void enrt_flyer_keese_decide_state(unsigned int slot);
extern void enrt_flyer_peahat_decide_state(unsigned int slot);   /* step 6 */
extern void enrt_flyer_ghini_decide_state(unsigned int slot);    /* 7.4 step 6a */
extern void enrt_flyer_delay(unsigned int slot);
extern void enrt_end_init_flyer(unsigned int slot);              /* step 6 */
extern void enrt_draw_ghini_and_check_collisions(unsigned int slot); /* walker_runtime */

/* Step 6 cross-bridge primitives (all walker_bridge / projectile_bridge). */
extern void c_obj_shove(unsigned int slot);
extern void c_check_link_collision(unsigned int slot);
extern void c_check_monster_collisions(unsigned int slot);
extern unsigned char z07_anim_fetch_obj_pos(unsigned int slot);
static void flyer_wander(unsigned int slot);

/* z04_* / z07_*: c_shims.asm xrefs that forward to transpiled NES bodies.
 * Zelda.md does not link the transpiled bank, so these symbols must
 * resolve via native equivalents. The drained C twins are
 * enrt_*-prefixed; re-export them under the z*_* names the oracle TU
 * calls. */
void z04_flyer_set_flying_state(unsigned int val, unsigned int slot);
void z04_flyer_compare_max_speed(unsigned char speed, unsigned int slot);
void z04_flyer_set_state_and_turns(unsigned int state, unsigned int slot);
void z04_end_init_flyer(unsigned int slot);
void z07_reset_obj_metastate_and_timer(unsigned int slot);

/* NES Z_04.asm:11540 Directions8. 8-way unit-direction table.
 * Indexed 0..7 by Flyer_Chase / Flyer_Wander turn logic. Bit layout
 * follows NES ObjDir convention: $01=right, $02=left, $04=down,
 * $08=up; combined diagonals { $05, $06, $09, $0A }. */
const unsigned char Directions8[8] = {
    0x08u, 0x09u, 0x01u, 0x05u, 0x04u, 0x06u, 0x02u, 0x0Au
};

void c_move_flyer(unsigned int slot)
{
    /* enrt_move_flyer is the drained body of NES MoveFlyer
     * (Z_04.asm:11590). Forwarder isolates the c_-named ABI shim from
     * the oracle TU's enrt_-named drain so dispatch rows can pull
     * either name without symbol churn. */
    enrt_move_flyer(slot);
}

/* ControlFairyFlight (NES Z_04.asm:11521). Fairy drops use the same
 * speed-up and wander leaves as the ordinary flyer state machine, but
 * their state-1 decision is fixed to six random wander turns. */
void c_control_fairy_flight(unsigned int slot)
{
    unsigned char state = (unsigned char)ENEMY_AI_STATE(slot);
    switch (state) {
    case 0u:
        enrt_flyer_speed_up(slot);
        break;
    case 1u:
        enrt_flyer_fairy_decide_state(slot);
        break;
    case 2u:
        enrt_flyer_do_nothing();
        break;
    case 3u:
        flyer_wander(slot);
        break;
    default:
        break;
    }
}

/* NES Z_04.asm:11579 Flyer_SetFlyingState. One-line drain mirroring
 * enrt_flyer_set_flying_state (flyer_runtime.c:147). */
void z04_flyer_set_flying_state(unsigned int val, unsigned int slot)
{
    ENEMY_AI_STATE(slot) = (unsigned char)val;
}

/* NES Z_04.asm:11584 Flyer_CompareMaxSpeed. Mirrors
 * enrt_flyer_compare_max_speed (flyer_runtime.c:70). */
void z04_flyer_compare_max_speed(unsigned char speed, unsigned int slot)
{
    if (speed < (unsigned char)ENEMY_MAX_AIR_SPEED) {
        return;
    }
    z04_flyer_set_flying_state(1u, slot);
}

/* NES Z_04.asm:11228 Flyer_SetStateAndTurns. Mirrors native
 * enemy_flyer_set_state_and_turns (enemy_dispatch.c:104), but with
 * matching signature for the c_shims-style call site in
 * enrt_flyer_keese_decide_state (sets ENEMY_TURN_TIMER = 6). */
void z04_flyer_set_state_and_turns(unsigned int state, unsigned int slot)
{
    ENEMY_AI_STATE(slot)   = (unsigned char)state;
    ENEMY_TURN_TIMER(slot) = 6u;
}

/* NES EndInitFlyer (Z_04.asm body) — drained twin enrt_end_init_flyer
 * (flyer_runtime.c:112). One-line re-export under the z04_* name the
 * oracle TU's enrt_init_peahat (flyer_runtime.c:103) calls. */
void z04_end_init_flyer(unsigned int slot)
{
    enrt_end_init_flyer(slot);
}

/* NES Z_07.asm:7524 ResetObjMetastateAndTimer — drained twin
 * core_reset_obj_metastate_and_timer (core_dispatch.c:455). One-line
 * re-export for the c_shims xref name. */
void z07_reset_obj_metastate_and_timer(unsigned int slot)
{
    core_reset_obj_metastate_and_timer(slot);
}

void c_reset_shove_info(unsigned int slot)
{
    /* NES Z_07.asm:2333 ResetShoveInfo. Two-cell zero — clears
     * ObjShoveDir ($00C0) + ObjShoveDistance ($00D3). Falls through
     * to SetShoveInfoWith0 in NES; same observable. */
    ENEMY_OBJ_SHOVE_DIR(slot) = 0u;
    OBJ(0x00D3u, slot)        = 0u;
}

void c_draw_object_mirrored_with_frame(unsigned int frame, unsigned int slot)
{
    draw_object_mirrored_with_frame((unsigned char)frame, slot);
}

/* NES Z_04.asm:11883 GetObjDir8Index. Find slot's ENEMY_DIR in
 * Directions8 by scanning index 7 -> 0. NES uses BPL after DEY which
 * exits at -1 (Y=$FF) with no fallthrough store; the trailing $C8
 * (INY) sets Y back to 0 — equivalent to "default 0". */
static unsigned char flyer_get_obj_dir8_index(unsigned int slot)
{
    const unsigned char d = (unsigned char)ENEMY_DIR(slot);
    for (signed char y = 7; y >= 0; --y) {
        if (Directions8[y] == d) {
            return (unsigned char)y;
        }
    }
    return 0u;
}

/* NES Z_04.asm:11707 Flyer_Chase. Turn towards player a number of
 * times; after Flyer_ObjTurns (ENEMY_TURN_TIMER) hits 0, go to flying
 * state 1 via Flyer_SetFlyingState. Each turn delays $10 frames in
 * ObjTimer (ENEMY_MOVE_TIMER).
 *
 * TurnTowardsPlayer8 builds a target direction in [00] from
 * ChaseTargetX/Y vs ObjX/Y, then performs:
 *
 *   - LoopLeft: scan 3 indices (idx+1, idx, idx-1). On exact target
 *     match, leave ObjDir untouched (already aiming the right way).
 *   - LoopRight: scan 3 indices (idx-1, idx, idx+1). For each:
 *       * BIT (Directions8[y] & target) — must share a component.
 *       * TestDir: (Directions8[y] | target) < 7 — accept (turn).
 *         Else NextLoopRight (try next index).
 *   - Default fallback (no acceptable turn): use Directions8[idx+1].
 */
static void flyer_chase(unsigned int slot)
{
    if ((unsigned char)ENEMY_MOVE_TIMER(slot) != 0u) {
        return;
    }

    /* Decrement turn counter; on zero -> SetFlyingState1. */
    {
        unsigned char turns = (unsigned char)(ENEMY_TURN_TIMER(slot) - 1u);
        ENEMY_TURN_TIMER(slot) = turns;
        if (turns == 0u) {
            z04_flyer_set_flying_state(1u, slot);
            return;
        }
    }

    /* SetDelayAndTurn: $10-frame delay. */
    ENEMY_MOVE_TIMER(slot) = 0x10u;

    /* TurnTowardsPlayer8: build target direction.
     * ChaseTargetX/Y live at NES $0061/$0062 (ENEMY_PLAYER_OBJ_X aliases
     * are $0070/$0084, NOT chase target — distinct cells). Use raw RAM
     * read so behavior mirrors NES exactly. */
    unsigned char target = 0u;
    {
        unsigned char lx = (unsigned char)RAM(0x0061u);
        unsigned char ox = (unsigned char)ENEMY_X(slot);
        if (lx > ox) {
            target = 1u;            /* right */
        } else if (lx < ox) {
            target = 2u;            /* left */
        }
    }
    {
        unsigned char ly = (unsigned char)RAM(0x0062u);
        unsigned char oy = (unsigned char)ENEMY_Y(slot);
        if (ly != oy) {
            /* NES: BCS branch -> Y stays 1 -> down ($04); fall-through
             * INY -> Y=2 -> up ($08). After two ASLs Y becomes Y*4. */
            unsigned char vbits = (ly < oy) ? 0x08u : 0x04u;
            target = (unsigned char)(target | vbits);
        }
    }

    const unsigned int idx = (unsigned int)flyer_get_obj_dir8_index(slot);

    /* LoopLeft: 3 iterations from y=idx+1, decrementing. Exact-match
     * exit means current ObjDir already aims at target — return as-is. */
    {
        unsigned int y = (idx + 1u) & 7u;
        for (int n = 0; n < 3; ++n) {
            if (Directions8[y] == target) {
                return;
            }
            y = (y - 1u) & 7u;
        }
    }

    /* LoopRight: 3 iterations from y=idx-1, incrementing. */
    {
        unsigned int y = (idx + 7u) & 7u;       /* idx-1 mod 8 */
        for (int n = 0; n < 3; ++n) {
            const unsigned char dir = Directions8[y];
            if ((dir & target) != 0u) {
                /* TestDir: (dir | target) < 7 -> accept this index. */
                if ((unsigned int)(dir | target) < 7u) {
                    ENEMY_DIR(slot) = dir;
                    return;
                }
                /* else fall through to NextLoopRight (try next). */
            }
            y = (y + 1u) & 7u;
        }
    }

    /* Default fallback — NES DEY after 3 INYs from idx-1 lands at
     * idx+1. SetDir8ForIndex stores Directions8[idx+1] into ObjDir. */
    {
        const unsigned int chosen = (idx + 1u) & 7u;
        ENEMY_DIR(slot) = Directions8[chosen];
    }
}

/* NES Z_04.asm:11844 Flyer_Wander. Same delay/turn-counter shape as
 * Flyer_Chase, but new direction is randomly turned left/right/none
 * via Random+1, X (ENEMY_RNG_B). */
static void flyer_wander(unsigned int slot)
{
    if ((unsigned char)ENEMY_MOVE_TIMER(slot) != 0u) {
        return;
    }

    {
        unsigned char turns = (unsigned char)(ENEMY_TURN_TIMER(slot) - 1u);
        ENEMY_TURN_TIMER(slot) = turns;
        if (turns == 0u) {
            z04_flyer_set_flying_state(1u, slot);
            return;
        }
    }

    ENEMY_MOVE_TIMER(slot) = 0x10u;

    /* TurnRandomlyDir8: idx-relative turn keyed off RNG. */
    unsigned int idx = (unsigned int)flyer_get_obj_dir8_index(slot);
    const unsigned char rnd = (unsigned char)ENEMY_RNG_B(slot);

    if (rnd >= 0xA0u) {
        /* don't turn */
    } else if (rnd >= 0x50u) {
        idx = (idx + 1u) & 7u;       /* turn right */
    } else {
        idx = (idx + 7u) & 7u;       /* turn left (idx-1 mod 8) */
    }
    ENEMY_DIR(slot) = Directions8[idx];
}

/* NES Z_04.asm:11724 TurnTowardsPlayer8 (parameterless form).
 * Phase 7 Task 7.4 step 2a — boulder UPDATE (boss_runtime.c:152) +
 * manhandla calls call this with no slot arg, using NES CurObjIndex
 * (X reg) implicitly. We mirror the same algorithm flyer_chase uses
 * (lines 190-251) but sourced off ENEMY_THROWER_SLOT ($0340 = NES
 * CurObjIndex), which enemy_loop_tick now writes per-slot before
 * dispatch. Stance: EXTEND. */
void c_turn_towards_player8(void)
{
    const unsigned int slot = (unsigned int)ENEMY_THROWER_SLOT;
    unsigned char target = 0u;
    {
        unsigned char lx = (unsigned char)RAM(0x0061u);
        unsigned char ox = (unsigned char)ENEMY_X(slot);
        if (lx > ox) {
            target = 1u;
        } else if (lx < ox) {
            target = 2u;
        }
    }
    {
        unsigned char ly = (unsigned char)RAM(0x0062u);
        unsigned char oy = (unsigned char)ENEMY_Y(slot);
        if (ly != oy) {
            unsigned char vbits = (ly < oy) ? 0x08u : 0x04u;
            target = (unsigned char)(target | vbits);
        }
    }

    const unsigned int idx = (unsigned int)flyer_get_obj_dir8_index(slot);

    {
        unsigned int y = (idx + 1u) & 7u;
        for (int n = 0; n < 3; ++n) {
            if (Directions8[y] == target) {
                return;
            }
            y = (y - 1u) & 7u;
        }
    }

    {
        unsigned int y = (idx + 7u) & 7u;
        for (int n = 0; n < 3; ++n) {
            const unsigned char dir = Directions8[y];
            if ((dir & target) != 0u) {
                if ((unsigned int)(dir | target) < 7u) {
                    ENEMY_DIR(slot) = dir;
                    return;
                }
            }
            y = (y + 1u) & 7u;
        }
    }

    {
        const unsigned int chosen = (idx + 1u) & 7u;
        ENEMY_DIR(slot) = Directions8[chosen];
    }
}

/* NES Z_04.asm:1201 ControlKeeseFlight. 6-row jump table:
 *   0: Flyer_SpeedUp           (drained: enrt_flyer_speed_up)
 *   1: Flyer_KeeseDecideState  (drained: enrt_flyer_keese_decide_state)
 *   2: Flyer_Chase             (NATIVE: this file)
 *   3: Flyer_Wander            (NATIVE: this file)
 *   4: Flyer_SlowDown          (drained: enrt_flyer_slow_down)
 *   5: Flyer_Delay             (drained: enrt_flyer_delay)
 *
 * NES TableJump out-of-range = undefined. Default branch is no-op
 * (matches transpile's safe-table default). */
void c_control_keese_flight(unsigned int slot)
{
    const unsigned char state = (unsigned char)ENEMY_AI_STATE(slot);
    switch (state) {
    case 0u: enrt_flyer_speed_up(slot);          break;
    case 1u: enrt_flyer_keese_decide_state(slot); break;
    case 2u: flyer_chase(slot);                  break;
    case 3u: flyer_wander(slot);                 break;
    case 4u: enrt_flyer_slow_down(slot);         break;
    case 5u: enrt_flyer_delay(slot);             break;
    default: break;
    }
}

/* Phase 8 Task 8.9 — expose Flyer_Chase / Flyer_Wander to other bridges.
 * NES Moldorm_Chase / Moldorm_Wander (Z_04.asm:5008/5054) call Flyer_Chase /
 * Flyer_Wander directly without going through the keese state table.
 * Same body, different caller. */
void c_flyer_chase(unsigned int slot)  { flyer_chase(slot); }
void c_flyer_wander(unsigned int slot) { flyer_wander(slot); }

/* NES Z_04.asm:4054 ControlPeahatFlight. Identical 6-row dispatch shape
 * as ControlKeeseFlight; only state 1 differs (PeahatDecideState vs
 * KeeseDecideState — see enrt_flyer_peahat_decide_state in
 * enemy_flyer_runtime.c:88, decision thresholds shifted up to RNG_A
 * (vs RNG_B for keese) with $B0/$20 cutoffs vs $A0/$20). */
void c_control_peahat_flight(unsigned int slot)
{
    const unsigned char state = (unsigned char)ENEMY_AI_STATE(slot);
    switch (state) {
    case 0u: enrt_flyer_speed_up(slot);           break;
    case 1u: enrt_flyer_peahat_decide_state(slot); break;
    case 2u: flyer_chase(slot);                   break;
    case 3u: flyer_wander(slot);                  break;
    case 4u: enrt_flyer_slow_down(slot);          break;
    case 5u: enrt_flyer_delay(slot);              break;
    default: break;
    }
}

/* NES Z_04.asm:4014 UpdatePeahat. Phase 7 Task 7.3 step 6 native drain.
 * Stance: ADOPT — verbatim transcription of NES body, composing
 * already-drained primitives.
 *
 * NES sequence:
 *   1. If ObjShoveDir != 0 -> Obj_Shove, then DrawAndCheckCollisions.
 *   2. Else if InvClock | ObjStunTimer != 0 -> DrawAndCheckCollisions.
 *   3. Else ControlPeahatFlight + MoveFlyer.
 *   4. DrawAndCheckCollisions:
 *        Anim_FetchObjPosForSpriteDescriptor.
 *        frame = Flyer_ObjDistTraveled & 1.
 *        DrawObjectMirrored.
 *        if Flyer_ObjFlyingState == 5 -> CheckMonsterCollisions
 *        else                            CheckLinkCollision.
 *
 * Cell-mapping notes:
 *   - InvClock == ENEMY_PAUSE_FLAG ($066C). NES drains use this name.
 *   - ObjStunTimer == ENEMY_STUN_TIMER ($003D).
 *   - Flyer_ObjDistTraveled lives at $0437 in NES; this codebase aliases
 *     the same cell as ENEMY_FLAP_PHASE. Shared enrt_move_flyer now
 *     increments it after a whole-pixel move and calls BoundFlyer,
 *     matching the NES @End block. */
void enrt_update_peahat(unsigned int slot)
{
    if ((unsigned char)ENEMY_OBJ_SHOVE_DIR(slot) != 0u) {
        c_obj_shove(slot);
    } else if ((((unsigned char)ENEMY_PAUSE_FLAG)
              | (unsigned char)ENEMY_STUN_TIMER(slot)) == 0u) {
        c_control_peahat_flight(slot);
        c_move_flyer(slot);
    }

    /* DrawAndCheckCollisions tail. */
    (void)z07_anim_fetch_obj_pos(slot);
    {
        const unsigned int frame = (unsigned int)(ENEMY_FLAP_PHASE(slot) & 1u);
        c_draw_object_mirrored_with_frame(frame, slot);
    }
    if ((unsigned char)ENEMY_AI_STATE(slot) == 5u) {
        c_check_monster_collisions(slot);
    } else {
        c_check_link_collision(slot);
    }
}

/* NES Z_04.asm:3984 ControlFlyingGhiniFlight. 6-row jump table.
 * Identical shape to ControlKeeseFlight / ControlPeahatFlight; state-1
 * decision swaps in Flyer_GhiniDecideState (RNG_A thresholds $A0/$08
 * vs keese RNG_B $A0/$20 vs peahat RNG_A $B0/$20). */
void c_control_flying_ghini_flight(unsigned int slot)
{
    const unsigned char state = (unsigned char)ENEMY_AI_STATE(slot);
    switch (state) {
    case 0u: enrt_flyer_speed_up(slot);            break;
    case 1u: enrt_flyer_ghini_decide_state(slot);  break;
    case 2u: flyer_chase(slot);                    break;
    case 3u: flyer_wander(slot);                   break;
    case 4u: enrt_flyer_slow_down(slot);           break;
    case 5u: enrt_flyer_delay(slot);               break;
    default: break;
    }
}

/* NES Z_04.asm:3967 UpdateFlyingGhini. Phase 7 Task 7.4 step 6a native
 * drain. Stance: ADOPT — verbatim transcription.
 *
 * NES sequence:
 *   1. If InvClock == 0: ControlFlyingGhiniFlight + MoveFlyer.
 *   2. Anim_FetchObjPosForSpriteDescriptor.
 *   3. (LDA Flyer_ObjDistTraveled & 1 — value computed but immediately
 *      clobbered by DrawGhini's own JSR Anim_FetchObjPos; preserved as
 *      no-op for parity with NES asm flow.)
 *   4. JMP DrawGhiniAndCheckCollisions.
 *
 * InvClock == ENEMY_PAUSE_FLAG ($066C). DrawGhiniAndCheckCollisions
 * drained at walker_runtime.c:295 — handles Anim_FetchObjPos + dir-based
 * frame select + check_link_collision. */
void enrt_update_flying_ghini(unsigned int slot)
{
    if ((unsigned char)ENEMY_PAUSE_FLAG == 0u) {
        c_control_flying_ghini_flight(slot);
        c_move_flyer(slot);
    }
    enrt_draw_ghini_and_check_collisions(slot);
}

/* --------------------------------------------------------------- */
/* T-050 pond fairy ($2F).                                          */
/* NES source: Z_04.asm UpdatePondFairy, PondFairy_HandleOtherStates, */
/*   PondFairy_MoveHearts, PondHeartStartAngles; Z_01.asm DrawFairy. */
/* Drained C: enemy_flyer_runtime.c enrt_init_pond_fairy (init);    */
/*   Patra DecreaseObjectAngle / RotateObjectLocation (hearts).      */
/* Coverage: FULL except Link_EndMoveAndAnimate_Bank4 (Link draws   */
/*   natively; NES only redraws him in place while halted).         */
/* Stance: GREENFIELD per asm.                                      */
/* --------------------------------------------------------------- */
extern void enrt_decrease_object_angle(unsigned char low, unsigned char high,
                                       unsigned int slot);
extern unsigned char enrt_rotate_object_location(unsigned char cosine_bits,
                                                 unsigned char sine_bits,
                                                 unsigned int slot);

#define PF_WORLD_IS_FILLING_HEARTS RAM(0x0063u)
extern void roomrom_main_link_end_move_from_object(void);   /* engine main.c */
#define PF_LINK_STATE              RAM(0x00ACu)
#define PF_LINK_X                  RAM(0x0070u)
#define PF_LINK_Y                  RAM(0x0084u)
#define PF_INPUT_DIR               RAM(0x03F8u)
#define PF_STATE(s)                RAM(0x00ACu + (s))
#define PF_TIMER(s)                RAM(0x0028u + (s))
#define PF_X(s)                    RAM(0x0070u + (s))
#define PF_Y(s)                    RAM(0x0084u + (s))
#define PF_DIR(s)                  RAM(0x0098u + (s))

static const unsigned char k_pond_heart_start_angles[7] = {
    0x14u, 0x10u, 0x0Cu, 0x08u, 0x04u, 0x00u, 0x1Cu
};


/* PondFairy_MoveHearts: hearts ride object slots 2..9. */
static void pond_fairy_move_hearts(unsigned int fairy)
{
    unsigned int x;
    for (x = 2u; x < 0x0Au; ++x) {
        if ((unsigned char)PF_STATE(x) == 0u) {
            if (x != 2u) {
                if ((unsigned char)PF_STATE(2u) == 0u) continue;
                if ((unsigned char)ENEMY_OBJ_ANGLE_WHOLE(2u) !=
                    k_pond_heart_start_angles[x - 3u]) continue;
            }
            PF_STATE(x) = (uint8_t)((unsigned char)PF_STATE(x) + 1u);
            PF_DIR(x) = 0x80u;
            ENEMY_OBJ_ANGLE_WHOLE(x) = 0x18u;         /* N */
            /* LDA ObjX+1 / ObjY+1: the fairy's slot is fixed at 1. */
            PF_X(x) = PF_X(1u);
            PF_Y(x) = (uint8_t)((unsigned char)PF_Y(1u) - 0x1Cu);
        }
        enrt_decrease_object_angle(0x60u, 0x00u, x);
        PF_Y(x) = enrt_rotate_object_location(0x06u, 0x06u, x);
        RAM(0x0004u) = 0x02u;                         /* red row */
        RAM(0x0005u) = 0x02u;
        RAM(0x0000u) = PF_X(x);                       /* Anim_FetchObjPos */
        RAM(0x0001u) = PF_Y(x);
        RAM(0x000Fu) = 0u;
        draw_object_not_mirrored(0u, fairy);          /* LDX CurObjIndex */
    }
}

void enrt_update_pond_fairy(unsigned int slot)
{
    unsigned char st;
    draw_fairy(slot);              /* DrawFairy */
    st = (unsigned char)PF_STATE(1u);
    if (st == 0u) {
        /* Link at the pond edge: Y $AD, X in [$70, $81). */
        if ((unsigned char)PF_LINK_Y != 0xADu) return;
        if ((unsigned char)PF_LINK_X < 0x70u) return;
        if ((unsigned char)PF_LINK_X >= 0x81u) return;
        PF_STATE(1u) = (uint8_t)(st + 1u);
        PF_LINK_STATE = 0x40u;
        PF_WORLD_IS_FILLING_HEARTS = 0x40u;
        return;
    }
    if (st == 1u) {
        if (!PF_WORLD_IS_FILLING_HEARTS) {
            PF_STATE(1u) = 2u;
            PF_TIMER(1u) = 0x50u;
            return;                    /* A = $50 fails CMP #$02 */
        }
    } else if (st == 2u) {
        if ((unsigned char)PF_TIMER(1u) == 0u) {
            PF_STATE(1u) = 3u;
            PF_LINK_STATE = 0u;
        }
    } else {
        return;
    }
    /* @DrawLinkAndHearts: input dir 0, Link_EndMoveAndAnimate_Bank4 with
     * ObjState 0 (restored after), then the hearts (Z_04.asm:3431, T-171). */
    PF_INPUT_DIR = 0u;
    {
        const unsigned char link_state = (unsigned char)PF_LINK_STATE;
        PF_LINK_STATE = 0u;
        roomrom_main_link_end_move_from_object();
        PF_LINK_STATE = link_state;
    }
    pond_fairy_move_hearts(slot);
}
