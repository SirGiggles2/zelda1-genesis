#ifndef ROOM_STATE_H
#define ROOM_STATE_H

#include "progress_state.h"
#include "scratch_state.h"
#include "dungeon_state.h"

/* Shared room / doorway / room-flag state for promoted z_05 helpers. */
#define ROOM_TOUCH_DOOR_BITS            ZP_TMPC
#define ROOM_TOUCH_BLOCK_FLAG           ZP_TMPE
#define ROOM_MODE_TIMER                 F_ram_0011_R2
#define ROOM_TRANSFER_BUF_SELECT        F_ram_0014_R2
#define ROOM_SCROLL_FRAME               F_ROOM_SCROLL_FRAME_R
#define ROOM_LEVEL_INDEX                F_ram_00E7_R2
#define ROOM_ROW_INDEX                  F_ram_00E9_R2
#define ROOM_LAST_ROW_INDEX             F_ROOM_LAST_ROW_INDEX_R
#define ROOM_CYCLE_DIR                  F_ROOM_CYCLE_DIR_R
#define ROOM_STATE_INDEX                F_ROOM_STATE_INDEX_R
#define ROOM_VSCROLL_LO                 F_ram_00E2_R2
#define ROOM_SPRITE0_ENABLED            F_ram_00E3_R2
#define ROOM_OPEN_DOOR_TIMER            F_ram_0054_R2
#define ROOM_OPEN_DOOR_ARG              F_ram_0055_R2
#define ROOM_NAMETABLE_SELECT           F_ROOM_NAMETABLE_SELECT_R
#define ROOM_LINK_CELLAR_FLAG           F_ram_005A_R2
#define ROOM_IN_DOORWAY_FLAG            F_ram_0053_R
#define ROOM_HEART_FILL_STATE           F_ram_0063_R2
#define ROOM_INV_OBJ_ACTIVE             F_ram_0064_R2
#define ROOM_DOOR_MASK_ACC             F_ROOM_DOOR_MASK_ACC_R
#define ROOM_BOUNDS(slot)              F_NES_BOUND_LEFT_R3((slot))
#define CUR_OPENED_DOORS               F_ram_00EE_R2
#define ROOM_MONSTER_ALL_DEAD          F_ram_034D_R2
#define ROOM_OW_KILL_COUNT             F_ram_034E_R
#define ROOM_OW_CUR_KILL_TOTAL         F_ram_034F_R2
#define ROOM_MAX_MONSTER_SLOT          F_ram_0340_R2
#define ROOM_OBJ_TYPE(slot)            F_CAVE_ROOM_TYPE_R6((slot))
#define ROOM_OBJ_STUN_TIMER(slot)      F_ram_0406_R2((slot))
#define ROOM_COLLIDABLE_TILE            F_ram_049E_R
#define ROOM_SCROLL_STATE               F_ram_051F_R
#define ROOM_SWORD_BLOCKED_FLAG         F_ram_052E_R
#define ROOM_SCROLL_DIR                LINK_DIR
#define ROOM_SHUTTER_TRIGGERED         F_ram_04CE_R2
#define ROOM_BLOCK_SECRET_FLAG         F_ram_04CF_R2
#define ROOM_SHUTTER_TOUCH_MASK        F_ram_0519_R
#define ROOM_PALETTE_ATTR(slot)        F_ram_0530_R2((slot))
#define ROOM_BOSS_SECRET_FLAG          LAST_BOSS_DEFEATED
#define ROOM_SFX_MAIN                  F_ram_0604_R2
#define ROOM_SFX_AUX                   F_ram_0603_R2
#define ROOM_OAM_BYTE(off)             F_ram_0200_R4((off))
#define ROOM_LINK_BG_ATTR_A            F_ROOM_LINK_BG_ATTR_A_R
#define ROOM_LINK_BG_ATTR_B            F_ROOM_LINK_BG_ATTR_B_R
#define ROOM_INV_OBJ_STATE(slot)       F_ram_00B9_R3((slot))
#define ROOM_PPU_MASK_FLAGS            F_ram_00FF_R2
#define ROOM_SCROLL_LOCK_FLAG          F_ROOM_SCROLL_LOCK_FLAG_R
#define ROOM_MENU_SCROLL_TIMER         F_ram_00E5_R2
#define ROOM_INPUT_DIR                 F_ram_03F8_R2
#define ROOM_LINK_SPEED                F_ram_03BC_R2
#define ROOM_LINK_SPEED_FRAC           F_ram_03A8_R
#define ROOM_OBJECT_SLOT_TYPE          F_ROOM_OBJECT_SLOT_TYPE_R
#define ROOM_OBJECT_INIT_DONE          F_ROOM_OBJECT_INIT_DONE_R
#define ROOM_OBJECT_X_SCRATCH          F_ROOM_OBJECT_X_SCRATCH_R
#define ROOM_OBJECT_Y_SCRATCH          F_ROOM_OBJECT_Y_SCRATCH_R
#define ROOM_PUSH_TIMER                F_ram_0412_R2
#define ROOM_PARTIAL_HEART_FILL_DONE   F_ram_00E0_R2
#define ROOM_MENU_SCROLL_POS           F_ROOM_MENU_SCROLL_POS_R
#define ROOM_TRIFORCE_HOLD_FLAG        F_ROOM_TRIFORCE_HOLD_FLAG_R
#define ROOM_PAUSED_FLAG               F_ram_00E0_R2
#define ROOM_LEVEL_NUMBER_VALUE        F_ram_6BB1_R2

/* ---- Plan W: room transfer + meta cells --------------------------------- */
#define ROOM_TILE_XFER_COL       F_ram_00E8_R2
#define ROOM_TILE_XFER_ROW       F_ram_00E9_R3
#define ROOM_CUR_ROOM_ID         F_ram_00EB_R2
#define ROOM_PPU_MASK_SHADOW     F_ram_00FE_R3
#define ROOM_TILE_XFER_BUF_IDX   F_ram_0301_R
#define ROOM_TILE_XFER_BUF(off)  F_ram_0302_R5((unsigned short)(off))
#define ROOM_ID_ALT              F_ROOM_ID_ALT_R
#define ROOM_HISTORY_IDX         F_ROOM_HISTORY_IDX_R
#define ROOM_HISTORY(i)          F_NES_ROOM_HISTORY_BASE_R((unsigned char)(i))
#define PLAY_AREA(i)             F_NES_PLAY_AREA_BASE_6530_X4((unsigned short)(i))

#endif
