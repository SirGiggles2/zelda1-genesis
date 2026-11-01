#ifndef TRAP_STATE_H
#define TRAP_STATE_H

#include "progress_state.h"
#include "enemy_state.h"

/* Shared state for trap/whirlwind/passive-tile owned C. */
#define TRAP_OBJ_TYPE                  73u
#define TRAP_ALT_OBJ_TYPE              30u
#define TRAP_PUSHED_OBJ_TYPE           34u

#define WHIRLWIND_ACTIVE_FLAG          F_WHIRLWIND_ACTIVE_FLAG_R
#define TELEPORT_ACTIVE_FLAG           F_ram_0522_R2
#define TELEPORT_LEVEL_INDEX           F_TELEPORT_LEVEL_INDEX_R
#define WHIRLWIND_PREV_ROOM_ID         F_WHIRLWIND_PREV_ROOM_ID_R

#define LINK_CELLAR_FLAG               F_ram_005A_R2
#define MODE_TIMER                     F_ram_0011_R2
/* SUBMODE_VALUE comes from progress_state.h (already included above). */

#define OAM_HIDE_2                     F_OAM_HIDE_2_R
#define OAM_HIDE_3                     F_OAM_HIDE_3_R

#define RUPEE_STASH_FLAG               F_ram_034E_R
#define TRAP_BASE_SLOT                 F_ram_0340_R2
#define PASSIVE_OBJ_FLAG               F_ram_03F8_R2

#define TRAP_RETURN_COORD(slot)        F_ram_0380_O((slot))

#endif
