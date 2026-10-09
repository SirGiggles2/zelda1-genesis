/* Phase 8 Task 8.7 — Gohma callee shims.
 *
 * NES source: Z_04.asm:7814 InitGohma + Z_04.asm:8207 UpdateGohma +
 *             Z_04.asm:8392 Gohma_AnimateAndDraw +
 *             Z_04.asm:8413 Gohma_DrawLegsOneSide +
 *             Z_04.asm:8453 Gohma_CheckCollisions.
 * Drained C:  src/oracle/enemies/enemy_boss_runtime.c
 *             (enrt_init_gohma + enrt_update_gohma +
 *              enrt_gohma_set_sprite_attributes — already shipped).
 * Coverage:   FULL — UpdateGohma drained primary; this TU resolves the
 *             two c_* callees the drain references via
 *             enemy_runtime_private.h that have no native body yet:
 *               c_gohma_animate_and_draw   (NES Z_04.asm:8392)
 *               c_gohma_check_collisions   (NES Z_04.asm:8453)
 *             Both compose existing primitives:
 *               sprite_anim_fetch_obj_pos / sprite_anim_advance_and_fetch /
 *               sprite_anim_set_obj_hflip (sprite_dispatch.c) +
 *               draw_object_mirrored_with_frame /
 *               draw_object_not_mirrored_with_frame (draw_dispatch.c) +
 *               enrt_gohma_set_sprite_attributes (enemy_boss_runtime.c) +
 *               c_check_monster_collisions (enemy_walker_bridge.c).
 * Stance:     ADOPT — drained primitives consumed verbatim.
 */

#include "boss_gohma.h"
#include "platform_abi.h"
#include "enemy_state.h"
#include "world/draw_dispatch.h"          /* draw_object_*_with_frame */
#include "world/sprite_dispatch.h"        /* sprite_anim_* */

extern void enrt_gohma_set_sprite_attributes(unsigned int slot);
extern void c_check_monster_collisions(unsigned int slot);

/* GohmaLegOffsetsX (NES Z_04.asm:8384). Signed 8-bit: $F0 = -16, $10 = +16. */
static const unsigned char kGohmaLegOffsetsX[2] = { 0xF0u, 0x10u };

/* ---- Gohma_DrawLegsOneSide (NES Z_04.asm:8413) ---------------------------
 *   Y register on entry = side index (0 = left, 1 = right).
 *   SCRATCH_X = ENEMY_X(slot) + GohmaLegOffsetsX[side]
 *   SCRATCH_Y = ENEMY_Y(slot)
 *   set sprite attrs, set HFlip = ObjAnimFrame
 *   frame = ((side + ObjAnimFrame) & 1) + 4   ; leg frame images 4 / 5
 *   DrawObjectNotMirrored(frame, slot)
 */
static void gohma_draw_legs_one_side(unsigned int side, unsigned int slot)
{
    ENEMY_SCRATCH_X = (unsigned char)(ENEMY_X(slot) + kGohmaLegOffsetsX[side]);
    ENEMY_SCRATCH_Y = ENEMY_Y(slot);
    enrt_gohma_set_sprite_attributes(slot);
    sprite_anim_set_obj_hflip(slot);
    {
        const unsigned char anim = (unsigned char)ENEMY_DRAW_FRAME(slot);
        const unsigned char frame =
            (unsigned char)((((unsigned int)side + (unsigned int)anim) & 1u) + 4u);
        draw_object_not_mirrored_with_frame(frame, slot);
    }
}

/* ---- c_gohma_animate_and_draw (NES Z_04.asm:8392) ------------------------
 *   PHA                                             ; save eye_frame
 *   Anim_FetchObjPosForSpriteDescriptor             ; SCRATCH_X/Y = ObjX/Y
 *   Gohma_SetSpriteAttributes
 *   PLA                                             ; restore eye_frame
 *   DrawObjectMirrored                              ; draw eye
 *   Anim_AdvanceAnimCounterAndSetObjPosForSpriteDescriptor (#$10)
 *   LDY #$01 / JSR Gohma_DrawLegsOneSide            ; right legs
 *   LDY #$00 / fall-through Gohma_DrawLegsOneSide   ; left legs
 */
void c_gohma_animate_and_draw(unsigned int eye_frame, unsigned int slot)
{
    (void)sprite_anim_fetch_obj_pos(slot);
    enrt_gohma_set_sprite_attributes(slot);
    draw_object_mirrored_with_frame((unsigned char)eye_frame, slot);
    sprite_anim_advance_and_fetch(0x10u, slot);
    gohma_draw_legs_one_side(1u, slot);
    gohma_draw_legs_one_side(0u, slot);
}

/* ---- c_gohma_check_collisions (NES Z_04.asm:8453) ------------------------
 *   Save ObjX. ObjX -= $10. 5-part loop (0..4):
 *     CheckMonsterCollisions ; ObjX += 8 each iter
 *   Restore ObjX.
 *   The NES @LoopSprite uses [0F] as a part counter — that's how
 *   Gohma_HandleWeaponCollision later distinguishes eye parts (3, 4)
 *   from leg/body parts. We mirror by writing ENEMY_FRAME_FLAGS
 *   (== ZP_TMPF == NES $0F) inside the loop.
 */
void c_gohma_check_collisions(unsigned int slot)
{
    const unsigned char saved_x = (unsigned char)ENEMY_X(slot);

    /* [0F] itself is the loop counter (5..1, 0 on exit), as on the NES. */
    ENEMY_X(slot) = (unsigned char)(saved_x - 0x10u);
    ENEMY_FRAME_FLAGS = 5u;
    do {
        c_check_monster_collisions(slot);
        ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot) + 8u);
        ENEMY_FRAME_FLAGS = (unsigned char)(ENEMY_FRAME_FLAGS - 1u);
    } while (ENEMY_FRAME_FLAGS != 0u);
    ENEMY_X(slot) = saved_x;
}
