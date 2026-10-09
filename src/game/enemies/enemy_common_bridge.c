/* enemy_common_bridge.c -- Phase 7 Task 7.3 step 4 zol/gel UPDATE
 * primitives bridge.
 *
 * Resolves the 4 c_* primitives consumed by enrt_update_zol /
 * enrt_update_gel in src/oracle/enemies/enemy_walker_runtime.c:
 *
 *   c_update_zol_state       -> enrt_update_zol_state
 *   c_zol_check_collisions   -> enrt_zol_check_collisions
 *   c_gel_move               -> enrt_gel_move
 *   c_gel_check_collisions   -> enrt_gel_check_collisions
 *
 * All four drained twins live in src/oracle/enemies/enemy_common_runtime.c
 * (already linked into Debug.md per build_debug.py:164). Stance: ADOPT
 * (Drain Rule D1) — drained C is PRIMARY evidence, forwarders are
 * one-line trampolines.
 *
 * One transitive primitive surfaces from enrt_update_zol_state -> state
 * 2 split path (enrt_create_child_gel calls c_shoot_limited): drained
 * natively here (REPLACE; was c_shims.asm forward to transpiled bank
 * which is NOT linked). Native body composed verbatim from NES
 * Z_04.asm:11369 ShootLimited + Z_04.asm:11395 Shoot. Mirrors the
 * existing c_shoot_if_wanted body in enemy_walker_bridge.c with the
 * ObjWantsToShoot gate stripped (ShootLimited has no such gate; type
 * read from caller's OBJ(NES_OBJ_TYPE, slot) cell instead of from
 * NES scratch [$00] / arg).
 *
 * Hard rule WT-5: lives at src/game/enemies/, not RoomRom/.
 */

#include "platform_abi.h"             /* RAM, OBJ, NES_OBJ_TYPE, CARRY_SET */
#include "roomrom_enemy_state.h"      /* ENEMY_* macros (re-export of state/enemy_state.h) */

/* Forward declarations of drained twins. Bodies in
 * src/oracle/enemies/enemy_common_runtime.c. Compiled into
 * oracle_enemy_common.o. */
extern void enrt_update_zol_state(unsigned int slot);
extern void enrt_zol_check_collisions(unsigned int slot);
extern void enrt_gel_move(unsigned int slot);
extern void enrt_gel_check_collisions(unsigned int slot);

void c_update_zol_state(unsigned int slot)
{
    enrt_update_zol_state(slot);
}

void c_zol_check_collisions(unsigned int slot)
{
    enrt_zol_check_collisions(slot);
}

void c_gel_move(unsigned int slot)
{
    enrt_gel_move(slot);
}

void c_gel_check_collisions(unsigned int slot)
{
    enrt_gel_check_collisions(slot);
}

/* NES Z_04.asm:11369 ShootLimited + :11395 Shoot. Phase 7 Task 7.3
 * step 4. Stance: REPLACE — c_shims.asm forwarder routes to transpiled
 * z_04 bank which is NOT linked into Debug.md.
 *
 * NES sequence (no ObjWantsToShoot gate, unlike _ShootIfWanted):
 *   1. FindEmptyMonsterSlot — scan Y=$0B downto $01 for ObjType==0.
 *      None found -> return C=0.
 *   2. shot_type read from [00] (NES STA $00 before JSR ShootLimited).
 *      If shot_type >= $53 (true projectile, not enemy clone):
 *        if ActiveMonsterShots >= 4 -> return C=0.
 *        else INC ActiveMonsterShots.
 *   3. SetTypeAndClearObject(shot_type, empty).
 *   4. Shoot block: ObjState[empty] = $10, ObjTimer[empty] = 0,
 *      Dir/X/Y[empty] = Dir/X/Y[caller_slot].
 *   5. Return C=1, slot=empty.
 *
 * Caller note (enrt_create_child_gel @ enemy_common_runtime.c:111):
 * sets [00] = $14 (Child Gel) before calling, then reads
 * back the new child slot from the low byte of the return value to
 * patch in inherited grid offset + opposing direction. */
unsigned int c_shoot_limited(unsigned int slot)
{
    unsigned int empty = 0u;
    {
        unsigned int y = 0x0Bu;
        for (;;) {
            /* FindEmptyMonsterSlot: STY EmptyMonsterSlot ($59, T-171:
             * t129 t4829 red wizzrobe wave). */
            if (ENEMY_TYPE(y) == 0u) { empty = y; ENEMY_NEXT_SHOT_SLOT = (unsigned char)y; break; }
            if (y == 0x01u) break;
            y--;
        }
    }
    if (empty == 0u) return 0u;

    /* NES ShootLimited reads the shot type from [00] (T-147: read the
     * caller's ObjType, so a red wizzrobe cloned itself, t129 t4829). */
    unsigned char shot_type = (unsigned char)RAM(0x0000u);
    if (shot_type >= 0x53u) {
        if (ENEMY_SHOT_COUNT >= 0x04u) return 0u;
        ENEMY_SHOT_COUNT = (unsigned char)(ENEMY_SHOT_COUNT + 1u);
    }

    /* SetTypeAndClearObject + DestroyObject_WRAM compositional clear.
     * Mirrors clear_slot_scratch in enemy_loop.c. */
    ENEMY_TYPE(empty)              = shot_type;
    ENEMY_OBJ_SHOVE_DIR(empty)     = 0u;
    OBJ(0x00D3u, empty)            = 0u;  /* ObjShoveDistance */
    ENEMY_MOVE_TIMER(empty)        = 0u;  /* ObjTimer ($0028) */
    ENEMY_STATE_TIMER(empty)       = 0u;  /* ObjState ($00AC) */
    ENEMY_HIT_REACTION(empty)      = 0u;  /* ObjInvincibilityTimer ($04F0) */
    ENEMY_METASTATE(empty)         = 0x01u;
    /* DestroyObject_WRAM: uninitialized; its first update runs
     * InitObject (T-012: the shot waits a frame, NES). */
    ENEMY_ALIVE_FLAG(empty)        = 0xFFu;

    /* Shoot block: state $10, copy dir/x/y from caller slot. */
    ENEMY_STATE_TIMER(empty) = 0x10u;
    ENEMY_MOVE_TIMER(empty)  = 0u;
    ENEMY_DIR(empty)         = (unsigned char)ENEMY_DIR(slot);
    ENEMY_X(empty)           = (unsigned char)ENEMY_X(slot);
    ENEMY_Y(empty)           = (unsigned char)ENEMY_Y(slot);

    return CARRY_SET | empty;
}
