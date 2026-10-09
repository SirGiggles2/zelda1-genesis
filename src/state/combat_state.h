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

#define LINK_ACTION_TIMER               RAM(0x00AC)
#define LINK_DIR                        RAM(0x0098)
#define LINK_DAMAGE_DISABLE_FLAG        RAM(0x0512)
#define LINK_STUN_TIMER                 RAM(0x04F0)
#define LINK_RING_LEVEL                 RAM(0x0662)
#define LINK_HEARTS                     RAM(0x066F)
#define LINK_PARTIAL_HEART              RAM(0x0670)
#define LINK_SHIELD_BLOCK_FLAG          RAM(0x0676)

#define ROOM_KILL_COUNT                 RAM(0x0627)
#define ROOM_CHAIN_KILL_COUNT           RAM(0x0050)
#define ROOM_CHAIN_KILL_BONUS           RAM(0x0051)
#define ROOM_MONSTER_COLLISION_COUNT    RAM(0x034B)
#define SFX_COMBAT                      RAM(0x0604)

/* Canonical owner: object_state.h (same cell, NES ObjState $AC+slot). */
#ifndef OBJ_STATE
#define OBJ_STATE(slot)                 OBJ(0x00AC, (slot))
#endif
#define OBJ_DIR(slot)                   OBJ(0x0098, (slot))
#define OBJ_X(slot)                     OBJ(NES_OBJ_X, (slot))
#define OBJ_Y(slot)                     OBJ(NES_OBJ_Y, (slot))
#define OBJ_GRID_OFFSET(slot)           OBJ(NES_OBJ_GRID_OFFSET, (slot))
#define OBJ_ANIM_TIMER(slot)            OBJ(0x03D0, (slot))

#define MON_TYPE(slot)                  OBJ(NES_OBJ_TYPE, (slot))
#define MON_STATUS_FLAGS(slot)          OBJ(0x04BF, (slot))
#define MON_INVINCIBILITY(slot)         OBJ(0x04B2, (slot))
#define MON_HIT_REACTION(slot)          OBJ(0x04F0, (slot))
#define MON_METASTATE(slot)             OBJ(0x0405, (slot))
#define MON_STUN_TIMER(slot)            OBJ(0x003D, (slot))
#define MON_SHOVE_DIR(slot)             OBJ(0x00C0, (slot))
#define MON_SHOVE_TIMER(slot)           OBJ(0x00D3, (slot))
#define MON_HP(slot)                    OBJ(0x0485, (slot))
#define MON_SUBSTATE(slot)              OBJ(0x046B, (slot))
#define MON_BOUNCE_TURNS(slot)          OBJ(0x042C, (slot))

#define ITEM_SWORD_LEVEL                RAM(0x0657)
#define ITEM_ARROW_OR_ROD_LEVEL         RAM(0x0659)

#endif
