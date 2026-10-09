#ifndef ENEMY_STATE_H
#define ENEMY_STATE_H

#include "platform_abi.h"
#include "scratch_state.h"

/* Enemy/object state map for promoted z_04 gameplay code. Keep names
 * close to gameplay meaning so owned C stops depending on raw offsets.
 */
#define ENEMY_DIR(slot)                 OBJ(0x0098, (slot))
#define ENEMY_STATE_TIMER(slot)         OBJ(0x00AC, (slot))
#define ENEMY_LIFE(slot)                OBJ(0x00BF, (slot))
#define ENEMY_X(slot)                   OBJ(NES_OBJ_X, (slot))
#define ENEMY_Y(slot)                   OBJ(NES_OBJ_Y, (slot))
#define ENEMY_RNG_A(slot)               OBJ(0x0018, (slot))
#define ENEMY_RNG_B(slot)               OBJ(0x0019, (slot))
#define ENEMY_MOVE_TIMER(slot)          OBJ(0x0028, (slot))
#define ENEMY_STUN_TIMER(slot)          OBJ(0x003D, (slot))
#define ENEMY_TYPE(slot)                OBJ(NES_OBJ_TYPE, (slot))
#define ENEMY_METASTATE(slot)           OBJ(0x0405, (slot))
#define ENEMY_PUSH_TIMER(slot)          OBJ(0x0412, (slot))
#define ENEMY_FLYER_SPEED_FRAC(slot)    OBJ(0x0412, (slot))
#define ENEMY_AIR_SPEED(slot)           OBJ(0x041F, (slot))
#define ENEMY_TURN_TIMER(slot)          OBJ(0x042C, (slot))
#define ENEMY_FLAP_PHASE(slot)          OBJ(0x0437, (slot))
#define ENEMY_AI_STATE(slot)            OBJ(0x0444, (slot))
#define ENEMY_BLOATED_TIMER(slot)       OBJ(0x045E, (slot))
#define ENEMY_FLYER_X_FINE(slot)        OBJ(0x046B, (slot))
#define ENEMY_BOUNCE_FLAGS(slot)        OBJ(0x0478, (slot))
#define ENEMY_FLYER_Y_FINE(slot)        OBJ(0x0478, (slot))
#define ENEMY_CHARGE_SPEED(slot)        OBJ(0x0485, (slot))
#define ENEMY_COLLIDED_TILE(slot)       OBJ(0x049E, (slot))
#define ENEMY_INVINCIBILITY(slot)       OBJ(0x04B2, (slot))
#define ENEMY_MAX_AIR_SPEED             RAM(0x04D1)
#define ENEMY_HIT_REACTION(slot)        OBJ(0x04F0, (slot))
#define ENEMY_ALIVE_FLAG(slot)          OBJ(0x0492, (slot))
#define ENEMY_DRAW_FRAME(slot)          OBJ(0x03E4, (slot))
#define ENEMY_WALK_SPEED(slot)          OBJ(0x03BC, (slot))
#define ENEMY_ANIM_TIMER(slot)          OBJ(0x03D0, (slot))
#define ENEMY_PUSH_DIR_SCRATCH(slot)    OBJ(0x03F8, (slot))
#define ENEMY_SHOT_COUNT                RAM(0x034C)
#define ENEMY_ROOM_MONSTER_FLAG         RAM(0x0514)
#define ENEMY_OAM_HIDE_0                RAM(0x0240)
#define ENEMY_OAM_HIDE_1                RAM(0x0244)
#define ENEMY_SFX_SECRET                RAM(0x0602)
#define ENEMY_SFX_BOSS_CRY              RAM(0x0601)
#define ENEMY_SFX_BOSS_CRY_FLAGS        RAM(0x0603)
#define ENEMY_SFX_PARRY                 RAM(0x0604)
#define ENEMY_BOULDER_SET_COUNT         RAM(0x0515)
#define ENEMY_CANDLE_FADE_TRIGGER       RAM(0x051C)
#define ENEMY_CANDLE_FADE_PHASE         RAM(0x051E)
#define ENEMY_CANDLE_STATE              RAM(0x051F)
#define ENEMY_CANDLE_ROOM_ID            RAM(0x00EB)
#define ENEMY_PAUSE_FLAG                RAM(0x066C)
#define ENEMY_FREEZE_FLAG               RAM(0x0506)
#define ENEMY_COLLISION_FLAG            ZP_TMP6
#define ENEMY_BLOCKED_FLAG              ZP_TMPE
#define ENEMY_BUBBLE_EFFECT             RAM(0x004C)
#define ENEMY_LEEVER_TIMER              RAM(0x004D)
#define ENEMY_BUBBLE_STATUS             RAM(0x052E)
/* NES ChaseTargetX/Y ($61/$62): where monsters head. Usually Link's
 * position, but UpdateMode5Play substitutes a decoy point while
 * ChaseOtherTarget ($60) is set. Previously named LINK_X/LINK_Y behind
 * #ifndef, so TUs that also included world_state.h (LINK_X = ObjX $70)
 * silently got the other meaning (T-093). */
#define CHASE_TARGET_X                  RAM(0x0061)
#define CHASE_TARGET_Y                  RAM(0x0062)
/* SAVE_SLOT_INDEX is defined in item_state.h (canonical owner). Pulled
 * in transitively via the include below so existing enemy_*_runtime.c
 * consumers don't need their own #include. */
#include "item_state.h"
#define SAVE_SLOT_QUEST(slot)           RAM(0x062D + (slot))
#define ENEMY_GLEEOK_HEAD_TIMER         RAM(0x0418)
#define ENEMY_GLEEOK_SEG_X(slot)        OBJ(0x0072, (slot))
#define ENEMY_GLEEOK_SEG_X_TARGET(slot) OBJ(0x0073, (slot))
#define ENEMY_GLEEOK_SEG_Y(slot)        OBJ(0x0086, (slot))
#define ENEMY_GLEEOK_SEG_Y_TARGET(slot) OBJ(0x0087, (slot))
#define ENEMY_MANHANDLA_SEGMENT_DIED_FLAG RAM(0x0383)
#define ENEMY_MANHANDLA_FRAME_ACCUM(slot) OBJ(0x0451, (slot))
#define ENEMY_MANHANDLA_FRAME_ATTR(slot)  OBJ(0x0478, (slot))
#define ENEMY_LAMNOLA_SPEED             RAM(0x04E6)
#define ENEMY_LAMNOLA_TYPE              RAM(0x04E7)
#define ENEMY_LAMNOLA_VIABLE_DIR_MASK   RAM(0x050F)
#define ENEMY_PLAYER_OBJ_X              RAM(0x0070)
#define ENEMY_PLAYER_OBJ_Y              RAM(0x0084)
#define ENEMY_VIRE_SPLIT_TYPE           ZP_TMP0
#define ENEMY_STATUE_PERSON_FIREBALLS   RAM(0x04CC)
#define ENEMY_STATUE_FIREBALL_TIMER(idx) RAM(0x04E8 + (idx))
#define ENEMY_JUMPER_TARGET_Y(slot)     OBJ(0x0444, (slot))
#define ENEMY_JUMPER_REVERSALS(slot)    OBJ(0x0451, (slot))
#define ENEMY_JUMPER_VSPEED_HI(slot)    OBJ(0x0412, (slot))
#define ENEMY_JUMPER_VSPEED_LO(slot)    OBJ(0x041F, (slot))
#define ENEMY_JUMPER_SHOVE(slot)        OBJ(0x00C0, (slot))
#define ENEMY_JUMPER_BLOCKED_FLAG       ZP_TMPF
/* Gleeok shared/global state */
#define ENEMY_GLEEOK_NECK_INDEX         RAM(0x04D7)
#define ENEMY_GLEEOK_REF_SEG_DIST       RAM(0x04D8)
#define ENEMY_GLEEOK_REF_LIMIT_H_3      RAM(0x04DD)
#define ENEMY_GLEEOK_REF_LIMIT_V_3      RAM(0x04DE)
#define ENEMY_GLEEOK_WRITHE_CNTR        RAM(0x0510)
#define ENEMY_GLEEOK_ANIM_CNTR          RAM(0x04E6)
#define ENEMY_GLEEOK_BODY_FRAME         RAM(0x04E7)
#define ENEMY_GLEEOK_DEAD_NECK_MASK     RAM(0x0511)
#define ENEMY_GLEEOK_HEAD_X             RAM(0x0075)   /* OBJ(0x70, 5) */
#define ENEMY_GLEEOK_HEAD_Y             RAM(0x0089)   /* OBJ(0x84, 5) */
#define ENEMY_GLEEOK_BASE_X             RAM(0x0071)   /* OBJ(0x70, 1) */
#define ENEMY_GLEEOK_BASE_Y             RAM(0x0085)   /* OBJ(0x84, 1) */
#define ENEMY_OBJ_SHOVE_DIR(slot)       OBJ(0x00C0, (slot))
#define ENEMY_GOHMA_GO_STRAIGHT(slot)   OBJ(0x0451, (slot))
#define ENEMY_GOHMA_DIST_TRAVELED(slot) OBJ(0x0412, (slot))   /* aliases ENEMY_PUSH_TIMER */
#define ENEMY_GOHMA_OPEN_EYE_TIMER(slot)   OBJ(0x0444, (slot))   /* aliases ENEMY_AI_STATE */
#define ENEMY_GOHMA_NEXT_OPEN_EYE(slot)    OBJ(0x042C, (slot))   /* aliases ENEMY_TURN_TIMER */
#define ENEMY_GOHMA_SPRINTS(slot)          OBJ(0x045E, (slot))   /* aliases ENEMY_BLOATED_TIMER */
#define ENEMY_GOHMA_EYE_FRAME(slot)        OBJ(0x046B, (slot))
#define ENEMY_GOHMA_CLOSED_EYE_CNTR(slot)  OBJ(0x0478, (slot))   /* aliases ENEMY_BOUNCE_FLAGS */
#define ENEMY_GOHMA_SHOOT_TIMER(slot)      OBJ(0x0380, (slot))   /* aliases ENEMY_BOSS_HP_PHASE */
#define ENEMY_GOHMA_MOVE_ACCUM(slot)       OBJ(0x041F, (slot))   /* aliases ENEMY_AIR_SPEED */
/* Gleeok per-segment data when loaded into the working slot 1..6 area */
#define ENEMY_GLEEOK_NECK_X_PTR_LO      ZP_TMP0
#define ENEMY_GLEEOK_NECK_X_PTR_HI      ZP_TMP1
#define ENEMY_GLEEOK_NECK_Y_PTR_LO      ZP_TMP2
#define ENEMY_GLEEOK_NECK_Y_PTR_HI      ZP_TMP3
#define ENEMY_GLEEOK_NECK_M_PTR_LO      ZP_TMP4
#define ENEMY_GLEEOK_NECK_M_PTR_HI      ZP_TMP5
#define NES_RAM_BASE                    0x0000u  /* RAM() already adds the FF0000 base */
#define ENEMY_MANHANDLA_SEG_DIR(slot)   OBJ(0x0099, (slot))
#define ENEMY_DARK_ROOM_FLAG            RAM(0x0010)
#define ENEMY_THROWER_SLOT              RAM(0x0340)
#define ENEMY_NEXT_SHOT_SLOT            RAM(0x0059)
#define ENEMY_SHOT_TYPE_SCRATCH         ZP_TMP0
#define ENEMY_SCRATCH_X                 ZP_TMP0
#define ENEMY_SCRATCH_Y                 ZP_TMP1
#define ENEMY_ATTR_SCRATCH              ZP_TMP4
#define ENEMY_FRAME_FLAGS               ZP_TMPF
#define ENEMY_CUR_SPRITE_ATTR_ROW       RAM(0x0015)
#define ENEMY_DUNGEON_TILE_FLOOR        RAM(0x034A)
#define ENEMY_BOSS_HP_PHASE(slot)       OBJ(0x0380, (slot))
#define ENEMY_SECRET_KIND               RAM(0x04CD)
#define ENEMY_DIGDOGGER_COUNT           RAM(0x0507)
#define ENEMY_USED_FLUTE                RAM(0x051B)

/* Digdogger per-slot vars (Z_04.asm ObjVars.inc:45-51).
 *   $41F speed-frac aliases ENEMY_AIR_SPEED
 *   $42C speed-whole aliases ENEMY_TURN_TIMER
 *   $437 target-frac aliases ENEMY_FLAP_PHASE
 *   $444 target-whole / $45E speed-flag / $46B is-child / $478 cur-part
 *        — fresh slot offsets dedicated to digdogger. */
#define ENEMY_DIGDOGGER_TARGET_SPEED_WHOLE(slot) OBJ(0x0444, (slot))
#define ENEMY_DIGDOGGER_SPEED_FLAG(slot)         OBJ(0x045E, (slot))
#define ENEMY_DIGDOGGER_IS_CHILD(slot)           OBJ(0x046B, (slot))
#define ENEMY_DIGDOGGER_CUR_PART(slot)           OBJ(0x0478, (slot))

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
#define ENEMY_OBJ_TIMER_HI(slot)             OBJ(0x0029, (slot))
#define ENEMY_OBJ_ANGLE_FRAC(slot)           OBJ(0x0380, (slot))
#define ENEMY_OBJ_ANGLE_WHOLE(slot)          OBJ(0x0394, (slot))
#define ENEMY_OBJ_QSPEED_FRAC(slot)          OBJ(0x03BC, (slot))   /* aliases ENEMY_WALK_SPEED */
#define ENEMY_OBJ_X_FRAC(slot)               OBJ(0x0412, (slot))   /* aliases ENEMY_PUSH_TIMER */
#define ENEMY_OBJ_Y_FRAC(slot)               OBJ(0x041F, (slot))   /* aliases ENEMY_AIR_SPEED */
#define ENEMY_PATRA_MANEUVER_INDEX(slot)     OBJ(0x045E, (slot))   /* aliases ENEMY_BLOATED_TIMER */
#define ENEMY_FLYER_OFFSET_X(slot)           OBJ(0x046B, (slot))   /* aliases ENEMY_FLYER_X_FINE */
#define ENEMY_FLYER_OFFSET_Y(slot)           OBJ(0x0478, (slot))   /* aliases ENEMY_FLYER_Y_FINE */

/* Moldorm per-slot vars (NES Z_04.asm/ObjVars.inc).
 *   $0380 Moldorm_ObjOldDir(slot)    aliases ENEMY_OBJ_ANGLE_FRAC /
 *                                    ENEMY_BOSS_HP_PHASE / ENEMY_GOHMA_SHOOT_TIMER
 *   $03BC Moldorm_ObjBounceDir(slot) aliases ENEMY_WALK_SPEED /
 *                                    ENEMY_OBJ_QSPEED_FRAC
 *   $0485 ObjHP(slot)                aliases ENEMY_CHARGE_SPEED
 *   $04BF ObjAttr(slot)              fresh — boss attribute byte
 *   $034E RoomObjCount               global (single byte) */
#define ENEMY_MOLDORM_OLD_DIR(slot)         OBJ(0x0380, (slot))
#define ENEMY_MOLDORM_BOUNCE_DIR(slot)      OBJ(0x03BC, (slot))
#define ENEMY_HP(slot)                      OBJ(0x0485, (slot))
#define ENEMY_ATTR(slot)                    OBJ(0x04BF, (slot))
#define ENEMY_ROOM_OBJ_COUNT                RAM(0x034E)

#endif
