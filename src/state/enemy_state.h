#ifndef ENEMY_STATE_H
#define ENEMY_STATE_H

#include "platform_abi.h"
#include "scratch_state.h"

/* Enemy/object state map for promoted z_04 gameplay code. Keep names
 * close to gameplay meaning so owned C stops depending on raw offsets.
 */
#define ENEMY_DIR(slot)                 F_ram_0098_O3((slot))
#define ENEMY_STATE_TIMER(slot)         F_ram_00AC_O2((slot))
#define ENEMY_LIFE(slot)                F_ram_00BF_O((slot))
#define ENEMY_X(slot)                   F_ram_0070_O2((slot))
#define ENEMY_Y(slot)                   F_ram_0084_O2((slot))
#define ENEMY_RNG_A(slot)               F_ram_0018_O2((slot))
#define ENEMY_RNG_B(slot)               F_ram_0019_O((slot))
#define ENEMY_MOVE_TIMER(slot)          F_ram_0028_O2((slot))
#define ENEMY_STUN_TIMER(slot)          F_NES_OBJ_STUN_TIMER_O3((slot))
#define ENEMY_TYPE(slot)                F_ram_034F_O2((slot))
#define ENEMY_METASTATE(slot)           F_NES_OBJ_METASTATE_BASE_O((slot))
#define ENEMY_PUSH_TIMER(slot)          F_ram_0412_O((slot))
#define ENEMY_FLYER_SPEED_FRAC(slot)    F_ram_0412_O((slot))
#define ENEMY_AIR_SPEED(slot)           F_ram_041F_O((slot))
#define ENEMY_TURN_TIMER(slot)          F_ram_042C_O2((slot))
#define ENEMY_FLAP_PHASE(slot)          F_ram_0437_O2((slot))
#define ENEMY_AI_STATE(slot)            F_ram_0444_O2((slot))
#define ENEMY_BLOATED_TIMER(slot)       F_ram_045E_O((slot))
#define ENEMY_FLYER_X_FINE(slot)        F_ram_046B_O2((slot))
#define ENEMY_BOUNCE_FLAGS(slot)        F_ram_0478_O2((slot))
#define ENEMY_FLYER_Y_FINE(slot)        F_ram_0478_O2((slot))
#define ENEMY_CHARGE_SPEED(slot)        F_ram_0485_O2((slot))
#define ENEMY_COLLIDED_TILE(slot)       F_ram_049E_O((slot))
#define ENEMY_INVINCIBILITY(slot)       F_ram_04B2_O2((slot))
#define ENEMY_MAX_AIR_SPEED             F_ENEMY_MAX_AIR_SPEED_R
#define ENEMY_HIT_REACTION(slot)        F_ram_04F0_O3((slot))
#define ENEMY_ALIVE_FLAG(slot)          F_ram_0492_O((slot))
#define ENEMY_DRAW_FRAME(slot)          F_ram_03E4_O2((slot))
#define ENEMY_WALK_SPEED(slot)          F_ram_03BC_O2((slot))
#define ENEMY_ANIM_TIMER(slot)          F_ram_03D0_O2((slot))
#define ENEMY_PUSH_DIR_SCRATCH(slot)    F_ram_03F8_O2((slot))
#define ENEMY_SHOT_COUNT                F_ENEMY_SHOT_COUNT_R2
#define ENEMY_ROOM_MONSTER_FLAG         F_ENEMY_ROOM_MONSTER_FLAG_R2
#define ENEMY_OAM_HIDE_0                F_ENEMY_OAM_HIDE_0_R
#define ENEMY_OAM_HIDE_1                F_ENEMY_OAM_HIDE_1_R
#define ENEMY_SFX_SECRET                F_ram_0602_R2
#define ENEMY_SFX_BOSS_CRY              F_ram_0601_R2
#define ENEMY_SFX_BOSS_CRY_FLAGS        F_ram_0603_R2
#define ENEMY_SFX_PARRY                 F_ram_0604_R2
#define ENEMY_BOULDER_SET_COUNT         F_ENEMY_BOULDER_SET_COUNT_R
#define ENEMY_CANDLE_FADE_TRIGGER       F_ram_051C_R2
#define ENEMY_CANDLE_FADE_PHASE         F_ram_051E_R
#define ENEMY_CANDLE_STATE              F_ram_051F_R
#define ENEMY_CANDLE_ROOM_ID            F_ram_00EB_R3
#define ENEMY_PAUSE_FLAG                F_ram_066C_R3
#define ENEMY_FREEZE_FLAG               F_ram_0506_R2
#define ENEMY_COLLISION_FLAG            ZP_TMP6
#define ENEMY_BLOCKED_FLAG              ZP_TMPE
#define ENEMY_BUBBLE_EFFECT             F_ENEMY_BUBBLE_EFFECT_R
#define ENEMY_LEEVER_TIMER              F_ram_004D_R2
#define ENEMY_BUBBLE_STATUS             F_ram_052E_R
/* NES ChaseTargetX/Y ($61/$62): where monsters head. Usually Link's
 * position, but UpdateMode5Play substitutes a decoy point while
 * ChaseOtherTarget ($60) is set. Previously named LINK_X/LINK_Y behind
 * #ifndef, so TUs that also included world_state.h (LINK_X = ObjX $70)
 * silently got the other meaning (T-093). */
#define CHASE_TARGET_X                  F_CHASE_TARGET_X_R2
#define CHASE_TARGET_Y                  F_CHASE_TARGET_Y_R2
/* SAVE_SLOT_INDEX is defined in item_state.h (canonical owner). Pulled
 * in transitively via the include below so existing enemy_*_runtime.c
 * consumers don't need their own #include. */
#include "item_state.h"
#define SAVE_SLOT_QUEST(slot)           F_ram_062D_R3((slot))
#define ENEMY_GLEEOK_HEAD_TIMER         F_ram_0418_R2
#define ENEMY_GLEEOK_SEG_X(slot)        F_ram_0072_O((slot))
#define ENEMY_GLEEOK_SEG_X_TARGET(slot) F_ram_0073_O((slot))
#define ENEMY_GLEEOK_SEG_Y(slot)        F_ram_0086_O((slot))
#define ENEMY_GLEEOK_SEG_Y_TARGET(slot) F_ram_0087_O((slot))
#define ENEMY_MANHANDLA_SEGMENT_DIED_FLAG F_ENEMY_MANHANDLA_SEGMENT_DIED_FLAG_R
#define ENEMY_MANHANDLA_FRAME_ACCUM(slot) F_ram_0451_O((slot))
#define ENEMY_MANHANDLA_FRAME_ATTR(slot)  F_ram_0478_O2((slot))
#define ENEMY_LAMNOLA_SPEED             F_ram_04E6_R
#define ENEMY_LAMNOLA_TYPE              F_ram_04E7_R
#define ENEMY_LAMNOLA_VIABLE_DIR_MASK   F_ENEMY_LAMNOLA_VIABLE_DIR_MASK_R
#define ENEMY_PLAYER_OBJ_X              F_ram_0070_R2
#define ENEMY_PLAYER_OBJ_Y              F_ram_0084_R2
#define ENEMY_VIRE_SPLIT_TYPE           ZP_TMP0
#define ENEMY_STATUE_PERSON_FIREBALLS   F_ENEMY_STATUE_PERSON_FIREBALLS_R
#define ENEMY_STATUE_FIREBALL_TIMER(idx) F_ram_04E8_R((idx))
#define ENEMY_JUMPER_TARGET_Y(slot)     F_ram_0444_O2((slot))
#define ENEMY_JUMPER_REVERSALS(slot)    F_ram_0451_O((slot))
#define ENEMY_JUMPER_VSPEED_HI(slot)    F_ram_0412_O((slot))
#define ENEMY_JUMPER_VSPEED_LO(slot)    F_ram_041F_O((slot))
#define ENEMY_JUMPER_SHOVE(slot)        F_ram_00C0_O4((slot))
#define ENEMY_JUMPER_BLOCKED_FLAG       ZP_TMPF
/* Gleeok shared/global state */
#define ENEMY_GLEEOK_NECK_INDEX         F_ENEMY_GLEEOK_NECK_INDEX_R
#define ENEMY_GLEEOK_REF_SEG_DIST       F_ENEMY_GLEEOK_REF_SEG_DIST_R
#define ENEMY_GLEEOK_REF_LIMIT_H_3      F_ENEMY_GLEEOK_REF_LIMIT_H_3_R
#define ENEMY_GLEEOK_REF_LIMIT_V_3      F_ENEMY_GLEEOK_REF_LIMIT_V_3_R
#define ENEMY_GLEEOK_WRITHE_CNTR        F_ram_0510_R
#define ENEMY_GLEEOK_ANIM_CNTR          F_ram_04E6_R
#define ENEMY_GLEEOK_BODY_FRAME         F_ram_04E7_R
#define ENEMY_GLEEOK_DEAD_NECK_MASK     F_ENEMY_GLEEOK_DEAD_NECK_MASK_R
#define ENEMY_GLEEOK_HEAD_X             F_ENEMY_GLEEOK_HEAD_X_R   /* OBJ(0x70, 5) */
#define ENEMY_GLEEOK_HEAD_Y             F_ENEMY_GLEEOK_HEAD_Y_R   /* OBJ(0x84, 5) */
#define ENEMY_GLEEOK_BASE_X             F_ENEMY_GLEEOK_BASE_X_R2   /* OBJ(0x70, 1) */
#define ENEMY_GLEEOK_BASE_Y             F_ENEMY_GLEEOK_BASE_Y_R2   /* OBJ(0x84, 1) */
#define ENEMY_OBJ_SHOVE_DIR(slot)       F_ram_00C0_O4((slot))
#define ENEMY_GOHMA_GO_STRAIGHT(slot)   F_ram_0451_O((slot))
#define ENEMY_GOHMA_DIST_TRAVELED(slot) F_ram_0412_O((slot))   /* aliases ENEMY_PUSH_TIMER */
#define ENEMY_GOHMA_OPEN_EYE_TIMER(slot)   F_ram_0444_O2((slot))   /* aliases ENEMY_AI_STATE */
#define ENEMY_GOHMA_NEXT_OPEN_EYE(slot)    F_ram_042C_O2((slot))   /* aliases ENEMY_TURN_TIMER */
#define ENEMY_GOHMA_SPRINTS(slot)          F_ram_045E_O((slot))   /* aliases ENEMY_BLOATED_TIMER */
#define ENEMY_GOHMA_EYE_FRAME(slot)        F_ram_046B_O2((slot))
#define ENEMY_GOHMA_CLOSED_EYE_CNTR(slot)  F_ram_0478_O2((slot))   /* aliases ENEMY_BOUNCE_FLAGS */
#define ENEMY_GOHMA_SHOOT_TIMER(slot)      F_ram_0380_O((slot))   /* aliases ENEMY_BOSS_HP_PHASE */
#define ENEMY_GOHMA_MOVE_ACCUM(slot)       F_ram_041F_O((slot))   /* aliases ENEMY_AIR_SPEED */
/* Gleeok per-segment data when loaded into the working slot 1..6 area */
#define ENEMY_GLEEOK_NECK_X_PTR_LO      ZP_TMP0
#define ENEMY_GLEEOK_NECK_X_PTR_HI      ZP_TMP1
#define ENEMY_GLEEOK_NECK_Y_PTR_LO      ZP_TMP2
#define ENEMY_GLEEOK_NECK_Y_PTR_HI      ZP_TMP3
#define ENEMY_GLEEOK_NECK_M_PTR_LO      ZP_TMP4
#define ENEMY_GLEEOK_NECK_M_PTR_HI      ZP_TMP5
#define NES_RAM_BASE                    0x0000u  /* RAM() already adds the FF0000 base */
#define ENEMY_MANHANDLA_SEG_DIR(slot)   F_ram_0099_O((slot))
#define ENEMY_DARK_ROOM_FLAG            F_ram_0010_R2
#define ENEMY_THROWER_SLOT              F_ram_0340_R2
#define ENEMY_NEXT_SHOT_SLOT            F_ram_0059_R
#define ENEMY_SHOT_TYPE_SCRATCH         ZP_TMP0
#define ENEMY_SCRATCH_X                 ZP_TMP0
#define ENEMY_SCRATCH_Y                 ZP_TMP1
#define ENEMY_ATTR_SCRATCH              ZP_TMP4
#define ENEMY_FRAME_FLAGS               ZP_TMPF
#define ENEMY_CUR_SPRITE_ATTR_ROW       F_ram_0015_R3
#define ENEMY_DUNGEON_TILE_FLOOR        F_ram_034A_R
#define ENEMY_BOSS_HP_PHASE(slot)       F_ram_0380_O((slot))
#define ENEMY_SECRET_KIND               F_ram_04CD_R2
#define ENEMY_DIGDOGGER_COUNT           F_ENEMY_DIGDOGGER_COUNT_R
#define ENEMY_USED_FLUTE                F_ENEMY_USED_FLUTE_R2

/* Digdogger per-slot vars (Z_04.asm ObjVars.inc:45-51).
 *   $41F speed-frac aliases ENEMY_AIR_SPEED
 *   $42C speed-whole aliases ENEMY_TURN_TIMER
 *   $437 target-frac aliases ENEMY_FLAP_PHASE
 *   $444 target-whole / $45E speed-flag / $46B is-child / $478 cur-part
 *        — fresh slot offsets dedicated to digdogger. */
#define ENEMY_DIGDOGGER_TARGET_SPEED_WHOLE(slot) F_ram_0444_O2((slot))
#define ENEMY_DIGDOGGER_SPEED_FLAG(slot)         F_ram_045E_O((slot))
#define ENEMY_DIGDOGGER_IS_CHILD(slot)           F_ram_046B_O2((slot))
#define ENEMY_DIGDOGGER_CUR_PART(slot)           F_ram_0478_O2((slot))

/* Patra per-slot vars (Z_07.asm Variables.inc).
 *   $0029 ObjTimer+1 — NES quirk: maneuver-change timer is stored at the
 *                     slot+1 position via the LDA ObjTimer+1, X trick.
 *   $0380 ObjAngleFrac
 *   $0394 ObjAngleWhole
 *   $03BC ObjQSpeedFrac (aliases ENEMY_WALK_SPEED — fine, Patra has no walker)
 *   $0412 ObjXFrac (aliases ENEMY_PUSH_TIMER / ENEMY_FLYER_SPEED_FRAC)
 *   $041F ObjYFrac (aliases ENEMY_AIR_SPEED)
 *   $045E Patra_ObjManeuverIndex (aliases ENEMY_BLOATED_TIMER)
 *   $046B Flyer_ObjOffsetX (aliases ENEMY_FLYER_X_FINE)
 *   $0478 Flyer_ObjOffsetY (aliases ENEMY_FLYER_Y_FINE)
 *   $04D1 FlyingMaxSpeedFrac (single global, ENEMY_MAX_AIR_SPEED) */
#define ENEMY_OBJ_TIMER_HI(slot)             F_ram_0029_O((slot))
#define ENEMY_OBJ_ANGLE_FRAC(slot)           F_ram_0380_O((slot))
#define ENEMY_OBJ_ANGLE_WHOLE(slot)          F_ram_0394_O3((slot))
#define ENEMY_OBJ_QSPEED_FRAC(slot)          F_ram_03BC_O2((slot))   /* aliases ENEMY_WALK_SPEED */
#define ENEMY_OBJ_X_FRAC(slot)               F_ram_0412_O((slot))   /* aliases ENEMY_PUSH_TIMER */
#define ENEMY_OBJ_Y_FRAC(slot)               F_ram_041F_O((slot))   /* aliases ENEMY_AIR_SPEED */
#define ENEMY_PATRA_MANEUVER_INDEX(slot)     F_ram_045E_O((slot))   /* aliases ENEMY_BLOATED_TIMER */
#define ENEMY_FLYER_OFFSET_X(slot)           F_ram_046B_O2((slot))   /* aliases ENEMY_FLYER_X_FINE */
#define ENEMY_FLYER_OFFSET_Y(slot)           F_ram_0478_O2((slot))   /* aliases ENEMY_FLYER_Y_FINE */

/* Moldorm per-slot vars (NES Z_04.asm/ObjVars.inc).
 *   $0380 Moldorm_ObjOldDir(slot)    aliases ENEMY_OBJ_ANGLE_FRAC /
 *                                    ENEMY_BOSS_HP_PHASE / ENEMY_GOHMA_SHOOT_TIMER
 *   $03BC Moldorm_ObjBounceDir(slot) aliases ENEMY_WALK_SPEED /
 *                                    ENEMY_OBJ_QSPEED_FRAC
 *   $0485 ObjHP(slot)                aliases ENEMY_CHARGE_SPEED
 *   $04BF ObjAttr(slot)              fresh — boss attribute byte
 *   $034E RoomObjCount               global (single byte) */
#define ENEMY_MOLDORM_OLD_DIR(slot)         F_ram_0380_O((slot))
#define ENEMY_MOLDORM_BOUNCE_DIR(slot)      F_ram_03BC_O2((slot))
#define ENEMY_HP(slot)                      F_ram_0485_O2((slot))
#define ENEMY_ATTR(slot)                    F_ram_04BF_O((slot))
#define ENEMY_ROOM_OBJ_COUNT                F_ram_034E_R

#endif
