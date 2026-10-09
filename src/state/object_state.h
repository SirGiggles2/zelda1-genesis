#ifndef OBJECT_STATE_H
#define OBJECT_STATE_H

#include "platform_abi.h"

/* Per-slot object-state cell bases. Index with `+ slot` at the call site.
 * Owned by object_runtime; any new offset touched by object logic belongs here.
 */

/* Position / tile */
#define OBJ_TILE_X(slot)        RAM(NES_OBJ_TILE_X_BASE + (slot))
#define OBJ_TILE_Y(slot)        RAM(NES_OBJ_TILE_Y_BASE + (slot))
#define OBJ_TILE_NEXT(slot)     RAM(NES_OBJ_TILE_NEXT_BASE + (slot))
#define OBJ_ALIGN_FLAG(slot)    RAM(NES_OBJ_ALIGN_FLAG_BASE + (slot))

/* Type / state */
#define OBJ_TYPE(slot)          RAM(NES_OBJ_TYPE_BASE + (slot))
#define OBJ_FLAG(slot)          RAM(NES_OBJ_FLAG_BASE + (slot))
#ifdef OBJ_STATE
#undef OBJ_STATE   /* identical cell from world/combat_state.h; this header owns it */
#endif
#define OBJ_STATE(slot)         RAM(NES_OBJ_STATE_BASE + (slot))
#define OBJ_METASTATE(slot)     RAM(NES_OBJ_METASTATE_BASE + (slot))

/* Shove */
#define OBJ_SHOVE_DIR(slot)     RAM(NES_OBJ_SHOVE_DIR_BASE + (slot))
#define OBJ_SHOVE_DIST(slot)    RAM(NES_OBJ_SHOVE_DIST_BASE + (slot))

/* Animation */
#define OBJ_ANIM_CNTR(slot)     RAM(NES_OBJ_ANIM_CNTR_BASE + (slot))
#define OBJ_HFLIP(slot)         RAM(NES_OBJ_HFLIP_BASE + (slot))

/* Invulnerability */
#define OBJ_INV_TIMER(slot)     RAM(NES_OBJ_INV_TIMER_BASE + (slot))

#endif /* OBJECT_STATE_H */
