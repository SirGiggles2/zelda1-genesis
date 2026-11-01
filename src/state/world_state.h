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
static inline uint8_t world_link_dir_get(void)        { return F_ram_0098_R2; }
static inline void    world_link_dir_set(uint8_t v)   { F_ram_0098_R2 = v; }
static inline uint8_t world_cur_room_id_get(void)     { return F_ram_00EB_R3; }
static inline void    world_cur_room_id_set(uint8_t v){ F_ram_00EB_R3 = v; }
static inline uint8_t world_prev_room_id_get(void)    { return F_PREV_ROOM_ID_R; }
static inline void    world_prev_room_id_set(uint8_t v){ F_PREV_ROOM_ID_R = v; }

/* ----------------------------------------------------------------------
 * Legacy macro view — kept until Phase 4 close-gate confirms migration.
 * -------------------------------------------------------------------- */
/* Shared world/object state for promoted z_01 world and weapon code. */
/* Zero-page scratch aliases through scratch_state.h. */
#define WORLD_TMP0                     ZP_TMP0
#define WORLD_TMP1                     ZP_TMP1
#define WORLD_TMP2                     ZP_TMP2
#define WORLD_TMP3                     ZP_TMP3

#define LINK_DIR                       F_ram_0098_R2
#define LINK_X                         F_ram_0070_R3
#define LINK_Y                         F_ram_0084_R3
#define LINK_ACTION_TIMER              F_ram_00AC_R3
#define CUR_ROOM_ID                    F_ram_00EB_R3
#define PREV_ROOM_ID                   F_PREV_ROOM_ID_R

#define WORLD_FADE_TIMER               F_WORLD_FADE_TIMER_R
#define WORLD_FADE_STEP                F_ram_051C_R2
#define WORLD_MAZE_STEP                F_WORLD_MAZE_STEP_R
#define WORLD_SECRET_SFX               F_ram_0602_R2
#define SFX_COMBAT                     F_ram_0604_R2

#define TRANSFER_BUF_POS               F_ram_0301_R2
#define TRANSFER_BUF_BYTE(off)         F_ram_0302_R6((off))

#define OBJ_DIR(slot)                  F_ram_0098_O3((slot))
#define OBJ_X(slot)                    F_ram_0070_O2((slot))
#define OBJ_Y(slot)                    F_ram_0084_O2((slot))
#define OBJ_GRID_OFFSET(slot)          F_ram_0394_O((slot))
#define OBJ_POS_FRAC(slot)             F_ram_03A8_O((slot))
#define OBJ_QSPD_FRAC(slot)            F_ram_03BC_O((slot))
#define OBJ_STATUS_FLAGS(slot)         F_ram_04BF_O((slot))
/* Canonical owner: object_state.h (same cell, NES ObjState $AC+slot). */
#ifndef OBJ_STATE
#define OBJ_STATE(slot)                F_ram_00AC_O2((slot))
#endif
#define OBJ_ANIM_TIMER(slot)           F_ram_03D0_O2((slot))
#define OBJ_MOVE_TIMER(slot)           F_ram_0028_O2((slot))

#define CANDLE_LIT_FLAG                F_ram_0513_R
/* WEAPON_DRAW_SLOT_A and WEAPON_DRAW_SLOT_B moved above with the typed view. */

#endif
