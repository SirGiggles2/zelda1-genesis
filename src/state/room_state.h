#ifndef ROOM_STATE_H
#define ROOM_STATE_H

#include "progress_state.h"
#include "scratch_state.h"
#include "dungeon_state.h"

/* Shared room / doorway / room-flag state for promoted z_05 helpers. */
#define ROOM_TOUCH_DOOR_BITS            ZP_TMPC
#define ROOM_TOUCH_BLOCK_FLAG           ZP_TMPE
#define ROOM_MODE_TIMER                 RAM(0x0011)
#define ROOM_TRANSFER_BUF_SELECT        RAM(0x0014)
#define ROOM_SCROLL_FRAME               RAM(0x00E6)
#define ROOM_LEVEL_INDEX                RAM(0x00E7)
#define ROOM_ROW_INDEX                  RAM(0x00E9)
#define ROOM_LAST_ROW_INDEX             RAM(0x00ED)
#define ROOM_CYCLE_DIR                  RAM(0x00EF)
#define ROOM_STATE_INDEX                RAM(0x00E1)
#define ROOM_VSCROLL_LO                 RAM(0x00E2)
#define ROOM_SPRITE0_ENABLED            RAM(0x00E3)
#define ROOM_OPEN_DOOR_TIMER            RAM(0x0054)
#define ROOM_OPEN_DOOR_ARG              RAM(0x0055)
#define ROOM_NAMETABLE_SELECT           RAM(0x005F)
#define ROOM_LINK_CELLAR_FLAG           RAM(0x005A)
#define ROOM_IN_DOORWAY_FLAG            RAM(0x0053)
#define ROOM_HEART_FILL_STATE           RAM(0x0063)
#define ROOM_INV_OBJ_ACTIVE             RAM(0x0064)
#define ROOM_DOOR_MASK_ACC             RAM(0x033F)
#define ROOM_BOUNDS(slot)              RAM(0x0346 + (slot))
#define CUR_OPENED_DOORS               RAM(0x00EE)
#define ROOM_MONSTER_ALL_DEAD          RAM(0x034D)
#define ROOM_OW_KILL_COUNT             RAM(0x034E)
#define ROOM_OW_CUR_KILL_TOTAL         RAM(0x034F)
#define ROOM_MAX_MONSTER_SLOT          RAM(0x0340)
#define ROOM_OBJ_TYPE(slot)            RAM(0x0350 + (slot))
#define ROOM_OBJ_STUN_TIMER(slot)      RAM(0x0406 + (slot))
#define ROOM_COLLIDABLE_TILE            RAM(0x049E)
#define ROOM_SCROLL_STATE               RAM(0x051F)
#define ROOM_SWORD_BLOCKED_FLAG         RAM(0x052E)
#define ROOM_SCROLL_DIR                LINK_DIR
#define ROOM_SHUTTER_TRIGGERED         RAM(0x04CE)
#define ROOM_BLOCK_SECRET_FLAG         RAM(0x04CF)
#define ROOM_SHUTTER_TOUCH_MASK        RAM(0x0519)
#define ROOM_PALETTE_ATTR(slot)        RAM(0x0530 + (slot))
#define ROOM_BOSS_SECRET_FLAG          LAST_BOSS_DEFEATED
#define ROOM_SFX_MAIN                  RAM(0x0604)
#define ROOM_SFX_AUX                   RAM(0x0603)
#define ROOM_OAM_BYTE(off)             RAM(0x0200 + (off))
#define ROOM_LINK_BG_ATTR_A            RAM(0x024A)
#define ROOM_LINK_BG_ATTR_B            RAM(0x024E)
#define ROOM_INV_OBJ_STATE(slot)       RAM(0x00B9 + (slot))
#define ROOM_PPU_MASK_FLAGS            RAM(0x00FF)
#define ROOM_SCROLL_LOCK_FLAG          RAM(0x010C)
#define ROOM_MENU_SCROLL_TIMER         RAM(0x00E5)
#define ROOM_INPUT_DIR                 RAM(0x03F8)
#define ROOM_LINK_SPEED                RAM(0x03BC)
#define ROOM_LINK_SPEED_FRAC           RAM(0x03A8)
#define ROOM_OBJECT_SLOT_TYPE          RAM(0x035A)
#define ROOM_OBJECT_INIT_DONE          RAM(0x00B7)
#define ROOM_OBJECT_X_SCRATCH          RAM(0x007B)
#define ROOM_OBJECT_Y_SCRATCH          RAM(0x008F)
#define ROOM_PUSH_TIMER                RAM(0x0412)
#define ROOM_PARTIAL_HEART_FILL_DONE   RAM(0x00E0)
#define ROOM_MENU_SCROLL_POS           RAM(0x005E)
#define ROOM_TRIFORCE_HOLD_FLAG        RAM(0x0619)
#define ROOM_PAUSED_FLAG               RAM(0x00E0)
#define ROOM_LEVEL_NUMBER_VALUE        RAM(0x6000u + 0x0BB1)

/* ---- Plan W: room transfer + meta cells --------------------------------- */
#define ROOM_TILE_XFER_COL       RAM(NES_TILE_XFER_COL)
#define ROOM_TILE_XFER_ROW       RAM(NES_TILE_XFER_ROW)
#define ROOM_CUR_ROOM_ID         RAM(NES_CUR_ROOM_ID)
#define ROOM_PPU_MASK_SHADOW     RAM(NES_PPU_MASK_SHADOW)
#define ROOM_TILE_XFER_BUF_IDX   RAM(NES_TILE_XFER_BUF_IDX)
#define ROOM_TILE_XFER_BUF(off)  RAM(NES_TILE_XFER_BUF_BASE + (unsigned short)(off))
#define ROOM_ID_ALT              RAM(NES_ROOM_ID_ALT)
#define ROOM_HISTORY_IDX         RAM(NES_ROOM_HISTORY_IDX)
#define ROOM_HISTORY(i)          RAM(NES_ROOM_HISTORY_BASE + (unsigned char)(i))
#define PLAY_AREA(i)             nes_ram[NES_PLAY_AREA_BASE + (unsigned short)(i)]

#endif
