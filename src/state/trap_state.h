#ifndef TRAP_STATE_H
#define TRAP_STATE_H

#include "progress_state.h"
#include "enemy_state.h"

/* Shared state for trap/whirlwind/passive-tile owned C. */
#define TRAP_OBJ_TYPE                  73u
#define TRAP_ALT_OBJ_TYPE              30u
#define TRAP_PUSHED_OBJ_TYPE           34u

#define WHIRLWIND_ACTIVE_FLAG          RAM(0x0508)
#define TELEPORT_ACTIVE_FLAG           RAM(0x0522)
#define TELEPORT_LEVEL_INDEX           RAM(0x0523)
#define WHIRLWIND_PREV_ROOM_ID         RAM(0x00EA)

#define LINK_CELLAR_FLAG               RAM(0x005A)
#define MODE_TIMER                     RAM(0x0011)
/* SUBMODE_VALUE comes from progress_state.h (already included above). */

#define OAM_HIDE_2                     RAM(0x0248)
#define OAM_HIDE_3                     RAM(0x024C)

#define RUPEE_STASH_FLAG               RAM(0x034E)
#define TRAP_BASE_SLOT                 RAM(0x0340)
#define PASSIVE_OBJ_FLAG               RAM(0x03F8)

#define TRAP_RETURN_COORD(slot)        OBJ(0x0380, (slot))

#endif
