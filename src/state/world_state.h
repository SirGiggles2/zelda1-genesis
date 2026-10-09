#ifndef WORLD_STATE_H
#define WORLD_STATE_H

#include <stdint.h>
#include "platform_abi.h"
#include "scratch_state.h"

/* World subsystem state.
 *
 * Phase 4 (Overworld) typed-struct promotion target per
 * docs/audit/state_contract.md migration order.
 *
 * Two views coexist (typed + legacy macros) until consumers migrate.
 * WORLD_TMP0..3 ($0000..$0003) are NES zero-page scratch shared with
 * cave/combat/targeting/progress and are NOT modeled in WorldState.
 */

typedef struct WorldState {
    /* Link's current position + direction (NES OBJ slot 0). */
    uint8_t link_dir;            /* NES $0098 */
    uint8_t link_x;              /* NES NES_OBJ_X */
    uint8_t link_y;              /* NES NES_OBJ_Y */
    uint8_t link_action_timer;   /* NES $00AC */

    /* Room transition. */
    uint8_t cur_room_id;         /* NES $00EB */
    uint8_t prev_room_id;        /* NES $00EC */

    /* Fade + maze step. */
    uint8_t fade_timer;          /* NES $0034 */
    uint8_t fade_step;           /* NES $051C */
    uint8_t maze_step;           /* NES $052F */

    /* Audio cues. */
    uint8_t secret_sfx;          /* NES $0602 */
    uint8_t sfx_combat;          /* NES $0604 */

    /* Candle / weapon flags. */
    uint8_t candle_lit_flag;     /* NES $0513 */
} WorldState;

/* Compile-time constants. */
#define WEAPON_DRAW_SLOT_A             16u
#define WEAPON_DRAW_SLOT_B             17u

/* Inline accessors (bridge layer). */
static inline uint8_t world_link_dir_get(void)        { return RAM(0x0098); }
static inline void    world_link_dir_set(uint8_t v)   { RAM(0x0098) = v; }
static inline uint8_t world_cur_room_id_get(void)     { return RAM(0x00EB); }
static inline void    world_cur_room_id_set(uint8_t v){ RAM(0x00EB) = v; }
static inline uint8_t world_prev_room_id_get(void)    { return RAM(0x00EC); }
static inline void    world_prev_room_id_set(uint8_t v){ RAM(0x00EC) = v; }

/* ----------------------------------------------------------------------
 * Legacy macro view — kept until Phase 4 close-gate confirms migration.
 * -------------------------------------------------------------------- */
/* Shared world/object state for promoted z_01 world and weapon code. */
/* Zero-page scratch aliases through scratch_state.h. */
#define WORLD_TMP0                     ZP_TMP0
#define WORLD_TMP1                     ZP_TMP1
#define WORLD_TMP2                     ZP_TMP2
#define WORLD_TMP3                     ZP_TMP3

#define LINK_DIR                       RAM(0x0098)
#define LINK_X                         RAM(NES_OBJ_X)
#define LINK_Y                         RAM(NES_OBJ_Y)
#define LINK_ACTION_TIMER              RAM(0x00AC)
#define CUR_ROOM_ID                    RAM(0x00EB)
#define PREV_ROOM_ID                   RAM(0x00EC)

#define WORLD_FADE_TIMER               RAM(0x0034)
#define WORLD_FADE_STEP                RAM(0x051C)
#define WORLD_MAZE_STEP                RAM(0x052F)
#define WORLD_SECRET_SFX               RAM(0x0602)
#define SFX_COMBAT                     RAM(0x0604)

#define TRANSFER_BUF_POS               RAM(0x0301)
#define TRANSFER_BUF_BYTE(off)         RAM(0x0302 + (off))

#define OBJ_DIR(slot)                  OBJ(0x0098, (slot))
#define OBJ_X(slot)                    OBJ(NES_OBJ_X, (slot))
#define OBJ_Y(slot)                    OBJ(NES_OBJ_Y, (slot))
#define OBJ_GRID_OFFSET(slot)          OBJ(NES_OBJ_GRID_OFFSET, (slot))
#define OBJ_POS_FRAC(slot)             OBJ(NES_OBJ_POS_FRAC, (slot))
#define OBJ_QSPD_FRAC(slot)            OBJ(NES_OBJ_QSPD_FRAC, (slot))
#define OBJ_STATUS_FLAGS(slot)         OBJ(0x04BF, (slot))
/* Canonical owner: object_state.h (same cell, NES ObjState $AC+slot). */
#ifndef OBJ_STATE
#define OBJ_STATE(slot)                OBJ(0x00AC, (slot))
#endif
#define OBJ_ANIM_TIMER(slot)           OBJ(0x03D0, (slot))
#define OBJ_MOVE_TIMER(slot)           OBJ(0x0028, (slot))

#define CANDLE_LIT_FLAG                RAM(0x0513)
/* WEAPON_DRAW_SLOT_A and WEAPON_DRAW_SLOT_B moved above with the typed view. */

#endif
