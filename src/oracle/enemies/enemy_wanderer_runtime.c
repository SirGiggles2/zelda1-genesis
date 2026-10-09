#include "enemy_runtime_private.h"
#include "legacy_bridge.h"

/* Wallmaster scratch aliases reused by enrt_update_goriya distance calc.
 * Same definitions as in enemy_walker_runtime.c / enemy_wallmaster_runtime.c.
 */
#define WALLMASTER_MINOR_MAJOR_MIN     RAM(0x0000)
#define WALLMASTER_MAJOR_MINOR_MIN     RAM(0x0001)
#define WALLMASTER_INSTR_AXIS          RAM(0x0002)
#define WALLMASTER_INSTR_MINOR_MIN     RAM(0x0003)

/* ---- Plan C: drained from z_07 (walker_alt_dir cluster) --------------- */

unsigned int enrt_walker_alt_dir_get_opposite(void) {
    unsigned char dir = ENEMY_FRAME_FLAGS;
    if (dir & 0x0A)
        return dir >> 1;
    return (dir << 1) & 0xFF;
}

void enrt_walker_alt_dir_end_loop(void) {
    ENEMY_BLOCKED_FLAG = 0;
}

unsigned char enrt_walker_alt_dir_get_random_perpendicular(unsigned int slot) {
    unsigned char rnd = ENEMY_RNG_A(slot);
    unsigned char dir = ENEMY_DIR(slot);
    unsigned int idx = (rnd & 0x80) ? 0 : 1;
    if (dir & 0x0C) idx += 2;
    return ReverseDirections[idx];
}

/* ---- Plan C: drained from z_04 (Common Wanderer / Goriya family) ------ */

/* Forward decls so the dispatchers can reference each other. */
void enrt_wanderer_target_player(unsigned int slot);
void enrt_walker_set_input_dir_and_try_shooting_boomerang(unsigned int slot);

/* UpdateCommonWanderer — D0 = turn rate, D2 = slot.
 *   Stores turn rate at $041F+slot, dispatches to shove / pause / target-player.
 *   Mirror of wanderer_update_common in c_wanderer.c (kept in sync; this is
 *   the drained owner now that the asm body is being replaced).
 */
void enrt_update_common_wanderer(unsigned int turn_rate, unsigned int slot) {
    ENEMY_AIR_SPEED(slot) = (unsigned char)turn_rate;
    if (OBJ(0x00C0, slot) != 0) {
        c_obj_shove(slot);
        return;
    }
    if ((ENEMY_PAUSE_FLAG | ENEMY_STUN_TIMER(slot)) != 0)
        return;
    enrt_wanderer_target_player(slot);
}

/* Wanderer_TargetPlayer — turn-toward-player AI for common wanderers.
 *   - Decrement turn timer ($0478) if non-zero.
 *   - Run Walker_Move; bail if being shoved.
 *   - If speed=0 or between-grid, just push input dir = facing dir.
 *   - Else maybe pick a new facing direction toward Link based on RNG and
 *     distance heuristics, set "wants to shoot" flag, then dispatch to the
 *     boomerang/shoot path.
 */
void enrt_wanderer_target_player(unsigned int slot) {
    unsigned char d3_dir = 0;        /* chosen direction once SetDirTowardTarget runs */
    int set_dir = 0;                 /* whether we should set facing/timer/shoot */

    /* Decrement turn timer if non-zero (uses $0478 = ENEMY_BOUNCE_FLAGS cell). */
    if (ENEMY_BOUNCE_FLAGS(slot) != 0) {
        ENEMY_BOUNCE_FLAGS(slot) = (unsigned char)(ENEMY_BOUNCE_FLAGS(slot) - 1);
    }

    c_walker_move(slot);

    /* If being shoved, return (do not chain into SetInputDir). */
    if (OBJ(0x00C0, slot) != 0)
        return;

    /* If speed = 0 or sub-tile grid offset != 0, fall through to SetInputDir. */
    if (ENEMY_WALK_SPEED(slot) == 0)
        goto set_input_dir;
    if ((OBJ(NES_OBJ_GRID_OFFSET, slot) & 0x0F) != 0)
        goto set_input_dir;

    /* Sub-tile aligned: clear grid offset (the masked low nibble was 0). */
    OBJ(NES_OBJ_GRID_OFFSET, slot) = 0;

    /* Compare turn-rate ($041F) to RNG-B ($19): if turn_rate < rng, go check
     * "turn if time" branch. Also if Link state ($00AC) == $FF, go same.
     */
    {
        unsigned char turn_rate = ENEMY_AIR_SPEED(slot);
        unsigned char rng_b     = ENEMY_RNG_B(slot);
        if (turn_rate < rng_b)
            goto turn_if_time;
        if (RAM(0x00AC) == 0xFF)
            goto turn_if_time;
    }

    /* Compute |CHASE_TARGET_X - OBJ_X|. If >= 9, check vertical instead. */
    {
        unsigned char x_dist = z01_abs((unsigned char)(CHASE_TARGET_X - ENEMY_X(slot)));
        if (x_dist < 9) {
            /* Close horizontally: turn vertically toward Link. */
            goto turn_vertically;
        }
    }
    goto check_vertical_distance;

turn_vertically:
    /* NES @TurnVertically chooses DOWN on equality too: CMP/BCC selects
     * UP only for target Y < object Y, then the BNE jumps unconditionally
     * because the fallback direction is $04. */
    if (CHASE_TARGET_Y < ENEMY_Y(slot)) {
        d3_dir = 8;
    } else {
        d3_dir = 4;
    }
    set_dir = 1;
    goto set_dir_toward_target;

check_vertical_distance:
    {
        unsigned char y_dist = z01_abs((unsigned char)(CHASE_TARGET_Y - ENEMY_Y(slot)));
        if (y_dist >= 9)
            goto turn_if_time;
    }
    /* fall through to turn_horizontally */

turn_horizontally:
    /* D3 = 1 (RIGHT) if CHASE_TARGET_X >= OBJ_X, else 2 (LEFT). */
    if (CHASE_TARGET_X >= ENEMY_X(slot))
        d3_dir = 1;
    else
        d3_dir = 2;
    set_dir = 1;
    /* fall through */

set_dir_toward_target:
    if (set_dir) {
        ENEMY_DIR(slot) = d3_dir;
        ENEMY_BOUNCE_FLAGS(slot) = ENEMY_RNG_A(slot);
        ENEMY_PUSH_TIMER(slot) = 1;
    }
    goto set_input_dir;

turn_if_time:
    ENEMY_PUSH_TIMER(slot) = 0;
    if (ENEMY_BOUNCE_FLAGS(slot) != 0)
        goto set_input_dir;
    /* Pick the perpendicular direction: vertical-facing -> turn horizontally,
     * horizontal-facing -> turn vertically.
     */
    if ((ENEMY_DIR(slot) & 0x0C) != 0)
        goto turn_horizontally;
    goto turn_vertically;

set_input_dir:
    enrt_walker_set_input_dir_and_try_shooting_boomerang(slot);
}

/* UpdateGoriya — Goriya/Armos AI: move, then maybe pick a chase direction
 * and shoot a boomerang. Mostly the same shape as Wanderer_TargetPlayer but
 * uses the larger of |dx|, |dy| to gate shooting at distance < $51.
 */
void enrt_update_goriya(unsigned int slot) {
    /* Armos (type $1E) skips the "delaying after shoot" early-out.
     * NOTE: the transpiled asm reads ($0350,A4,D2.W) — i.e. RAM[0x0350+slot]
     * — not the canonical ENEMY_TYPE at 0x034F+slot. Preserve this exact
     * indexing to keep parity with the asm callers; if it turns out to be
     * an off-by-one bug it should be fixed in the transpiler, not here.
     */
    if (OBJ(0x0350, slot) != 0x1E) {
        if ((ENEMY_STATE_TIMER(slot) & 0x80) != 0)
            return;  /* high bit set: monster is in shoot-delay state */
    }

    c_walker_move(slot);

    if (OBJ(0x00C0, slot) != 0)
        return;  /* being shoved */

    /* AfterMove: */
    if (ENEMY_WALK_SPEED(slot) == 0) {
        enrt_walker_set_input_dir_and_try_shooting_boomerang(slot);
        return;
    }
    if ((OBJ(NES_OBJ_GRID_OFFSET, slot) & 0x0F) != 0) {
        enrt_walker_set_input_dir_and_try_shooting_boomerang(slot);
        return;
    }
    OBJ(NES_OBJ_GRID_OFFSET, slot) = 0;
    if (RAM(0x00AC) == 0xFF) {
        enrt_walker_set_input_dir_and_try_shooting_boomerang(slot);
        return;
    }

    /* Build (|dy|, vert_dir) at scratch [0]/[2], (|dx|, horiz_dir) at [1]/[3].
     * vert_dir defaults to 4 (DOWN) — flipped to 8 (UP) if Link is above.
     * horiz_dir defaults to 1 (RIGHT) — flipped to 2 (LEFT) if Link is left.
     */
    {
        unsigned char link_y = CHASE_TARGET_Y;
        unsigned char obj_y  = ENEMY_Y(slot);
        unsigned char vdir   = 4;
        unsigned char a, b;
        if (link_y >= obj_y) {
            a = link_y; b = obj_y;
        } else {
            a = obj_y;  b = link_y;
            vdir <<= 1;  /* 4 -> 8 (UP) */
        }
        RAM(0x000E) = b;     /* NES STY $0E (subtrahend), T-171 */
        WALLMASTER_INSTR_AXIS = vdir;
        WALLMASTER_MINOR_MAJOR_MIN = (unsigned char)(a - b);  /* |dy| */
    }
    {
        unsigned char link_x = CHASE_TARGET_X;
        unsigned char obj_x  = ENEMY_X(slot);
        unsigned char hdir   = 1;
        unsigned char a, b;
        if (link_x >= obj_x) {
            a = link_x; b = obj_x;
        } else {
            a = obj_x;  b = link_x;
            hdir <<= 1;  /* 1 -> 2 (LEFT) */
        }
        RAM(0x000E) = b;
        WALLMASTER_INSTR_MINOR_MIN = hdir;
        WALLMASTER_MAJOR_MINOR_MIN = (unsigned char)(a - b);  /* |dx| */
    }

    /* Pick the larger distance: index 0 if |dy| >= |dx|, else 1. */
    {
        unsigned int idx = (WALLMASTER_MINOR_MAJOR_MIN >= WALLMASTER_MAJOR_MINOR_MIN) ? 0u : 1u;

        ENEMY_PUSH_TIMER(slot) = 0;

        /* If chosen distance < $51, set "wants to shoot" and face that way. */
        if (RAM(0x0000 + idx) < 0x51) {
            ENEMY_PUSH_TIMER(slot) = (unsigned char)(ENEMY_PUSH_TIMER(slot) + 1);
            ENEMY_DIR(slot) = RAM(0x0002 + idx);
        }
    }

    enrt_walker_set_input_dir_and_try_shooting_boomerang(slot);
}

/* L_Walker_SetInputDirAndTryShootingBoomerang —
 *   Always: input direction ($03F8) := facing direction ($0098).
 *   Goriya only: maybe spawn a boomerang shot tracked by both monster and shot.
 */
void enrt_walker_set_input_dir_and_try_shooting_boomerang(unsigned int slot) {
    /* Set input direction to facing direction. */
    ENEMY_PUSH_DIR_SCRATCH(slot) = ENEMY_DIR(slot);

    {
        unsigned char shot_type = 92;          /* boomerang object type ($5C) */
        unsigned char goriya_kind = ENEMY_TYPE(slot);

        if (goriya_kind != 0x05) {             /* not blue goriya */
            if (goriya_kind != 0x06)           /* not red goriya either */
                return;
            /* Red goriya: only shoots when RNG-A == $23 or $77. */
            {
                unsigned char rng = ENEMY_RNG_A(slot);
                if (rng != 0x23 && rng != 0x77)
                    return;
            }
            /* Reset shot type (dropped through CheckTimerToShootBoomerang). */
            shot_type = 92;
        }

        /* CheckTimerToShoot: if object move-timer ($28) != 0, return. */
        if (ENEMY_MOVE_TIMER(slot) != 0)
            return;

        /* Stash type for downstream Shoot path. */
        ENEMY_SHOT_TYPE_SCRATCH = shot_type;

        /* If frozen by clock or stunned, return. */
        if ((ENEMY_PAUSE_FLAG | ENEMY_STUN_TIMER(slot)) != 0)
            return;

        /* Try to shoot. Return if it failed. */
        {
            unsigned int result = c_shoot_if_wanted((unsigned int)shot_type, slot);
            if ((result & CARRY_SET) == 0)
                return;
            unsigned int shot_slot = result & 0xFFu;

            /* Monster: enter shoot-delay state ($80), clear "wants to shoot". */
            ENEMY_STATE_TIMER(slot) = 0x80;
            ENEMY_PUSH_TIMER(slot) = 0;

            /* Cross-link: shot tracks monster, monster tracks shot. */
            ENEMY_TURN_TIMER(shot_slot) = (unsigned char)slot;
            ENEMY_TURN_TIMER(slot)      = (unsigned char)shot_slot;

            /* Shot setup: flying state, q-speed $A0, $51 px range. */
            ENEMY_STATE_TIMER(shot_slot) = 0x10;
            ENEMY_WALK_SPEED(shot_slot)  = 0xA0;
            ENEMY_BOSS_HP_PHASE(shot_slot) = 0x51;
            ENEMY_METASTATE(shot_slot)   = 0;
            ENEMY_ANIM_TIMER(shot_slot)  = 3;

            /* Monster waits up to $3F frames before shooting again. */
            ENEMY_MOVE_TIMER(slot) = (unsigned char)(ENEMY_RNG_A(slot) & 0x3F);
        }
    }
}
