/* enemy_boss_bridge.c -- Phase 7 Task 7.3 step 7 vire UPDATE primitives
 * bridge.
 *
 * Resolves the 5 c_* / z04_* primitives consumed by enrt_update_vire
 * (entry point) + transitively by enrt_update_vire_state /
 * enrt_check_vire_collisions / enrt_draw_vire in
 * src/oracle/enemies/enemy_boss_runtime.c (linked Task 7.3 step 7):
 *
 *   c_gel_move_splitting       -> enrt_gel_move_splitting
 *                                 (enemy_common_runtime.c:226)
 *   z04_update_common_wanderer -> enrt_update_common_wanderer
 *                                 (enemy_wanderer_runtime.c:44)
 *   c_anim_advance_and_fetch   -> sprite_anim_advance_and_fetch
 *                                 (sprite_dispatch.c:105) — same back-end
 *                                 used by z07_anim_advance_and_fetch in
 *                                 walker_bridge; vire calls the c_-named
 *                                 entry directly.
 *   c_find_empty_monster_slot  -> enrt_find_empty_monster_slot
 *                                 (enemy_runtime.c:12) — also stashes
 *                                 ENEMY_NEXT_SHOT_SLOT for c_shoot.
 *   c_shoot                    -> writes ENEMY_SHOT_TYPE_SCRATCH (= ZP_TMP0
 *                                 = ENEMY_VIRE_SPLIT_TYPE) then forwards
 *                                 to enrt_shoot (enemy_boss_runtime.c:434).
 *
 * Stance: EXTEND. All callees are drained C (PRIMARY evidence per
 * Drain Rule D1). No NES asm linkage. No transpiled-bank fallback.
 *
 * Hard rule WT-5: lives at src/game/enemies/, not RoomRom/.
 *
 * NOTE — ENEMY_THROWER_SLOT ($0340) is read by enrt_shoot but not
 * written by any current call-site in the vire chain. NES Shoot uses
 * CurObjIndex (the X register) implicitly. Our enemy_loop_tick does
 * not yet write ENEMY_THROWER_SLOT per-slot. Step 7 lands the link
 * surface; step 8+ probe will validate runtime correctness and add
 * the dispatcher write if needed.
 */

#include "platform_abi.h"             /* RAM, OBJ, NES_OBJ_TYPE, CARRY_SET */
#include "roomrom_enemy_state.h"      /* ENEMY_SHOT_TYPE_SCRATCH macros */
#include "enemy_state.h"              /* ENEMY_X/Y/DIR/RNG_B/INVINCIBILITY/MOVE_TIMER/BOUNCE_FLAGS */
#include "progress_state.h"           /* FRAME_COUNTER */
#include "world/sprite_dispatch.h"    /* sprite_anim_advance_and_fetch */
#include "world/draw_dispatch.h"      /* draw_write_boss_sprite */
#include "core/core_dispatch.h"       /* core_set_type_and_clear_object */

/* Forward declarations of drained twins. Bodies in src/oracle/enemies/. */
extern unsigned int enrt_gel_move_splitting(unsigned int slot);
extern void         enrt_update_common_wanderer(unsigned int turn_rate,
                                                unsigned int slot);
extern unsigned int enrt_shoot(void);
extern unsigned int enrt_shoot_fireball_55(unsigned int source_slot);

/* z07_set_type_and_clear_object — required by enrt_shoot
 * (enemy_boss_runtime.c:437). Drained native twin lives in
 * core_dispatch.c:490 as core_set_type_and_clear_object. Same body
 * NES Z_07.asm SetTypeAndClearObject — sets ObjType[slot]=type then
 * zeroes scratch fields. */
void z07_set_type_and_clear_object(unsigned int type, unsigned int slot)
{
    core_set_type_and_clear_object(type, slot);
}

unsigned int c_gel_move_splitting(unsigned int slot)
{
    return enrt_gel_move_splitting(slot);
}

void z04_update_common_wanderer(unsigned int turn_rate, unsigned int slot)
{
    enrt_update_common_wanderer(turn_rate, slot);
}

void c_anim_advance_and_fetch(unsigned int val, unsigned int slot)
{
    sprite_anim_advance_and_fetch(val, slot);
}

/* c_find_empty_monster_slot — native body. enemy_runtime.c (which
 * carries enrt_find_empty_monster_slot) is NOT linked into Debug.md
 * (would pull enrt_animate_and_draw_common_object + its full chain).
 * Body verbatim from enemy_runtime.c:12 — scan slots 11..1 for
 * ObjType==0, stash into ENEMY_NEXT_SHOT_SLOT, return slot index. */
unsigned char c_find_empty_monster_slot(void)
{
    signed char i;
    for (i = 11; i >= 1; i--) {
        if (OBJ(NES_OBJ_TYPE, (unsigned char)i) == 0u) {
            ENEMY_NEXT_SHOT_SLOT = (unsigned char)i;
            return (unsigned char)i;
        }
    }
    return 0u;
}

unsigned int c_shoot(unsigned int type)
{
    /* ENEMY_SHOT_TYPE_SCRATCH = ZP_TMP0 — same cell vire's call-site
     * already populated via ENEMY_VIRE_SPLIT_TYPE (also ZP_TMP0).
     * Writing again is idempotent and forward-compatible with future
     * c_shoot consumers that don't pre-populate. */
    ENEMY_SHOT_TYPE_SCRATCH = (unsigned char)type;
    return enrt_shoot();
}

/* ---------------------------------------------------------------------- */
/* Phase 7 Task 7.4 step 10 — Aquamentus family bridge.                   */
/*                                                                        */
/* NES sources:                                                           */
/*   Aquamentus_Move  Z_04.asm:5612 (~70 lines).                          */
/*   Aquamentus_Shoot Z_04.asm:5684 (~60 lines).                          */
/*   Aquamentus_Draw  Z_04.asm:5764 (~80 lines, 6-sprite render).         */
/*   AquamentusSpeeds Z_04.asm:5609 -> {$01, $FF}.                        */
/*   AquamentusTiles  Z_04.asm:5754 -> 12 tiles, 6 per anim frame.        */
/*   AquamentusSpriteOffsetsX/Y Z_04.asm:5758/5761 -> 6 entries each.     */
/*                                                                        */
/* Stance per Drain Rule D1: EXTEND. The drained C consumer is            */
/*   enrt_init_aquamentus  @ enemy_boss_runtime.c:102                     */
/*   enrt_update_aquamentus @ enemy_boss_runtime.c:108                    */
/* which calls c_aquamentus_{move,shoot,draw} as primitives. NES bodies   */
/* live in legacy bank Z_04 (NOT linked into Debug.md via c_shims.asm),   */
/* so native bodies must be carried here. Every primitive is a direct    */
/* per-line translation of the NES asm — no logic divergence. Verified   */
/* per-line vs reference/aldonunez/Z_04.asm.                             */
/* ---------------------------------------------------------------------- */

/* AquamentusSpeeds[2] (Z_04.asm:5609). Indexed by (ObjDir - 1):
 *   Dir=1 (right) -> +1, Dir=2 (left) -> -1 (0xFF as unsigned byte). */
static const unsigned char k_aquamentus_speeds[2] = { 0x01u, 0xFFu };

/* AquamentusTiles[12] (Z_04.asm:5754). Two anim frames of 6 tiles each.
 * Frame 0 = indices 0..5 (last index 5). Frame 1 = indices 6..B (last B).
 * Tile 0 ($CC) = closed-mouth face; substitute $C0 (open mouth) when
 * ObjTimer < $20 (about-to-shoot indicator). */
static const unsigned char k_aquamentus_tiles[12] = {
    0xCCu, 0xC4u, 0xC8u, 0xC2u, 0xC6u, 0xCAu,
    0xCCu, 0xC4u, 0xC8u, 0xCEu, 0xD0u, 0xD2u
};

/* AquamentusSpriteOffsetsY[6] (Z_04.asm:5758).
 * Top row: 0,0,0; bottom row: $10,$10,$10. */
static const unsigned char k_aquamentus_sprite_offsets_y[6] = {
    0x00u, 0x00u, 0x00u, 0x10u, 0x10u, 0x10u
};

/* AquamentusSpriteOffsetsX[6] (Z_04.asm:5761).
 * 3 columns: 0, 8, $10 (mirrored top/bottom). */
static const unsigned char k_aquamentus_sprite_offsets_x[6] = {
    0x00u, 0x08u, 0x10u, 0x00u, 0x08u, 0x10u
};

/* NES ObjGridOffset = $0394 (per-slot distance-remaining counter).
 * Aquamentus reuses it as its move-distance-remaining counter. */
#define BOSS_OBJ_GRID_OFFSET(slot) OBJ(0x0394u, (slot))

void c_aquamentus_move(unsigned int slot)
{
    /* Z_04.asm:5612 Aquamentus_Move. */
    if (BOSS_OBJ_GRID_OFFSET(slot) == 0u) {
        /* New random distance: (Random[X] & $0F) | $07 → 7 or 15. */
        /* NES LDA Random,X = Random+slot (T-013: read Random+1+slot;
         * t013_route t7796 distance NES $0F, Genesis $07). */
        const unsigned char rng = (unsigned char)ENEMY_RNG_A(slot);
        BOSS_OBJ_GRID_OFFSET(slot) = (unsigned char)((rng & 0x0Fu) | 0x07u);
        /* Random direction: (rng & $01) + 1 → Dir=1 (right) or Dir=2 (left). */
        ENEMY_DIR(slot) = (unsigned char)((rng & 0x01u) + 1u);
        return;
    }
    /* Move once every 8 screen frames. */
    if ((((unsigned char)FRAME_COUNTER) & 0x07u) != 0u) {
        return;
    }
    const unsigned char x = (unsigned char)ENEMY_X(slot);
    if (x < 0x88u) {
        /* Crossed left limit — clamp + face right + reset distance. */
        ENEMY_X(slot) = 0x88u;
        ENEMY_DIR(slot) = 0x01u;
        BOSS_OBJ_GRID_OFFSET(slot) = 0x07u;
    } else if (x >= 0xC8u) {
        /* Crossed right limit — clamp + face left + reset distance. */
        ENEMY_X(slot) = 0xC7u;
        ENEMY_DIR(slot) = 0x02u;
        BOSS_OBJ_GRID_OFFSET(slot) = 0x07u;
    }
    /* Apply speed: ObjX += AquamentusSpeeds[Dir-1]; distance--. */
    const unsigned char dir = (unsigned char)ENEMY_DIR(slot);
    ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot)
        + k_aquamentus_speeds[(unsigned char)(dir - 1u) & 0x01u]);
    BOSS_OBJ_GRID_OFFSET(slot) = (unsigned char)(BOSS_OBJ_GRID_OFFSET(slot) - 1u);
}

void c_aquamentus_shoot(unsigned int slot)
{
    /* Z_04.asm:5684 Aquamentus_Shoot.
     *
     * If ObjTimer != 0: spread out flying fireballs.
     * Else: fire 3 fireballs (middle/lower/upper) and reseed timer. */
    if (ENEMY_MOVE_TIMER(slot) == 0u) {
        /* Reseed timer: (Random[X] | $70) — at least $70 frames. */
        /* NES LDA Random,X = Random+slot (T-013: read Random+1+slot;
         * t013_route t7747 timer NES $F3, Genesis $F8). */
        const unsigned char rng = (unsigned char)ENEMY_RNG_A(slot);
        ENEMY_MOVE_TIMER(slot) = (unsigned char)(rng | 0x70u);

        /* Middle fireball (vertical offset $00). */
        /* STA Aquamentus_ObjFireballOffset, Y with ShootFireball's Y: the
         * new slot, or 0 when none was free (the NES then writes slot 0's
         * cell; the old EmptyMonsterSlot read reused a stale slot, T-171). */
        ENEMY_BOUNCE_FLAGS(enrt_shoot_fireball_55(slot)) = 0x00u;
        /* Lower fireball (offset $01 = drift down). */
        ENEMY_BOUNCE_FLAGS(enrt_shoot_fireball_55(slot)) = 0x01u;
        /* Upper fireball (offset $FF = drift up). */
        ENEMY_BOUNCE_FLAGS(enrt_shoot_fireball_55(slot)) = 0xFFu;
        return;
    }

    /* @SpreadOutFireballs: scan slots $0B..$00 — every other frame
     * (FrameCounter bit 0 == 0), add per-fireball Y-offset to ObjY. */
    if ((((unsigned char)FRAME_COUNTER) & 0x01u) != 0u) {
        return;
    }
    signed char i;
    for (i = 0x0B; i >= 0; i--) {
        if ((unsigned char)ENEMY_TYPE((unsigned int)i) != 0x55u) continue;
        ENEMY_Y((unsigned int)i) = (unsigned char)(
            (unsigned char)ENEMY_Y((unsigned int)i)
            + (unsigned char)ENEMY_BOUNCE_FLAGS((unsigned int)i));
    }
}

void c_aquamentus_draw(unsigned int slot)
{
    /* Z_04.asm:5764 Aquamentus_Draw. 6-sprite render, two-frame anim. */
    /* Frame select: every $10 screen frames switch tiles.
     * Y = $05 if (FrameCounter & $10) != 0 else $0B. */
    unsigned char tile_idx;
    if ((((unsigned char)FRAME_COUNTER) & 0x10u) != 0u) {
        tile_idx = 0x05u;
    } else {
        tile_idx = 0x0Bu;
    }
    /* Per-frame attr: ((InvincibilityTimer & 3) ^ 3). When timer=0 this
     * is constant 3 (palette row 7 = level palette); when temporarily
     * invincible it cycles palette rows for a hit-flash. */
    const unsigned char attr = (unsigned char)(
        ((unsigned char)ENEMY_HIT_REACTION(slot) & 0x03u) ^ 0x03u);

    /* Loop sprite_idx = 5..0 inclusive. */
    /* NES scratch kept as on the NES (T-171): [0A] tile index, [0B]
     * sprite index (both end one below their last value), [00]/[01]/[03]
     * the last sprite's X/Y/attributes. */
    signed char sprite_idx;
    for (sprite_idx = 5; sprite_idx >= 0; sprite_idx--) {
        const unsigned char sx = (unsigned char)(
            (unsigned char)ENEMY_X(slot)
            + k_aquamentus_sprite_offsets_x[(unsigned char)sprite_idx]);
        const unsigned char sy = (unsigned char)(
            (unsigned char)ENEMY_Y(slot)
            + k_aquamentus_sprite_offsets_y[(unsigned char)sprite_idx]);
        RAM(0x0000u) = sx;
        RAM(0x0001u) = sy;
        RAM(0x0003u) = attr;

        unsigned char tile = k_aquamentus_tiles[tile_idx];
        if (tile == k_aquamentus_tiles[0]) {
            /* Face tile — substitute $C0 (open mouth) if timer < $20. */
            if ((unsigned char)ENEMY_MOVE_TIMER(slot) < 0x20u) {
                tile = 0xC0u;
            }
        }
        draw_write_boss_sprite(tile, sx, sy, attr);
        tile_idx = (unsigned char)(tile_idx - 1u);
        RAM(0x000Au) = tile_idx;
        RAM(0x000Bu) = (unsigned char)(sprite_idx - 1);
    }
}

/* c_shoot_fireball — c_shims.asm legacy-bank trampoline. NES uses
 * (dir, slot) but that's actually (type, source_slot) via a register
 * naming aliasing in the shim. Native body lives at
 * enrt_shoot_fireball (enemy_projectile_runtime.c:67). */
void c_shoot_fireball(unsigned int dir, unsigned int slot)
{
    extern unsigned int enrt_shoot_fireball(unsigned int type, unsigned int source_slot);
    enrt_shoot_fireball(dir, slot);
}
