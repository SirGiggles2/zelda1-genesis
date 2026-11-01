#ifndef OBJECT_STATE_H
#define OBJECT_STATE_H

#include "platform_abi.h"

/* Per-slot object-state cell bases. Index with `+ slot` at the call site.
 * Owned by object_runtime; any new offset touched by object logic belongs here.
 */

/* Position / tile */
#define OBJ_TILE_X(slot)        F_ram_0070_R6((slot))
#define OBJ_TILE_Y(slot)        F_ram_0084_R6((slot))
#define OBJ_TILE_NEXT(slot)     F_ram_049E_R3((slot))
#define OBJ_ALIGN_FLAG(slot)    F_ram_0394_R5((slot))

/* Type / state */
#define OBJ_TYPE(slot)          F_ram_034F_R6((slot))
#define OBJ_FLAG(slot)          F_ram_0098_R4((slot))
#ifdef OBJ_STATE
#undef OBJ_STATE   /* identical cell from world/combat_state.h; this header owns it */
#endif
#define OBJ_STATE(slot)         F_ram_00AC_R6((slot))
#define OBJ_METASTATE(slot)     F_NES_OBJ_METASTATE_BASE_R2((slot))

/* Shove */
#define OBJ_SHOVE_DIR(slot)     F_ram_00C0_R3((slot))
#define OBJ_SHOVE_DIST(slot)    F_ram_00D3_R3((slot))

/* Animation */
#define OBJ_ANIM_CNTR(slot)     F_ram_03D0_R3((slot))
#define OBJ_HFLIP(slot)         F_ram_03E4_R3((slot))

/* Invulnerability */
#define OBJ_INV_TIMER(slot)     F_ram_04F0_R4((slot))

#endif /* OBJECT_STATE_H */
