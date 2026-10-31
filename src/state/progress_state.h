#ifndef PROGRESS_STATE_H
#define PROGRESS_STATE_H

#include "world_state.h"
#include "item_state.h"
#include "combat_state.h"
#include "scratch_state.h"

/* Shared state for save/progress/map/curtain owned C.
 *
 * SAVEFILE_* zero-page bytes are scratch-time uses of $00/$01/$08/$09;
 * aliased through scratch_state.h canonical names. */
#define SAVEFILE_PTR_LO                 ZP_TMP0
#define SAVEFILE_PTR_HI                 ZP_TMP1
#define SAVEFILE_MASK_LO                ZP_TMP8
#define SAVEFILE_MASK_HI                ZP_TMP9

#define CUR_LEVEL                      F_ram_0010_R2
#define FRAME_COUNTER                  F_ram_0015_R3
#define MODE_VALUE                     F_ram_0012_R3
#define SUBMODE_VALUE                  F_ram_0013_R3
#define CUR_ROOM_FLAGS_PTR             F_ram_00E8_R
#define MAP_FLASH_ATTR                 F_MAP_FLASH_ATTR_R

#define ROOM_TILE_OBJ_0                F_ROOM_TILE_OBJ_0_R2
#define ROOM_TILE_OBJ_1                F_ROOM_TILE_OBJ_1_R2
#define ROOM_TILE_OBJ_2                F_ROOM_TILE_OBJ_2_R2
#define PLAYER_MARKER_DISABLE          F_ram_0522_R2
#define POWER_TRIFORCE_FANFARE_FLAG    F_POWER_TRIFORCE_FANFARE_FLAG_R
/* NES Variables.inc:LastBossDefeated. The old drain called this HUD_DIRTY_FLAG;
 * keep that alias for oracle compatibility, but gameplay uses this name. */
#define LAST_BOSS_DEFEATED             F_LAST_BOSS_DEFEATED_R
#define HUD_DIRTY_FLAG                 LAST_BOSS_DEFEATED
#define CURTAIN_LEFT_COL               F_ram_007C_R2
#define CURTAIN_RIGHT_COL              F_ram_007D_R2
#define CURTAIN_TIMER                  F_ram_0028_R2
#define CUR_INV_TILE                   F_ram_00FE_R2

#define MAP_MARKER_Y(idx)              F_ram_0254_R((idx))
#define MAP_MARKER_TILE(idx)           F_ram_0255_R((idx))
#define MAP_MARKER_ATTR(idx)           F_MAP_FLASH_ATTR_R2((idx))
#define MAP_MARKER_X(idx)              F_ram_0257_R((idx))

/* ---- Plan W: save / progress cells -------------------------------------- */
#define PROG_CUR_LEVEL           F_ram_0010_R3
#define PROG_ITEMS_BY_LEVEL(off) F_ram_0657_R7((unsigned short)(off))

#endif
