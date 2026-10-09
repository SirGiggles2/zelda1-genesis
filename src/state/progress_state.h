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

#define CUR_LEVEL                      RAM(0x0010)
#define FRAME_COUNTER                  RAM(0x0015)
#define MODE_VALUE                     RAM(0x0012)
#define SUBMODE_VALUE                  RAM(0x0013)
#define CUR_ROOM_FLAGS_PTR             RAM(0x00E8)
#define MAP_FLASH_ATTR                 RAM(0x0256)

#define ROOM_TILE_OBJ_0                RAM(0x052B)
#define ROOM_TILE_OBJ_1                RAM(0x052C)
#define ROOM_TILE_OBJ_2                RAM(0x052D)
#define PLAYER_MARKER_DISABLE          RAM(0x0522)
#define POWER_TRIFORCE_FANFARE_FLAG    RAM(0x0509)
/* NES Variables.inc:LastBossDefeated. The old drain called this HUD_DIRTY_FLAG;
 * keep that alias for oracle compatibility, but gameplay uses this name. */
#define LAST_BOSS_DEFEATED             RAM(0x0672)
#define HUD_DIRTY_FLAG                 LAST_BOSS_DEFEATED
#define CURTAIN_LEFT_COL               RAM(0x007C)
#define CURTAIN_RIGHT_COL              RAM(0x007D)
#define CURTAIN_TIMER                  RAM(0x0028)
#define CUR_INV_TILE                   RAM(0x00FE)

#define MAP_MARKER_Y(idx)              RAM(0x0254 + (idx))
#define MAP_MARKER_TILE(idx)           RAM(0x0255 + (idx))
#define MAP_MARKER_ATTR(idx)           RAM(0x0256 + (idx))
#define MAP_MARKER_X(idx)              RAM(0x0257 + (idx))

/* ---- Plan W: save / progress cells -------------------------------------- */
#define PROG_CUR_LEVEL           RAM(NES_CUR_LEVEL)
#define PROG_ITEMS_BY_LEVEL(off) RAM(NES_ITEMS_BY_LEVEL_BASE + (unsigned short)(off))

#endif
