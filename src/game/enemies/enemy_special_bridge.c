/* enemy_special_bridge.c — Phase 7 Task 7.5 special-enemy UPDATE
 * primitives bridge.
 *
 * Per Drain Rule D1 (drain primary, NES asm secondary tiebreaker):
 *
 * The special-enemy family ($16 PolsVoice / $17 LikeLike / $27
 * Wallmaster) UPDATE bodies are NOT directly drained as
 * enrt_update_<name> in src/oracle/enemies/. Helpers ARE drained
 * (enrt_pols_voice_move_x, enrt_wallmaster_*, etc), but the top-level
 * UPDATE state machines live only in NES Z_04.asm. This file carries
 * native bridge bodies translated per-line from NES asm with audit
 * trail, same model as enemy_boss_bridge.c (Aquamentus / Vire) and
 * enemy_jumper_bridge.c.
 *
 * Step 2 wires $17 LikeLike. Subsequent steps add $16 PolsVoice +
 * $27 Wallmaster.
 *
 * Hard rule WT-5: lives at src/game/enemies/, not engine/.
 */

#include <stdint.h>                     /* uint8_t */
#include "platform_abi.h"               /* RAM, OBJ */
#include "enemy_state.h"                /* ENEMY_*, slot 0 = Link */
#include "world/draw_dispatch.h"        /* draw_object_mirrored_with_frame,
                                         * draw_object_mirrored_over_link,
                                         * draw_object_not_mirrored_with_frame,
                                         * draw_object_not_mirrored_over_link,
                                         * k_sprite_offsets */
#include "world/sprite_dispatch.h"      /* sprite_show_link_sprites_behind_horizontal_doors */
#include "world/render/sprite_render.h"  /* roomrom_sprites_set_link_pose */
#include "enemies/enemy_dispatch.h"     /* enemy_hide_sprites_over_link */
#include "../options/options_consumer.h" /* options_consumer_get_like_like_behavior */
#include "../options/options_state.h"    /* OPTIONS_LIKELIKE_VANILLA */

/* NES RAM cell aliases not yet in enemy_state.h. */
#define LIKELIKE_CAPTURE_TIMER(slot)    OBJ(0x042Cu, (slot))  /* ObjCaptureTimer */
#define LINK_PARALYZED_FLAG             RAM(0x0512u)          /* LinkParalyzed */
#define INV_MAGIC_SHIELD                RAM(0x0676u)          /* InvMagicShield */
#define LIKELIKE_OBJ_SHOVE_DIR(slot)    OBJ(0x00C0u, (slot))  /* ObjShoveDir */
#define LIKELIKE_OBJ_SHOVE_DIST(slot)   OBJ(0x00D3u, (slot))  /* ObjShoveDistance */

/* PolsVoice RAM cell aliases (NES ObjVars.inc + Variables.inc). */
#define POLS_OBJ_REM_DISTANCE(slot)     OBJ(0x0394u, (slot))  /* ObjRemDistance */
#define POLS_OBJ_STATE(slot)            OBJ(0x00ACu, (slot))  /* ObjState */
#define POLS_STUN_TIMER(slot)           OBJ(0x003Du, (slot))  /* ObjStunTimer */
#define POLS_OBJ_SPEED_WHOLE(slot)      OBJ(0x0412u, (slot))  /* PolsVoice_ObjSpeedWhole */
#define POLS_OBJ_LAST_TILE(slot)        OBJ(0x041Fu, (slot))  /* PolsVoice_ObjLastTile (alias of ENEMY_AIR_SPEED) */
#define POLS_OBJ_TARGET_Y(slot)         OBJ(0x042Cu, (slot))  /* PolsVoice_ObjTargetY (alias of ObjCaptureTimer) */
#define POLS_OBJ_SPEED_FRAC(slot)       OBJ(0x0444u, (slot))  /* PolsVoice_ObjSpeedFrac */
#define POLS_INV_CLOCK                  RAM(0x066Cu)          /* InvClock */
#define POLS_INVINCIBILITY_MASK(slot)   OBJ(0x04B2u, (slot))  /* ObjInvincibilityMask */
#define POLS_FRAME_COUNTER              RAM(0x0015u)          /* FrameCounter */
#define POLS_RANDOM(slot)               OBJ(0x0018u, (slot))  /* Random,X ($18; was $19, T-171) */

/* PolsVoice tables (verbatim from NES Z_04.asm:6516-6531).
 *
 * PolsVoiceWalkSpeedsX is the externally-linked NES table (drained
 * `enrt_pols_voice_move_x` in src/oracle/enemies/enemy_boss_runtime.c
 * declares it as `extern const unsigned char PolsVoiceWalkSpeedsX[]`).
 * Defined here so the drain links cleanly without adding a new TU. */
const unsigned char PolsVoiceWalkSpeedsX[10] = {
    0x01u, 0xFFu, 0x00u, 0x00u, 0x01u, 0xFFu, 0x00u, 0x00u, 0x01u, 0xFFu
};
static const unsigned char k_pols_voice_walk_speeds_y[10] = {
    0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x01u, 0x00u, 0xFFu, 0xFFu, 0xFFu
};
static const unsigned char k_pols_voice_initial_jump_speeds[8] = {
    0xFDu, 0xFDu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFCu
};
static const unsigned char k_pols_voice_destination_y_offsets[8] = {
    0x00u, 0x00u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0xE0u
};
static const unsigned char k_pols_voice_directions[4] = {
    0x01u, 0x02u, 0x04u, 0x08u
};

/* Wallmaster RAM cell aliases (NES ObjVars.inc + Variables.inc). */
#define WM_OBJ_STATE(slot)              OBJ(0x00ACu, (slot))  /* ObjState */
#define WM_OBJ_SHOVE_DIR(slot)          OBJ(0x00C0u, (slot))  /* ObjShoveDir */
#define WM_OBJ_INPUT_DIR                RAM(0x03F8u)          /* ObjInputDir, slot 0 (Link) */
#define WM_OBJ_TIMER_SLOT1              RAM(0x0029u)          /* ObjTimer + 1 (slot 1) */
#define WM_INV_CLOCK                    RAM(0x066Cu)          /* InvClock */
#define WM_OBJ_STUN_TIMER(slot)         OBJ(0x003Du, (slot))  /* ObjStunTimer */
#define WM_OBJ_GRID_OFFSET(slot)        OBJ(0x0394u, (slot))  /* ObjGridOffset */
#define WM_OBJ_QSPEED_FRAC(slot)        OBJ(0x03BCu, (slot))  /* ObjQSpeedFrac */
#define WM_OBJ_ANIM_COUNTER(slot)       OBJ(0x03D0u, (slot))  /* ObjAnimCounter */
#define WM_OBJ_ANIM_FRAME(slot)         OBJ(0x03E4u, (slot))  /* ObjAnimFrame */
#define WM_OBJ_STEP(slot)               OBJ(0x0412u, (slot))  /* Wallmaster_ObjStep */
#define WM_OBJ_TILES_CROSSED(slot)      OBJ(0x041Fu, (slot))  /* Wallmaster_ObjTilesCrossed (alias of ENEMY_AIR_SPEED) */
#define WM_OBJ_CAPTURE_TIMER(slot)      OBJ(0x042Cu, (slot))  /* ObjCaptureTimer */
#define WM_ROLLING_SPRITE_INDEX         RAM(0x0341u)          /* RollingSpriteIndex */
#define WM_GAME_MODE                    RAM(0x0012u)          /* GameMode */
#define WM_GAME_SUBMODE                 RAM(0x0013u)          /* GameSubmode */
#define WM_IS_UPDATING_MODE             RAM(0x0011u)          /* IsUpdatingMode */
#define WM_SPRITES(off)                 RAM(0x0200u + (unsigned char)(off))
#define WM_SCRATCH_INSTR_AXIS           RAM(0x0002u)          /* [02] axis decrease bit */
#define WM_SCRATCH_LINK_MINOR           RAM(0x0000u)          /* [00] Link's minor coord */
#define WM_SCRATCH_LINK_MAJOR           RAM(0x0001u)          /* [01] Link's major coord */
#define WM_SCRATCH_INIT_MINOR_COORD     RAM(0x0004u)          /* [04] init minor coord */
#define WM_SCRATCH_PATCH_LEFT_OFFSET    RAM(0x0000u)          /* [00] left sprite OAM off (PatchSprites) */
#define WM_SCRATCH_PATCH_RIGHT_OFFSET   RAM(0x0001u)          /* [01] right sprite OAM off (PatchSprites) */

/* Wallmaster tables (verbatim from NES Z_04.asm:4099-4119).
 *
 * The four DirsAndAttrs tables are stored CONTIGUOUSLY in NES asm:
 * Left at $0..$F, Right at $10..$1F, Top at $20..$2F, Bottom at
 * $30..$3F. The lookup at line 4230 / 4292 / 4421 always indexes
 * `WallmasterDirsAndAttrsLeft, Y` with Y = ObjStep ranging 0..63 to
 * reach all four wall blocks. We mirror that layout in a single 64-
 * entry table indexed by ObjStep. */
static const unsigned char k_wallmaster_dirs_and_attrs[64] = {
    /* Left ($00-$0F): NES Z_04.asm:4099 */
    0x01u, 0x01u, 0x08u, 0x08u, 0x08u, 0x02u, 0x02u, 0x02u,
    0xC1u, 0xC1u, 0xC4u, 0xC4u, 0xC4u, 0xC2u, 0xC2u, 0xC2u,
    /* Right ($10-$1F): NES Z_04.asm:4103 */
    0x42u, 0x42u, 0x48u, 0x48u, 0x48u, 0x41u, 0x41u, 0x41u,
    0x82u, 0x82u, 0x84u, 0x84u, 0x84u, 0x81u, 0x81u, 0x81u,
    /* Top ($20-$2F): NES Z_04.asm:4107 */
    0xC4u, 0xC4u, 0xC2u, 0xC2u, 0xC2u, 0xC8u, 0xC8u, 0xC8u,
    0x84u, 0x84u, 0x81u, 0x81u, 0x81u, 0x88u, 0x88u, 0x88u,
    /* Bottom ($30-$3F): NES Z_04.asm:4111 */
    0x48u, 0x48u, 0x42u, 0x42u, 0x42u, 0x44u, 0x44u, 0x44u,
    0x08u, 0x08u, 0x01u, 0x01u, 0x01u, 0x04u, 0x04u, 0x04u,
};
static const unsigned char k_wallmaster_initial_xs[2] = { 0x00u, 0xF0u };
static const unsigned char k_wallmaster_initial_ys[2] = { 0x3Du, 0xDDu };

/* Drained primitives (enemy_walker_bridge / enemy_boss_bridge / projectile). */
extern void z04_update_common_wanderer(unsigned int turn_rate, unsigned int slot);
extern void c_check_monster_collisions(unsigned int slot);
extern void c_obj_shove(unsigned int slot);
extern void c_move_object(unsigned short slot);
extern unsigned char z07_anim_fetch_obj_pos(unsigned int slot);
extern void z07_anim_advance_and_fetch(unsigned int val, unsigned int slot);

/* Drained PolsVoice helpers (enemy_boss_runtime.c). */
extern void enrt_pols_voice_move_x(unsigned int slot);
extern unsigned int enrt_pols_voice_is_square_walkable(unsigned int slot);

/* Drained Wallmaster helpers (enemy_wallmaster_runtime.c +
 * enemy_boss_runtime.c). */
extern void enrt_wallmaster_prepare_to_draw(unsigned int slot);
extern unsigned int enrt_wallmaster_calc_start_position(unsigned int instr_offset,
                                                        unsigned int init_major_min,
                                                        unsigned int slot);
extern void enrt_wallmaster_put_sprites_behind_bg_if_needed(void);
extern void enemy_render_wallmaster_patch(unsigned char slot, unsigned char closed_hand);
extern void roomrom_main_set_link_story_pose(unsigned char x, unsigned char y,
                                            unsigned char face);
extern unsigned char roomrom_main_current_link_face(void);
extern void roomrom_combat_animate_link_base(void);

/* The captured-Link call enters Link_EndMoveAndAnimate_Bank4 with Link's
 * ObjX/Y already copied from the hand. Keep the native player owner in
 * sync, then run its drained AnimateLinkBase and redraw Link at the hand.
 * The Wallmaster path has Link halted and ObjGridOffset=0; the normal
 * player movement/warp branches do not run here. */
static void wm_link_end_move_and_animate_bank4(void)
{
    unsigned char x = (unsigned char)ENEMY_X(0u);
    unsigned char y = (unsigned char)ENEMY_Y(0u);
    unsigned char face = roomrom_main_current_link_face();
    roomrom_main_set_link_story_pose(x, y, face);
    roomrom_combat_animate_link_base();
    if (RAM(0x04F0u) != 0u)
        roomrom_sprites_set_link_hurt_pose((short)x, (short)y,
            (link_face_t)face, (unsigned char)(WM_OBJ_ANIM_FRAME(0u) & 1u),
            (unsigned char)RAM(0x04F0u));
    else
        roomrom_sprites_set_link_pose((short)x, (short)y,
            (link_face_t)face, (unsigned char)(WM_OBJ_ANIM_FRAME(0u) & 1u));
}

/* CARRY_SET sentinel matches enrt_*_runtime.c. */
#ifndef CARRY_SET
#define CARRY_SET 0x10000u
#endif

/*--------------------------------------------------------------------
 * UpdateLikeLike (drained from Z_04.asm:6818)
 *
 * State machine has two paths driven by ObjCaptureTimer (slot's
 * $042C cell):
 *
 * 1. ObjCaptureTimer == 0 (free roaming):
 *    - UpdateCommonWanderer(turn_rate=$80) — wander + 1px-pulse.
 *    - 4-frame anim cycle (vs 2-frame default): every 8 frames
 *      step (frame+1) & 3.
 *    - Fetch obj pos + draw mirrored + check monster collisions.
 *    - If post-collision ObjCaptureTimer != 0 (Link grab fired
 *      inside CheckMonsterCollisions), seed capture state:
 *      monster X/Y = Link X/Y, clear Link's timer / metastate /
 *      shove dir+distance, reset monster anim (frame=0,counter=4),
 *      paralyze Link.
 *
 * 2. ObjCaptureTimer != 0 (Link captured):
 *    - Animate up to frame 3 (LDA #$02 / CMP frame / BCC = skip
 *      anim if frame > 2; else dec counter; on rollover counter=4
 *      and frame++).
 *    - Increment capture timer; on >= $60, clear magic shield + lock
 *      timer at $C0 (so the bite-flash keeps replaying without
 *      overflowing).
 *    - Draw mirrored OVER Link (sprite slots $10/$11) so the
 *      like-like body covers him.
 *    - Check collisions; if metastate != 0 (monster died from Link's
 *      sword swing), free Link (LinkParalyzed=0) + hide the
 *      over-Link sprites.
 *
 * NES tail-call HideSpritesOverLink at the bottom of @HandleCaptured
 * branch is replaced with a direct call to enemy_hide_sprites_over_link.
 *------------------------------------------------------------------*/
void enrt_update_like_like(unsigned int slot)
{
    if (LIKELIKE_CAPTURE_TIMER(slot) != 0u) {
        /* @HandleCaptured branch. */
        unsigned char frame = (unsigned char)ENEMY_DRAW_FRAME(slot);
        if (frame <= 0x02u) {
            /* NES: LDA #$02 / CMP frame / BCC @IncCaptureTime —
             * BCC = $02 < frame, i.e. frame > $02 -> skip animate. */
            unsigned char counter =
                (unsigned char)(ENEMY_ANIM_TIMER(slot) - 1u);
            ENEMY_ANIM_TIMER(slot) = counter;
            if (counter == 0u) {
                ENEMY_ANIM_TIMER(slot) = 0x04u; /* ASL of $02. */
                ENEMY_DRAW_FRAME(slot) = (uint8_t)(frame + 1u);
            }
        }
        /* @IncCaptureTime: */
        LIKELIKE_CAPTURE_TIMER(slot) =
            (uint8_t)((unsigned char)LIKELIKE_CAPTURE_TIMER(slot) + 1u);
        if ((unsigned char)LIKELIKE_CAPTURE_TIMER(slot) >= 0x60u) {
            /* Phase 9 Task 9.4 LIKE_LIKE_BEHAVIOR consumer.
             * VANILLA (NES Z_04.asm:6818 path) eats magic shield once
             * the capture timer exceeds $60. Redux NO_EAT skips that
             * write — the bite-flash still cycles, but the shield
             * survives. Capture-timer lock at $C0 stays unconditional
             * to match the lock semantics from the NES routine. */
            if (options_consumer_get_like_like_behavior()
                == OPTIONS_LIKELIKE_VANILLA) {
                INV_MAGIC_SHIELD = 0u;
            }
            LIKELIKE_CAPTURE_TIMER(slot) = 0xC0u;
        }
        /* @DrawAfterCapture: */
        z07_anim_fetch_obj_pos(slot);
        draw_object_mirrored_over_link(
            (unsigned char)ENEMY_DRAW_FRAME(slot), slot);
        c_check_monster_collisions(slot);
        if ((unsigned char)ENEMY_METASTATE(slot) != 0u) {
            /* Monster died — release Link + hide over-Link sprites. */
            LINK_PARALYZED_FLAG = 0u;
            enemy_hide_sprites_over_link();
        }
        return;
    }

    /* Free-roaming path. */
    z04_update_common_wanderer(0x80u, slot);

    /* 4-frame anim. NES: DEC counter; BNE @Draw; LDA #$08; STA counter;
     * INY (frame+1); AND #$03; STA frame. */
    {
        unsigned char counter =
            (unsigned char)(ENEMY_ANIM_TIMER(slot) - 1u);
        ENEMY_ANIM_TIMER(slot) = counter;
        if (counter == 0u) {
            ENEMY_ANIM_TIMER(slot) = 0x08u;
            ENEMY_DRAW_FRAME(slot) =
                (uint8_t)(((unsigned char)ENEMY_DRAW_FRAME(slot) + 1u)
                          & 0x03u);
        }
    }

    /* @Draw. */
    z07_anim_fetch_obj_pos(slot);
    draw_object_mirrored_with_frame(
        (unsigned char)ENEMY_DRAW_FRAME(slot), slot);
    c_check_monster_collisions(slot);

    /* If post-collision capture timer fired, seed capture state. */
    if ((unsigned char)LIKELIKE_CAPTURE_TIMER(slot) == 0u) {
        return;
    }
    /* Monster captured Link — overlap him + reset link state. */
    ENEMY_X(slot) = (uint8_t)ENEMY_X(0u);
    ENEMY_Y(slot) = (uint8_t)ENEMY_Y(0u);
    ENEMY_MOVE_TIMER(0u) = 0u;
    ENEMY_METASTATE(0u) = 0u;
    LIKELIKE_OBJ_SHOVE_DIR(0u) = 0u;
    LIKELIKE_OBJ_SHOVE_DIST(0u) = 0u;
    /* Restart monster's movement frame cycle: frame 0, counter 4. */
    ENEMY_DRAW_FRAME(slot) = 0u;
    ENEMY_ANIM_TIMER(slot) = 0x04u;
    /* Now Link can't move. */
    LINK_PARALYZED_FLAG =
        (uint8_t)((unsigned char)LINK_PARALYZED_FLAG + 1u);
}

/*--------------------------------------------------------------------
 * UpdatePolsVoice (drained from Z_04.asm:6533)
 *
 * Two-state machine driven by ObjState:
 *
 *   State 0 — walking. Decrement ObjRemDistance; on rollover schedule
 *             a state-1 jump. Add per-direction WalkSpeedY (table) to
 *             ObjY; check walkability — if blocked by tile $B0 or
 *             $F4..$FF, transition to state 1; else flip direction
 *             (horizontal: EOR $03 + 2 MoveX calls; vertical:
 *             EOR $0C).
 *
 *   State 1 — jumping. Add fractional accel $38 to SpeedFrac with
 *             carry into SpeedWhole; add SpeedWhole to ObjY (vertical
 *             projectile arc). Exit early if speed still negative or
 *             ObjY < TargetY. On reach: zero SpeedFrac/Whole, pick a
 *             new random direction (Random & 3 -> directions[$01,$02,
 *             $04,$08]), random distance ($31 or $71), then snap X/Y
 *             to grid (X=(X+8)&$F0, Y=((Y+8)&$F0)-3).
 *
 * Pre-pass guards (both states):
 *   - InvClock active OR ObjStunTimer != 0 -> draw + collisions only.
 *   - Odd FrameCounter (LSR carry set) -> draw + collisions only.
 *
 * Tail-call (both states):
 *   - Anim_AdvanceAnimCounterAndSetObjPosForSpriteDescriptor(8).
 *   - DrawObjectMirrored(ObjAnimFrame).
 *   - ObjInvincibilityMask = $FE  (sword-only damage; arrow special-
 *     cased outside).
 *   - CheckMonsterCollisions.
 *
 * Carry-from-CMP semantics (state-1 reach branch):
 *   The "ADC #$30" after "AND #$40" depends on carry from the prior
 *   "CMP PolsVoice_ObjTargetY" — taking the BCS-fall-through implies
 *   ObjY >= TargetY, so carry is set. The translation hardcodes +1 to
 *   match: ObjRemDistance = (Random & $40) + $30 + 1.
 *
 * PolsVoice_MoveX leaves Y register = ObjDir - 1 in NES asm; we
 * recompute that index after the call as `dir_idx`.
 *------------------------------------------------------------------*/
void enrt_update_pols_voice(unsigned int slot)
{
    unsigned char dir_idx = 0u;
    unsigned char tile;
    unsigned char dir;
    unsigned char y_idx;

    /* Magic clock or stun -> bypass movement. */
    if ((unsigned char)POLS_INV_CLOCK != 0u
        || (unsigned char)POLS_STUN_TIMER(slot) != 0u) {
        goto draw_and_check;
    }

    /* Odd frame -> bypass movement. NES: LDA FrameCounter / LSR / BCS. */
    if (((unsigned char)POLS_FRAME_COUNTER & 0x01u) != 0u) {
        goto draw_and_check;
    }

    /* Always JSR PolsVoice_MoveX before state branch. */
    enrt_pols_voice_move_x(slot);
    dir_idx = (unsigned char)((unsigned char)ENEMY_DIR(slot) - 1u);

    if ((unsigned char)POLS_OBJ_STATE(slot) != 0u) {
        /* State 1: jumping (Z_04.asm:6675 UpdatePolsVoiceState1_Jumping). */
        unsigned int sum_frac =
            (unsigned int)(unsigned char)POLS_OBJ_SPEED_FRAC(slot) + 0x38u;
        unsigned char carry = (sum_frac >= 0x100u) ? 1u : 0u;
        unsigned char whole;
        POLS_OBJ_SPEED_FRAC(slot) = (uint8_t)sum_frac;
        whole = (unsigned char)((unsigned char)POLS_OBJ_SPEED_WHOLE(slot)
                                + carry);
        POLS_OBJ_SPEED_WHOLE(slot) = whole;
        ENEMY_Y(slot) =
            (uint8_t)((unsigned char)ENEMY_Y(slot) + whole);

        /* If speed still negative OR ObjY < TargetY, fall through to
         * walkability check (NES: BMI/BCC -> @Exit -> RTS -> caller's
         * JMP @CheckWalkability). */
        if ((whole & 0x80u) == 0u
            && (unsigned char)ENEMY_Y(slot)
               >= (unsigned char)POLS_OBJ_TARGET_Y(slot))
        {
            /* Reached destination -> back to state 0, randomize. */
            unsigned char rng_dir;
            unsigned char rng_dist;
            POLS_OBJ_STATE(slot) = 0u;
            POLS_OBJ_SPEED_FRAC(slot) = 0u;
            POLS_OBJ_SPEED_WHOLE(slot) = 0u;
            rng_dir = (unsigned char)POLS_RANDOM(slot);
            ENEMY_DIR(slot) = k_pols_voice_directions[rng_dir & 0x03u];
            rng_dist = (unsigned char)POLS_RANDOM(slot);
            /* Carry from CMP target was set (we passed BCC) -> ADC adds 1. */
            POLS_OBJ_REM_DISTANCE(slot) =
                (uint8_t)((rng_dist & 0x40u) + 0x30u + 1u);
            /* Snap X/Y to grid. */
            ENEMY_X(slot) =
                (uint8_t)(((unsigned char)ENEMY_X(slot) + 0x08u) & 0xF0u);
            ENEMY_Y(slot) =
                (uint8_t)((((unsigned char)ENEMY_Y(slot) + 0x08u) & 0xF0u)
                          - 0x03u);
        }
        goto check_walkability;
    }

    /* State 0: walking. */
    if ((unsigned char)POLS_OBJ_REM_DISTANCE(slot) == 0u) {
        goto set_state1;
    }
    POLS_OBJ_REM_DISTANCE(slot) =
        (uint8_t)((unsigned char)POLS_OBJ_REM_DISTANCE(slot) - 1u);
    ENEMY_Y(slot) = (uint8_t)((unsigned char)ENEMY_Y(slot)
                              + k_pols_voice_walk_speeds_y[dir_idx]);
    /* Fall through. */

check_walkability:
    {
        unsigned int walk = enrt_pols_voice_is_square_walkable(slot);
        if ((walk & CARRY_SET) == 0u) {
            goto draw_and_check;
        }
    }
    tile = (unsigned char)((unsigned char)POLS_OBJ_LAST_TILE(slot) & 0xFCu);
    if (tile == 0xB0u || tile >= 0xF4u) {
        goto set_state1;
    }
    /* Flip direction. */
    dir = (unsigned char)ENEMY_DIR(slot);
    if ((dir & 0x03u) != 0u) {
        /* Horizontal: EOR $03 (right<->left), then 2x MoveX. */
        ENEMY_DIR(slot) = (uint8_t)((dir & 0x03u) ^ 0x03u);
        enrt_pols_voice_move_x(slot);
        enrt_pols_voice_move_x(slot);
    } else {
        /* Vertical: EOR $0C (down<->up). */
        ENEMY_DIR(slot) = (uint8_t)(dir ^ 0x0Cu);
    }
    goto draw_and_check;

set_state1:
    if ((unsigned char)POLS_OBJ_STATE(slot) != 0u) {
        /* Already jumping (entered SetState1 via tile-block). */
        goto draw_and_check;
    }
    POLS_OBJ_STATE(slot) =
        (uint8_t)((unsigned char)POLS_OBJ_STATE(slot) + 1u);
    y_idx = (unsigned char)((unsigned char)ENEMY_DIR(slot) - 1u);
    /* Edge guards: top half forces idx=3 (down jump), bottom forces
     * idx=7 (up jump), so PolsVoice never jumps off-screen. */
    if ((unsigned char)ENEMY_Y(slot) < 0x78u) {
        y_idx = 0x03u;
    }
    if ((unsigned char)ENEMY_Y(slot) >= 0xA8u) {
        y_idx = 0x07u;
    }
    POLS_OBJ_SPEED_WHOLE(slot) =
        k_pols_voice_initial_jump_speeds[y_idx];
    POLS_OBJ_TARGET_Y(slot) =
        (uint8_t)((unsigned char)ENEMY_Y(slot)
                  + k_pols_voice_destination_y_offsets[y_idx]);
    ENEMY_DIR(slot) = (uint8_t)(y_idx + 1u);
    /* Fall through. */

draw_and_check:
    z07_anim_advance_and_fetch(0x08u, slot);
    draw_object_mirrored_with_frame(
        (unsigned char)ENEMY_DRAW_FRAME(slot), slot);
    POLS_INVINCIBILITY_MASK(slot) = 0xFEu;
    c_check_monster_collisions(slot);
    (void)dir_idx;
}

/*--------------------------------------------------------------------
 * UpdateWallmaster (drained from Z_04.asm:4121)
 *
 * Two-state machine driven by ObjState:
 *
 *   State 0 - idle inside wall. Gated on Link's metastate ($40 = stunned),
 *             Link's frame timer slot 1 (=0), and Link standing in the
 *             trigger zone next to one of the four walls. If gating
 *             passes, calls Wallmaster_CalcStartPosition (drained) to
 *             compute the emergence offset + initial X/Y, sets up
 *             initial dir from the dirs-and-attrs table indexed by
 *             ObjStep (the byte CalcStartPosition wrote), seeds
 *             timer1=$60 / qspeed=$18 / animcount=$08, zeroes
 *             grid offset / tiles crossed / anim frame, then INC
 *             ObjState to enter state 1.
 *
 *   State 1 - walking along wall toward Link. Each frame:
 *             - If shoved (ObjShoveDir != 0), Obj_Shove + draw + collisions.
 *             - Otherwise, if magic clock or stun, draw + collisions.
 *             - Otherwise, MoveObject in current dir; on hitting grid
 *               alignment ($10 / $F0 inner-frac), advance ObjStep,
 *               re-fetch dir from table, increment tiles-crossed; on
 *               7th tile, end of trip:
 *                 - If Link uncaptured: ObjState[slot]=0 + RTS.
 *                 - If Link captured: HideSpritesOverLink, GameMode=3,
 *                   ObjState[Link]=0, IsUpdatingMode=0, GameSubmode=0,
 *                   ObjState[slot]=0, RTS (mode 3 = unfurl reset).
 *             - Otherwise fall through to draw + collisions.
 *
 *   Draw + collisions:
 *     - If Link captured (ObjCaptureTimer != 0), draw with captured Link
 *       (reposition Link onto monster, force frame=1 hand-closed, draw
 *       OverLink + patch sprite priority + apply $9C->$AC tile fixup
 *       using hardcoded sprite offsets $40/$44).
 *     - Otherwise: CheckMonsterCollisions; if collision captured Link,
 *       seed Link's metastate=$40 + clear shove dir; then save sprite
 *       cursor, PrepareToDraw (advances anim + sets attrs), draw
 *       NotMirrored, restore cursor, look up SpriteOffsets[idx], apply
 *       $9C keese-tile patch on the closed-hand frame.
 *
 * Trigger-zone gates (NES Z_04.asm:4144-4202):
 *   - LeftRight: Link X in {$20, $D0}. Out-of-corridor (X<$29 || X>=$C8
 *     and Y<$6D || Y>=$B5) early-exits.
 *   - TopBottom: Link Y in {$5D, $BD}.
 *
 * Carry-from-CMP: NES `LDA WallmasterDirsAndAttrsLeft, Y` with Y running
 * 0..63 reaches Right ($10..$1F), Top ($20..$2F), Bottom ($30..$3F)
 * because the four blocks are stored CONTIGUOUSLY. The native port
 * mirrors that with one 64-byte table.
 *
 * Link_EndMoveAndAnimate_Bank4 uses the native Link owner and drained
 * animation in the captured-Link branch (see helper above).
 *------------------------------------------------------------------*/
void enrt_update_wallmaster(unsigned int slot)
{
    unsigned char link_x;
    unsigned char link_y;
    unsigned int idx;
    unsigned char step;
    unsigned char attrs_dirs;
    unsigned char saved_sprite_idx;
    unsigned char left_off;
    unsigned char right_off;
    unsigned char anim_frame;

    if ((unsigned char)WM_OBJ_STATE(slot) != 0u) {
        goto state1;
    }

    /* State 0: idle gating. */
    if ((unsigned char)WM_OBJ_TIMER_SLOT1 != 0u) {
        return;
    }
    /* NES: LDA ObjState / CMP #$40 / BNE @State0 -- a halted Link ($40)
     * holds them back (T-013: the test was inverted and no wallmaster
     * ever came out). */
    if ((unsigned char)WM_OBJ_STATE(0u) == 0x40u) {
        return;
    }

    link_x = (unsigned char)ENEMY_X(0u);
    link_y = (unsigned char)ENEMY_Y(0u);

    /* Out-of-corridor early-exit:
     *   if (link_x < $29 || link_x >= $C8) and (link_y < $6D || link_y >= $B5) -> exit.
     * NES uses fall-through structure: BCC :+ / CMP $C8 / BCC @CheckLeftAndRight,
     * so X in [$29..$C7] forces the LeftRight check; X outside falls through
     * to the Y range gate which exits if Y<$6D or Y>=$B5.
     */
    if (link_x < 0x29u || link_x >= 0xC8u) {
        if (link_y < 0x6Du || link_y >= 0xB5u) {
            return;
        }
    }

    /* @CheckLeftAndRight: Link X must equal $20 or $D0 to trigger side wall. */
    if (link_x == 0x20u || link_x == 0xD0u) {
        /* Link at side wall. */
        WM_SCRATCH_LINK_MINOR  = link_y;   /* [00] minor = Link Y */
        WM_SCRATCH_LINK_MAJOR  = link_x;   /* [01] major = Link X */
        WM_SCRATCH_INSTR_AXIS  = 0x08u;    /* [02] decreasing dir = up */
        idx = enrt_wallmaster_calc_start_position(0x00u, 0x20u, slot);
        ENEMY_Y(slot) = (uint8_t)WM_SCRATCH_INIT_MINOR_COORD;
        ENEMY_X(slot) = k_wallmaster_initial_xs[idx & 1u];
    } else {
        /* @CheckTopAndBottom: Link Y must equal $5D or $BD. */
        if (link_y != 0x5Du && link_y != 0xBDu) {
            return;
        }
        WM_SCRATCH_LINK_MINOR  = link_x;   /* [00] minor = Link X */
        WM_SCRATCH_LINK_MAJOR  = link_y;   /* [01] major = Link Y */
        WM_SCRATCH_INSTR_AXIS  = 0x02u;    /* [02] decreasing dir = left */
        idx = enrt_wallmaster_calc_start_position(0x20u, 0x5Du, slot);
        ENEMY_Y(slot) = k_wallmaster_initial_ys[idx & 1u];
        ENEMY_X(slot) = (uint8_t)WM_SCRATCH_INIT_MINOR_COORD;
    }

    /* @SetUpToEmerge: shared tail. ObjStep was set by CalcStartPosition. */
    step = (unsigned char)WM_OBJ_STEP(slot);
    ENEMY_DIR(slot) = (uint8_t)(k_wallmaster_dirs_and_attrs[step & 0x3Fu]
                                & 0x0Fu);
    WM_OBJ_TIMER_SLOT1            = 0x60u;
    WM_OBJ_QSPEED_FRAC(slot)      = 0x18u;
    WM_OBJ_ANIM_COUNTER(slot)     = 0x08u;
    WM_OBJ_GRID_OFFSET(slot)      = 0u;
    WM_OBJ_TILES_CROSSED(slot)    = 0u;
    WM_OBJ_ANIM_FRAME(slot)       = 0u;
    WM_OBJ_STATE(slot) =
        (uint8_t)((unsigned char)WM_OBJ_STATE(slot) + 1u);
    return;

state1:
    /* L_Wallmaster_State1. */
    if ((unsigned char)WM_OBJ_SHOVE_DIR(slot) != 0u) {
        c_obj_shove(slot);
        goto draw_and_check_collisions;
    }

    /* @CheckStunned: magic clock OR stun -> draw + collisions only. */
    if (((unsigned char)WM_INV_CLOCK
         | (unsigned char)WM_OBJ_STUN_TIMER(slot)) != 0u) {
        goto draw_and_check_collisions;
    }

    /* Move along wall. NES stores ObjDir into [$0F] (LINK_MOVING_DIR
     * scratch read by collision_get_colliding_tile_moving inside
     * MoveObject). object_move_object reads the dir from the slot's
     * cell directly; populate $0F for the collision probe path. */
    RAM(0x000Fu) = (uint8_t)ENEMY_DIR(slot);
    c_move_object((unsigned short)slot);

    {
        unsigned char grid = (unsigned char)WM_OBJ_GRID_OFFSET(slot);
        if (grid != 0x10u && grid != 0xF0u) {
            goto draw_and_check_collisions;
        }
    }
    /* Square-aligned: truncate offset, advance step + dir, count tile. */
    WM_OBJ_GRID_OFFSET(slot) = 0u;
    WM_OBJ_STEP(slot) =
        (uint8_t)((unsigned char)WM_OBJ_STEP(slot) + 1u);
    step = (unsigned char)WM_OBJ_STEP(slot);
    ENEMY_DIR(slot) = (uint8_t)(k_wallmaster_dirs_and_attrs[step & 0x3Fu]
                                & 0x0Fu);
    WM_OBJ_TILES_CROSSED(slot) =
        (uint8_t)((unsigned char)WM_OBJ_TILES_CROSSED(slot) + 1u);
    if ((unsigned char)WM_OBJ_TILES_CROSSED(slot) < 0x07u) {
        goto draw_and_check_collisions;
    }

    /* End-of-trip (>= 7 tiles). */
    if ((unsigned char)WM_OBJ_CAPTURE_TIMER(slot) != 0u) {
        /* Link captured: hide sprites, force unfurl mode, reset Link/sub. */
        enemy_hide_sprites_over_link();
        WM_GAME_MODE        = 0x03u;
        WM_OBJ_STATE(0u)    = 0u;
        WM_IS_UPDATING_MODE = 0u;
        WM_GAME_SUBMODE     = 0u;
    }
    /* Both branches end with ObjState[slot]=0 + return. */
    WM_OBJ_STATE(slot) = 0u;
    return;

draw_and_check_collisions:
    if ((unsigned char)WM_OBJ_CAPTURE_TIMER(slot) != 0u) {
        goto draw_with_captured_link;
    }
    c_check_monster_collisions(slot);
    /* Post-collision: if Link just got captured, halt him + clear shove. */
    if ((unsigned char)WM_OBJ_CAPTURE_TIMER(slot) != 0u) {
        WM_OBJ_STATE(0u)     = 0x40u;
        WM_OBJ_SHOVE_DIR(0u) = 0u;
    }

    /* Save sprite cursor (NES PHA), draw, restore (PLA TAY). */
    saved_sprite_idx = (unsigned char)WM_ROLLING_SPRITE_INDEX;
    enrt_wallmaster_prepare_to_draw(slot);
    anim_frame = (unsigned char)WM_OBJ_ANIM_FRAME(slot);
    draw_object_not_mirrored_with_frame(anim_frame, slot);

    /* SpriteOffsets[saved_sprite_idx] / [+1] -> [00] / [01]. */
    left_off  = k_sprite_offsets[saved_sprite_idx & 0x3Fu];
    right_off = k_sprite_offsets[(unsigned char)(saved_sprite_idx + 1u) & 0x3Fu];

patch_sprites:
    WM_SCRATCH_PATCH_LEFT_OFFSET  = left_off;
    WM_SCRATCH_PATCH_RIGHT_OFFSET = right_off;
    enrt_wallmaster_put_sprites_behind_bg_if_needed();
    /* The drawn pair lives in the native render cache: patch it there. */
    enemy_render_wallmaster_patch((unsigned char)slot,
                                  (unsigned char)(WM_OBJ_ANIM_FRAME(slot) != 0u));

    /* Frame 0 = open hand: nothing to patch, exit. Frame 1 = closed hand:
     * its $9C/$9D left tile is the Keese; substitute $AC. The left/right
     * pair may be swapped on horizontal flip — find the slot whose tile
     * byte equals $9C and patch only that one. */
    anim_frame = (unsigned char)WM_OBJ_ANIM_FRAME(slot);
    if (anim_frame == 0u) {
        return;
    }
    {
        unsigned char left_tile_off  = (unsigned char)(left_off + 1u);
        unsigned char right_tile_off = (unsigned char)(right_off + 1u);
        if ((unsigned char)WM_SPRITES(left_tile_off) == 0x9Cu) {
            WM_SPRITES(left_tile_off) = 0xACu;
        } else {
            WM_SPRITES(right_tile_off) = 0xACu;
        }
    }
    return;

draw_with_captured_link:
    /* Reposition Link onto monster, retick/draw, then draw hand on top. */
    ENEMY_X(0u) = (uint8_t)ENEMY_X(slot);
    ENEMY_Y(0u) = (uint8_t)ENEMY_Y(slot);
    wm_link_end_move_and_animate_bank4();
    sprite_show_link_sprites_behind_horizontal_doors();
    enrt_wallmaster_prepare_to_draw(slot);
    WM_OBJ_ANIM_FRAME(slot) = 0x01u;        /* force closed hand */
    draw_object_not_mirrored_over_link(0x01u, slot);
    /* Hardcoded over-Link sprite slots $10/$11 -> OAM offsets $40/$44. */
    left_off  = 0x40u;
    right_off = 0x44u;
    /* Reuse PatchSprites for the priority/keese fixup. */
    (void)attrs_dirs;
    goto patch_sprites;
}
