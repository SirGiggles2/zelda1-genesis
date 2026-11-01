#ifndef COMBAT_STATE_H
#define COMBAT_STATE_H

#include "platform_abi.h"
#include "scratch_state.h"

/* Shared state map for combat/collision/link-collision owned C. Keep
 * the scratch/collision boundary named so raw offsets stop spreading.
 *
 * Zero-page scratch slots aliased through scratch_state.h canonical names.
 */
#define COMBAT_WEAPON_SLOT              ZP_TMP0
#define COMBAT_HITBOX_X                 ZP_TMP4
#define COMBAT_HITBOX_Y                 ZP_TMP5
#define COMBAT_COLLIDED                 ZP_TMP6
#define COMBAT_DAMAGE_AMOUNT            ZP_TMP7
#define COMBAT_SHOVE_DIR                ZP_TMP8
#define COMBAT_DAMAGE_TYPE              ZP_TMP9
#define COMBAT_ABS_DX                   ZP_TMPA
#define COMBAT_ABS_DY                   ZP_TMPB
#define COMBAT_HARM_FLAG                ZP_TMPC
#define COMBAT_THRESHOLD_X              ZP_TMPD
#define COMBAT_THRESHOLD_Y              ZP_TMPE
#define COMBAT_PART_INDEX               ZP_TMPF

#define LINK_ACTION_TIMER               F_ram_00AC_R3
#define LINK_DIR                        F_ram_0098_R2
#define LINK_DAMAGE_DISABLE_FLAG        F_ram_0512_R2
#define LINK_STUN_TIMER                 F_ram_04F0_R2
#define LINK_RING_LEVEL                 F_ram_0662_R
#define LINK_HEARTS                     F_ram_066F_R2
#define LINK_PARTIAL_HEART              F_ram_0670_R2
#define LINK_SHIELD_BLOCK_FLAG          F_ram_0676_R2

#define ROOM_KILL_COUNT                 F_ram_0627_R
#define ROOM_CHAIN_KILL_COUNT           F_ram_0050_R
#define ROOM_CHAIN_KILL_BONUS           F_ram_0051_R
#define ROOM_MONSTER_COLLISION_COUNT    F_ROOM_MONSTER_COLLISION_COUNT_R
#define SFX_COMBAT                      F_ram_0604_R2

/* Canonical owner: object_state.h (same cell, NES ObjState $AC+slot). */
#ifndef OBJ_STATE
#define OBJ_STATE(slot)                 F_ram_00AC_O2((slot))
#endif
#define OBJ_DIR(slot)                   F_ram_0098_O3((slot))
#define OBJ_X(slot)                     F_ram_0070_O2((slot))
#define OBJ_Y(slot)                     F_ram_0084_O2((slot))
#define OBJ_GRID_OFFSET(slot)           F_ram_0394_O((slot))
#define OBJ_ANIM_TIMER(slot)            F_ram_03D0_O2((slot))

#define MON_TYPE(slot)                  F_ram_034F_O2((slot))
#define MON_STATUS_FLAGS(slot)          F_ram_04BF_O((slot))
#define MON_INVINCIBILITY(slot)         F_ram_04B2_O2((slot))
#define MON_HIT_REACTION(slot)          F_ram_04F0_O3((slot))
#define MON_METASTATE(slot)             F_NES_OBJ_METASTATE_BASE_O((slot))
#define MON_STUN_TIMER(slot)            F_NES_OBJ_STUN_TIMER_O3((slot))
#define MON_SHOVE_DIR(slot)             F_ram_00C0_O4((slot))
#define MON_SHOVE_TIMER(slot)           F_ram_00D3_O3((slot))
#define MON_HP(slot)                    F_ram_0485_O2((slot))
#define MON_SUBSTATE(slot)              F_ram_046B_O2((slot))
#define MON_BOUNCE_TURNS(slot)          F_ram_042C_O2((slot))

#define ITEM_SWORD_LEVEL                F_ram_0657_R
#define ITEM_ARROW_OR_ROD_LEVEL         F_ram_0659_R2

#endif
