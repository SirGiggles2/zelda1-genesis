/* enemy_jumper_bridge.c -- Phase 7 Task 7.4 step 2a jumper/projectile
 * primitives bridge.
 *
 * Resolves the primitives consumed by the boulder UPDATE chain
 * (enrt_update_tektite_or_boulder + enrt_init_tektite +
 * enrt_update_boulder_set + enrt_shoot_fireball + transitively
 * enrt_bound_flyer) once $1F BoulderSet INIT/UPDATE and $20 Boulder
 * INIT/UPDATE wire into enemy_loop dispatch:
 *
 *   TektiteStartingDirs           — DATA drain. NES Z_04.asm:1832
 *                                   ($01 $02 $05 $0A). Indexed by
 *                                   enrt_init_tektite via RNG_B & 3.
 *   c_bound_flyer                 — forwards to enrt_bound_flyer
 *                                   (enemy_flyer_runtime.c:205).
 *   c_bound_direction_horizontally — NATIVE drain of NES Z_01.asm:3312
 *                                   BoundDirectionHorizontally.
 *   c_bound_direction_vertically  — NATIVE drain of NES Z_01.asm:3382
 *                                   BoundDirectionVertically.
 *   c_reverse_obj_dir8            — NATIVE drain of NES Z_04.asm:11664
 *                                   ReverseObjDir8, including Moldorm's
 *                                   deferred bounce for head slots 5/10.
 *   z07_find_empty_monster_slot   — NATIVE body (slots 11..1 scan, return
 *                                   first ObjType==0 slot, else 0).
 *                                   Mirrors enemy_boss_bridge.c:79
 *                                   c_find_empty_monster_slot but with
 *                                   unsigned-int + z07_ naming used by
 *                                   projectile_runtime / trap_runtime.
 *
 * Stance: EXTEND. All callees are drained C (Drain Rule D1 PRIMARY) or
 * NES data tables transcribed verbatim. No NES asm linkage. No
 * transpiled-bank fallback.
 *
 * Hard rule WT-5: lives at src/game/enemies/, not RoomRom/.
 */

#include "platform_abi.h"             /* RAM, OBJ, NES_OBJ_TYPE */
#include "enemy_state.h"              /* ENEMY_NEXT_SHOT_SLOT, ENEMY_FRAME_FLAGS */
#include "room_state.h"               /* ROOM_BOUNDS */
#include "world/draw_dispatch.h"      /* draw_object_mirrored */
#include "world/sprite_dispatch.h"    /* sprite_anim_advance_and_fetch */
#include "combat/link_collision_dispatch.h" /* link_collision_check_monster_collisions */

/* Drained twin in src/oracle/enemies/enemy_flyer_runtime.c:205. */
extern void enrt_bound_flyer(unsigned int slot);
extern void enrt_defer_bounce(unsigned int slot, unsigned int dir_idx);

/* NES Z_04.asm:11540 Directions8 — already drained in flyer_bridge as
 * `const unsigned char Directions8[8]`. Reuse via extern. */
extern const unsigned char Directions8[8];

/* NES Z_04.asm:1832 TektiteStartingDirs.
 * .BYTE $01, $02, $05, $0A
 *
 * Read by enrt_init_tektite (boss_runtime.c:121) — RNG_B & 3 indexes
 * into this table; chosen value seeds ObjDir + ObjMoveTimer (dir << 2).
 */
const unsigned char TektiteStartingDirs[4] = {
    0x01u, 0x02u, 0x05u, 0x0Au
};

/* NES Z_04.asm BoundFlyer wrapper. Drained body in
 * enemy_flyer_runtime.c:205 — handles screen-edge wrap for flyer Y/X
 * cells. Boulder UPDATE (boss_runtime.c:181) and manhandla UPDATE
 * (manhandla_runtime.c:199) both call this c_-named entry. */
void c_bound_flyer(unsigned int slot)
{
    enrt_bound_flyer(slot);
}

/* z07_find_empty_monster_slot — NATIVE body (gen/z_07.c is not linked
 * into Debug.md). Body matches enemy_runtime.c:12 enrt_find_empty_monster_slot
 * + the inline c_find_empty_monster_slot (enemy_boss_bridge.c:79):
 *
 *   for slot in 11..1:
 *     if ObjType[slot] == 0:
 *       ENEMY_NEXT_SHOT_SLOT = slot
 *       return slot
 *   return 0
 *
 * Returns unsigned int per legacy_bridge.h:77. Distinct from
 * c_find_empty_monster_slot (unsigned char) in boss_bridge — both bodies
 * are functionally identical; the type difference exists because
 * z07_find_empty_monster_slot was the gen-c carrier (returning int) and
 * c_find_empty_monster_slot was the c_shims.asm trampoline (returning
 * byte via D0 low). Each call-site picks the right name. */
unsigned int z07_find_empty_monster_slot(void)
{
    signed char i;
    for (i = 11; i >= 1; --i) {
        if (OBJ(NES_OBJ_TYPE, (unsigned char)i) == 0u) {
            ENEMY_NEXT_SHOT_SLOT = (unsigned char)i;
            return (unsigned int)(unsigned char)i;
        }
    }
    return 0u;
}

/* NES Z_01.asm:3312 BoundDirectionHorizontally.
 *
 * Inputs:  X = slot, [0F] = candidate direction (= ENEMY_FRAME_FLAGS).
 * Outputs: Y = direction-component crossed (0/1/2 = none/right/left);
 *          [0F] cleared if a boundary was crossed.
 *
 * Behavior:
 *   x = ObjX[slot]
 *   if slot != 0 and (slot >= $0D or ObjType[slot] == $5C boomerang)
 *       x += $0B                       ; weapon-side adjustment
 *   if x < ROOM_BOUNDS(0):              ; left bound
 *       Y := 2; [0F] &= 0; return       ; via BoundDirectionReturn
 *   if slot != 0 and (slot >= $0D or ObjType[slot] == $5C boomerang)
 *       x -= $17                       ; right-bound adjustment
 *   if x < ROOM_BOUNDS(1):              ; right bound (BCC -> in-bounds)
 *       Y := 1; return (no clear)
 *   else: Y := 1; [0F] := [0F] & Y; return  (cleared because Y != 0)
 *
 * Boulder ($20) and tektite ($1B?) live in slots 1..11 and have type
 * != $5C, so the conditional adds never fire — but we drain the full
 * conditional verbatim per Drain Rule D1.
 */
void c_bound_direction_horizontally(unsigned int slot)
{
    unsigned char x = (unsigned char)ENEMY_X(slot);
    unsigned char y_reg = 2u;        /* default: left-bound direction */
    if (slot != 0u) {
        const unsigned char t = (unsigned char)ENEMY_TYPE(slot);
        if ((slot >= 0x0Du) || (t == 0x5Cu)) {
            x = (unsigned char)(x + 0x0Bu);
        }
    }
    if (x < (unsigned char)ROOM_BOUNDS(0)) {
        /* left-bound crossed — Y already 2; clear [0F] if [0F] & Y != 0. */
        if (((unsigned char)ENEMY_FRAME_FLAGS & y_reg) != 0u) {
            ENEMY_FRAME_FLAGS = 0u;
        }
        return;
    }
    if (slot != 0u) {
        const unsigned char t = (unsigned char)ENEMY_TYPE(slot);
        if ((slot >= 0x0Du) || (t == 0x5Cu)) {
            x = (unsigned char)(x - 0x17u);
        }
    }
    y_reg = 1u;                       /* right-bound direction */
    if (x < (unsigned char)ROOM_BOUNDS(1)) {
        /* in-bounds — leave [0F] alone. */
        return;
    }
    /* right-bound crossed. */
    if (((unsigned char)ENEMY_FRAME_FLAGS & y_reg) != 0u) {
        ENEMY_FRAME_FLAGS = 0u;
    }
}

/* NES Z_01.asm:3382 BoundDirectionVertically. Same shape as the
 * horizontal version but on Y axis with $0F/$21 fudge constants and
 * Y-direction values $08 (up) / $04 (down). */
void c_bound_direction_vertically(unsigned int slot)
{
    unsigned char y = (unsigned char)ENEMY_Y(slot);
    unsigned char y_reg = 8u;        /* default: up-bound direction */
    if (slot != 0u) {
        const unsigned char t = (unsigned char)ENEMY_TYPE(slot);
        if ((slot >= 0x0Du) || (t == 0x5Cu)) {
            y = (unsigned char)(y + 0x0Fu);
        }
    }
    if (y < (unsigned char)ROOM_BOUNDS(2)) {
        if (((unsigned char)ENEMY_FRAME_FLAGS & y_reg) != 0u) {
            ENEMY_FRAME_FLAGS = 0u;
        }
        return;
    }
    if (slot != 0u) {
        const unsigned char t = (unsigned char)ENEMY_TYPE(slot);
        if ((slot >= 0x0Du) || (t == 0x5Cu)) {
            y = (unsigned char)(y - 0x21u);
        }
    }
    y_reg = 4u;                       /* down-bound direction */
    if (y >= (unsigned char)ROOM_BOUNDS(3)) {
        /* NES BCS BoundDirectionReturn — Y >= bound -> reset. */
        if (((unsigned char)ENEMY_FRAME_FLAGS & y_reg) != 0u) {
            ENEMY_FRAME_FLAGS = 0u;
        }
    }
}

/* NES Z_04.asm:11883 GetObjDir8Index — find ObjDir in Directions8 by
 * scanning index 7 -> 0. Local copy because flyer_bridge's same helper
 * is static there. */
static unsigned char jumper_get_obj_dir8_index(unsigned int slot)
{
    const unsigned char d = (unsigned char)ENEMY_DIR(slot);
    for (signed char y = 7; y >= 0; --y) {
        if (Directions8[y] == d) {
            return (unsigned char)y;
        }
    }
    return 0u;
}

/* NES Z_04.asm:11664 ReverseObjDir8.
 *   idx := GetObjDir8Index(slot)
 *   idx := (idx + 4) & 7        ; opposite direction
 *   if ObjType[slot] != $41 (moldorm) then
 *       ObjDir[slot] := Directions8[idx]
 *
 * Moldorm heads defer the opposite direction until their movement
 * chain consumes it; NES DeferBounce only stores for slots 5/10. */
void c_reverse_obj_dir8(unsigned int slot)
{
    unsigned int idx = (unsigned int)jumper_get_obj_dir8_index(slot);
    idx = (idx + 4u) & 7u;
    if ((unsigned char)ENEMY_TYPE(slot) != 0x41u) {
        ENEMY_DIR(slot) = Directions8[idx];
    } else {
        enrt_defer_bounce(slot, idx);
    }
}

/* ----------------------------------------------------------------- *
 * Phase 7 Task 7.4 step 2b — UpdateBurrower native drain chain.
 *
 * NES sources:
 *   Z_04.asm:2603 UpdateBurrower
 *   Z_04.asm:2661 Burrower_AnimateDrawAndCheckCollisions
 *   Z_04.asm:2590 BlueLeeverStateQSpeeds  ($08 $0A $10 $20 $10 $0A)
 *   Z_04.asm:2593 BlueLeeverStateTimes    ($80 $20 $0F $FF $10 $60)
 *   Z_04.asm:2596 BlueLeeverStateAnimTimes($10 $0B $01 $05 $01 $0B)
 *
 * Body-shared across $0F BlueLeever / $10 RedLeever / $11 Zora. Wired
 * here for $11 Zora UPDATE (enrt_update_zora -> c_update_burrower).
 * Other types use it indirectly via UpdateBlueLeever/UpdateRedLeever,
 * not yet wired.
 *
 * RAM cell mapping (NES Variables.inc + state/enemy_state.h):
 *   ObjTimer            ($0028) -> ENEMY_MOVE_TIMER
 *   ObjState            ($00AC) -> ENEMY_STATE_TIMER  (NB: name-misleading)
 *   ObjY (slot)         ($0084) -> ENEMY_Y(slot)
 *   ObjY (no idx)       ($0084) -> ENEMY_Y(0) (player Y)
 *   ObjDir              ($0098) -> ENEMY_DIR
 *   ObjType             ($034F) -> ENEMY_TYPE
 *   ObjQSpeedFrac       ($03BC) -> ENEMY_WALK_SPEED
 *   ObjAnimFrame        ($03E4) -> ENEMY_DRAW_FRAME
 *   ObjMetastate        ($0405) -> ENEMY_METASTATE
 *   ActiveRedLeeverCount($0510) -> RAM(0x0510)
 *
 * Composes already-linked native primitives:
 *   sprite_anim_advance_and_fetch         -> sprite_dispatch.c:105
 *   draw_object_mirrored                  -> draw_dispatch.c:404
 *   link_collision_check_monster_collisions -> link_collision_dispatch.c:263
 *
 * Stance: ADOPT (translation of NES asm verbatim).
 */

static const unsigned char BlueLeeverStateQSpeeds[6] = {
    0x08u, 0x0Au, 0x10u, 0x20u, 0x10u, 0x0Au
};
static const unsigned char BlueLeeverStateTimes[6] = {
    0x80u, 0x20u, 0x0Fu, 0xFFu, 0x10u, 0x60u
};
static const unsigned char BlueLeeverStateAnimTimes[6] = {
    0x10u, 0x0Bu, 0x01u, 0x05u, 0x01u, 0x0Bu
};

void c_update_burrower(unsigned int slot)
{
    unsigned char state;
    unsigned char anim_rollover_val;
    unsigned char type;
    unsigned char frame_for_draw;

    /* @CycleState gate — when ObjTimer is non-zero, skip cycling and go
     * straight to @Animate. NES BNE @Animate. */
    if ((unsigned char)ENEMY_MOVE_TIMER(slot) == 0u) {
        type = (unsigned char)ENEMY_TYPE(slot);
        /* Zora state-1 special: pick front (2) / back (3) frame index
         * by comparing zora-Y to player-Y. NES stores result in ObjDir. */
        if (type == 0x11u && (unsigned char)ENEMY_STATE_TIMER(slot) == 1u) {
            unsigned char frame_idx = 3u;             /* default = back */
            if ((unsigned char)ENEMY_Y(slot) < (unsigned char)ENEMY_Y(0)) {
                frame_idx = 2u;                       /* front */
            }
            ENEMY_DIR(slot) = frame_idx;
        }
        /* Cycle state mod 6, seed speed + timer for new state. */
        state = (unsigned char)((unsigned char)ENEMY_STATE_TIMER(slot) + 1u);
        if (state >= 6u) state = 0u;
        ENEMY_STATE_TIMER(slot) = state;
        ENEMY_WALK_SPEED(slot)  = BlueLeeverStateQSpeeds[state];
        ENEMY_MOVE_TIMER(slot)  = BlueLeeverStateTimes[state];
    }

    /* @Animate: A := BlueLeeverStateAnimTimes[state], JSR Anim_Adv... */
    state = (unsigned char)ENEMY_STATE_TIMER(slot);
    anim_rollover_val = BlueLeeverStateAnimTimes[state];
    sprite_anim_advance_and_fetch((unsigned int)anim_rollover_val, slot);

    /* Re-load state (NES LDA ObjState, X / BEQ @Exit). */
    state = (unsigned char)ENEMY_STATE_TIMER(slot);
    if (state == 0u) return;

    type = (unsigned char)ENEMY_TYPE(slot);
    /* Zora state 2..4: frame index lives in ObjDir (set in state-1 above). */
    if (type == 0x11u && state >= 2u && state < 5u) {
        frame_for_draw = (unsigned char)ENEMY_DIR(slot);
    } else {
        /* @CalcFrameImage: A = ((state - 1) * 2) + ObjAnimFrame. */
        frame_for_draw = (unsigned char)(((unsigned int)(state - 1u) << 1)
                                          + (unsigned char)ENEMY_DRAW_FRAME(slot));
    }
    draw_object_mirrored(frame_for_draw, slot);

    /* Collision gating:
     *   non-zora: collisions only if state == 3
     *   zora: collisions if state in {2, 3, 4}
     */
    {
        unsigned char do_collisions = 0u;
        if (type == 0x11u) {
            if (state == 2u || state == 4u) do_collisions = 1u;
        }
        if (!do_collisions && state == 3u) do_collisions = 1u;
        if (!do_collisions) return;

        link_collision_check_monster_collisions(slot);
        if ((unsigned char)ENEMY_METASTATE(slot) == 0u) return;
        /* Dying — DEC ActiveRedLeeverCount only when type is RedLeever ($10). */
        if (type == 0x10u) {
            unsigned char rlc = (unsigned char)RAM(0x0510);
            RAM(0x0510) = (unsigned char)(rlc - 1u);
        }
    }
}

/* ----------------------------------------------------------------- *
 * Phase 7 Task 7.6 step 3 — UpdateRedLeever bridge ($10).
 *
 * NES `Z_04.asm:2737-2961` UpdateRedLeever (~225 lines):
 *   State 0:
 *     - If RedLeeverLongTimer != 0 -> exit.
 *     - If ActiveRedLeeverCount >= 2 -> exit.
 *     - Face Link's direction.
 *     - If Random+1[slot] >= $C0 -> ReverseObjDir (face away from Link).
 *     - If facing vertically -> place at Link's X, Y offset $28/$D8,
 *       sanitize Y = (sum & $F0) | $0D, gate on Y >= $5D.
 *     - Else place horizontally at Link's Y (gate Y >= $5D), X +=
 *       $28/$D8, AND $F8, gate |dx| < $30.
 *     - GetCollidableTileStill < ObjectFirstUnwalkableTile required.
 *     - INC ActiveRedLeeverCount, ObjAnimCounter=1, RedLeeverLongTimer=2,
 *       RedLeever_CycleStateDrawAndCheckCollisions, ReverseObjDir.
 *
 *   Other states (CheckOtherStates):
 *     - If state != 3 -> AnimateIfTime.
 *     - State 3 + ObjShoveDir != 0 -> Obj_Shove + AnimateAndCheckCollisions.
 *     - State 3 + (InvClock | ObjStunTimer) -> AnimateAndCheckCollisions.
 *     - State 3 + GetCollidingTileMoving >= ObjectFirstUnwalkableTile or
 *       BoundByRoom == 0 -> CycleState.
 *     - Else MoveObject + grid-truncate + ObjTimer = $FF.
 *     - AnimateIfTime: if ObjTimer != 0 -> AnimateAndCheckCollisions.
 *     - Else -> CycleState (state++ mod 6, DEC count on wrap, table
 *       lookup) -> AnimateAndCheckCollisions.
 *
 *   AnimateAndCheckCollisions: A = RedLeeverStateAnimTimes[state],
 *   JMP Burrower_AnimateDrawAndCheckCollisions (shared with BlueLeever
 *   path — uses ObjState/ObjType to gate frame + collision).
 *
 * Body lives entirely in this bridge. Uses already-drained helpers:
 *   - core_reverse_obj_dir         (NES ReverseObjDir)
 *   - collision_get_collidable_tile_still
 *   - collision_get_colliding_tile_moving
 *   - object_bound_by_room
 *   - c_obj_shove, c_move_object
 *   - sprite_anim_advance_and_fetch + draw_object_mirrored +
 *     link_collision_check_monster_collisions (Burrower_AnimateDraw...)
 *
 * Stance: EXTEND. Bridge body.
 */

#include "world/object_dispatch.h"            /* object_bound_by_room */
#include "combat/collision_dispatch.h"        /* collision_get_collidable_tile_still / _moving */
#include "core/core_dispatch.h"               /* core_reverse_obj_dir */

extern void          c_obj_shove(unsigned int slot);
extern void          c_move_object(unsigned short slot);

/* NES Variables.inc:160 ActiveRedLeeverCount := $510 */
#define RED_LEEVER_LONG_TIMER         RAM(0x004D)   /* NES $4D */
#define ACTIVE_RED_LEEVER_COUNT       RAM(0x0510)   /* NES $510 */
#define OBJECT_FIRST_UNWALKABLE_TILE  RAM(0x034A)   /* NES $34A */
#define OBJ_SHOVE_DIR                 0x00C0u       /* OBJ(_, slot) base */
#define OBJ_TIMER_BASE                0x0028u       /* NES_OBJ_MOVE_TIMER */
#define OBJ_GRID_OFFSET_BASE          NES_OBJ_GRID_OFFSET  /* ObjGridOffset $394 (T-012: was $470) */

/* NES Z_04.asm:2728-2735 RedLeeverStateQSpeeds / Times / AnimTimes. */
static const unsigned char RedLeeverStateQSpeeds[6] = {
    0x00u, 0x00u, 0x00u, 0x20u, 0x00u, 0x00u
};
static const unsigned char RedLeeverStateTimes[6] = {
    0x00u, 0x10u, 0x08u, 0xFFu, 0x08u, 0x10u
};
static const unsigned char RedLeeverStateAnimTimes[6] = {
    0x10u, 0x08u, 0x08u, 0x05u, 0x08u, 0x08u
};

/* Burrower_AnimateDrawAndCheckCollisions (NES Z_04.asm:2661) — shared
 * post-cycle path. Mirrors second half of c_update_burrower above but
 * parameterized on anim rollover so the RedLeever caller can pass its
 * own table value. */
static void burrower_animate_draw_and_check_collisions(
    unsigned char anim_rollover_val,
    unsigned int slot)
{
    unsigned char state;
    unsigned char type;
    unsigned char frame_for_draw;

    sprite_anim_advance_and_fetch((unsigned int)anim_rollover_val, slot);

    state = (unsigned char)ENEMY_STATE_TIMER(slot);
    if (state == 0u) return;

    type = (unsigned char)ENEMY_TYPE(slot);
    if (type == 0x11u && state >= 2u && state < 5u) {
        frame_for_draw = (unsigned char)ENEMY_DIR(slot);
    } else {
        frame_for_draw = (unsigned char)(((unsigned int)(state - 1u) << 1)
                                          + (unsigned char)ENEMY_DRAW_FRAME(slot));
    }
    draw_object_mirrored(frame_for_draw, slot);

    {
        unsigned char do_collisions = 0u;
        if (type == 0x11u) {
            if (state == 2u || state == 4u) do_collisions = 1u;
        }
        if (!do_collisions && state == 3u) do_collisions = 1u;
        if (!do_collisions) return;

        link_collision_check_monster_collisions(slot);
        if ((unsigned char)ENEMY_METASTATE(slot) == 0u) return;
        if (type == 0x10u) {
            ACTIVE_RED_LEEVER_COUNT = (unsigned char)(ACTIVE_RED_LEEVER_COUNT - 1u);
        }
    }
}

/* RedLeever_AnimateAndCheckCollisions (NES Z_04.asm:2958). */
static void red_leever_animate_and_check_collisions(unsigned int slot)
{
    unsigned char state = (unsigned char)ENEMY_STATE_TIMER(slot);
    burrower_animate_draw_and_check_collisions(
        RedLeeverStateAnimTimes[state], slot);
}

/* RedLeever_CycleStateDrawAndCheckCollisions (NES Z_04.asm:2935). */
static void red_leever_cycle_state_draw_and_check_collisions(unsigned int slot)
{
    unsigned char state = (unsigned char)ENEMY_STATE_TIMER(slot);
    state = (unsigned char)(state + 1u);
    if (state >= 6u) {
        ACTIVE_RED_LEEVER_COUNT = (unsigned char)(ACTIVE_RED_LEEVER_COUNT - 1u);
        state = 0u;
    }
    ENEMY_STATE_TIMER(slot) = state;
    ENEMY_WALK_SPEED(slot)  = RedLeeverStateQSpeeds[state];
    ENEMY_MOVE_TIMER(slot)  = RedLeeverStateTimes[state];
    red_leever_animate_and_check_collisions(slot);
}

void enrt_update_red_leever(unsigned int slot)
{
    /* If state != 0 -> jump @CheckOtherStates. */
    if ((unsigned char)ENEMY_STATE_TIMER(slot) != 0u) {
        goto check_other_states;
    }

    /* State 0 — spawn-from-Link gate. */
    if ((unsigned char)RED_LEEVER_LONG_TIMER != 0u) return;
    if ((unsigned char)ACTIVE_RED_LEEVER_COUNT >= 2u) return;

    /* Face Link's dir; randomly reverse to face Link. */
    ENEMY_DIR(slot) = (unsigned char)ENEMY_DIR(0);
    if ((unsigned char)ENEMY_RNG_B(slot) >= 0xC0u) {
        core_reverse_obj_dir(slot);
    }

    /* Branch on vertical/horizontal facing. */
    {
        unsigned char dir = (unsigned char)ENEMY_DIR(slot);
        if ((dir & 0x0Cu) != 0u) {
            /* Vertical: place at Link's X, offset Y. */
            unsigned char offset;
            unsigned char y_sum;
            ENEMY_X(slot) = (unsigned char)ENEMY_X(0);
            offset = ((dir & 0x08u) != 0u) ? 0xD8u : 0x28u;
            y_sum = (unsigned char)((unsigned char)ENEMY_Y(0) + offset);
            y_sum = (unsigned char)((y_sum & 0xF0u) | 0x0Du);
            ENEMY_Y(slot) = y_sum;
            if (y_sum < 0x5Du) return;
            /* fall through to @CheckSafeToSpawn */
        } else {
            /* Horizontal: place at Link's Y, offset X. */
            unsigned char y_link = (unsigned char)ENEMY_Y(0);
            unsigned char x_link;
            unsigned char offset;
            unsigned char x_new;
            unsigned char a, b;

            ENEMY_Y(slot) = y_link;
            if (y_link < 0x5Du) return;

            offset = ((dir & 0x02u) != 0u) ? 0xD8u : 0x28u;
            x_link = (unsigned char)ENEMY_X(0);
            x_new = (unsigned char)((x_link + offset) & 0xF8u);
            ENEMY_X(slot) = x_new;

            /* Compute |x_new - x_link| as distance gate. NES PHA/PLA
             * swap pattern -> distance = max - min. */
            if (x_new >= x_link) {
                a = x_new;       /* $02 = larger */
                b = x_link;      /* $01 = smaller */
            } else {
                a = x_link;
                b = x_new;
            }
            /* NES @Subtract: A = $01 - $02 with carry pre-set; we
             * computed |dx| = a - b directly. */
            if ((unsigned char)(a - b) >= 0x30u) return;
        }
    }

    /* @CheckSafeToSpawn — tile must be walkable. */
    if (collision_get_collidable_tile_still(slot)
        >= (unsigned char)OBJECT_FIRST_UNWALKABLE_TILE) {
        return;
    }
    ACTIVE_RED_LEEVER_COUNT = (unsigned char)(ACTIVE_RED_LEEVER_COUNT + 1u);
    ENEMY_ANIM_TIMER(slot)  = 1u;
    /* NES: LDA #$01 / ASL -> A=$02 / STA RedLeeverLongTimer. */
    RED_LEEVER_LONG_TIMER   = 2u;
    red_leever_cycle_state_draw_and_check_collisions(slot);
    core_reverse_obj_dir(slot);
    /* NES falls through to @CheckOtherStates: state 1 animates again
     * this tick (T-171: t054_uw_block42_nes t480, AnimCounter 7 not 8). */

check_other_states:
    {
        unsigned char state = (unsigned char)ENEMY_STATE_TIMER(slot);

        /* If not state 3 -> @AnimateIfTime. */
        if (state != 3u) goto animate_if_time;

        /* State 3 — shove handling. */
        if ((unsigned char)OBJ(OBJ_SHOVE_DIR, slot) != 0u) {
            c_obj_shove(slot);
            red_leever_animate_and_check_collisions(slot);
            return;
        }
        /* @CheckStunned. */
        if ((unsigned char)ENEMY_PAUSE_FLAG | (unsigned char)ENEMY_STUN_TIMER(slot)) {
            red_leever_animate_and_check_collisions(slot);
            return;
        }
        /* Tile-still + room-boundary gates. NES writes ObjDir to [$0F]
         * (LINK_MOVING_DIR scratch) before GetCollidingTileMoving. */
        RAM(NES_LINK_MOVING_DIR) = (unsigned char)ENEMY_DIR(slot);
        if (collision_get_colliding_tile_moving(slot)
            >= (unsigned char)OBJECT_FIRST_UNWALKABLE_TILE) {
            red_leever_cycle_state_draw_and_check_collisions(slot);
            return;
        }
        if (object_bound_by_room(slot) == 0u) {
            red_leever_cycle_state_draw_and_check_collisions(slot);
            return;
        }
        /* Move + grid-offset truncate + timer = $FF. */
        c_move_object((unsigned short)slot);
        {
            unsigned char go = (unsigned char)OBJ(OBJ_GRID_OFFSET_BASE, slot);
            unsigned char masked = (unsigned char)(go & 0x0Fu);
            if (masked == 0u) {
                OBJ(OBJ_GRID_OFFSET_BASE, slot) = 0u;
            }
        }
        ENEMY_MOVE_TIMER(slot) = 0xFFu;
        /* fall through to @AnimateIfTime */
    }

animate_if_time:
    if ((unsigned char)ENEMY_MOVE_TIMER(slot) != 0u) {
        red_leever_animate_and_check_collisions(slot);
        return;
    }
    red_leever_cycle_state_draw_and_check_collisions(slot);
}

/* ----------------------------------------------------------------- *
 * Phase 7 Task 7.6 step 2 — UpdateBlueLeever bridge ($0F).
 *
 * NES `Z_04.asm:2599-2647` UpdateBlueLeever:
 *   LDA #$A0                       ; turn rate
 *   STA ObjTurnRate, X             ; $041F+slot = ENEMY_AIR_SPEED
 *   JSR Wanderer_TargetPlayer      ; turn-toward-Link AI
 *   ; fall through to UpdateBurrower (this file: c_update_burrower)
 *
 * Wanderer_TargetPlayer drained at
 *   src/oracle/enemies/enemy_wanderer_runtime.c:63 (enrt_wanderer_target_player).
 * UpdateBurrower drained natively above (c_update_burrower).
 *
 * Stance: EXTEND. Three-line bridge body — single AIR_SPEED seed +
 * two drained-twin calls.
 */
extern void enrt_wanderer_target_player(unsigned int slot);

void enrt_update_blue_leever(unsigned int slot)
{
    ENEMY_AIR_SPEED(slot) = 0xA0u;
    enrt_wanderer_target_player(slot);
    c_update_burrower(slot);
}
