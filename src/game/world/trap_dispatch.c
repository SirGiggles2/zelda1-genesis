/* trap_dispatch.c — native trap subsystem dispatch (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; calls native core_* + enemy_*.
 * Drain provenance: src/oracle/world/trap_runtime.c.
 */

#include "trap_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"      /* RAM, OBJ */
#include "trap_state.h"        /* TELEPORT_LEVEL_INDEX, TELEPORT_ACTIVE_FLAG,
                                * WHIRLWIND_ACTIVE_FLAG, MODE_TIMER,
                                * TRAP_OBJ_TYPE, TRAP_BASE_SLOT */
#include "world_state.h"       /* LINK_DIR, LINK_Y, LINK_X, LINK_ACTION_TIMER,
                                * WORLD_TMP0/_1/_2/_3, OBJ_MOVE_TIMER */
#include "progress_state.h"    /* MODE_VALUE, SUBMODE_VALUE, FRAME_COUNTER */
#include "object_state.h"      /* OBJ_X, OBJ_Y */
#include "combat_state.h"      /* MON_TYPE, MON_STATUS_FLAGS,
                                * COMBAT_COLLIDED, MON_SHOVE_DIR/TIMER */
#include "enemy_state.h"       /* ENEMY_COLLIDED_TILE, ENEMY_ALIVE_FLAG */
#include "core/core_dispatch.h"     /* core_set_up_whirlwind, core_init_one_simple_object,
                                     * core_get_opposite_dir, core_reset_obj_metastate,
                                     * core_anim_set_sprite_desc_attrs,
                                     * core_destroy_whirlwind, core_destroy_monster,
                                     * core_take_one_rupee, core_abs,
                                     * core_clear_ram0300_up_to */
#include "enemies/enemy_dispatch.h" /* enemy_find_empty_monster_slot */
#include "world/sprite_dispatch.h"  /* sprite_anim_advance_and_fetch,
                                     * sprite_anim_set_obj_hflip,
                                     * sprite_anim_fetch_obj_pos */
#include "world/draw_dispatch.h"    /* draw_object_not_mirrored_with_frame,
                                     * draw_item_in_inventory */
#include "world/progress_dispatch.h" /* progress_update_player_position_marker */
#include "world/object_dispatch.h"   /* object_move_object */
#include "combat/link_collision_dispatch.h" /* link_collision_check_link_collision */
#include "cave/uw_person_dispatch.h" /* uw_person_person_draw_and_check_collisions */
#include "room/room_dispatch.h"     /* room_go_to_next_mode_from_play */

/* TeleportYs — Z_01.asm:1226. Per-level teleport Y coords. */
static const unsigned char k_teleport_ys[8] = {
    0x8Du, 0xADu, 0x8Du, 0x8Du, 0xADu, 0x8Du, 0xADu, 0x5Du
};

/* WhirlwindPrevRoomIdList — Z_01.asm:1203. Per-level previous room id
 * to restore after a whirlwind cycle (level 1..8). */
static const unsigned char k_whirlwind_prev_room_id_list[8] = {
    0x36u, 0x3Bu, 0x73u, 0x44u, 0x0Au, 0x21u, 0x41u, 0x6Cu
};

/* TrapXs — Z_01.asm:1297. */
static const unsigned char k_trap_xs[6] = {
    0x20u, 0x20u, 0xD0u, 0xD0u, 0x40u, 0xB0u
};

/* TrapYs — Z_01.asm:1301. */
static const unsigned char k_trap_ys[6] = {
    0x5Du, 0xBDu, 0x5Du, 0xBDu, 0x8Du, 0x8Du
};

/* TrapAllowedDirs — Z_01.asm:1308. Per-slot gate of which dirs
 * the trap is allowed to charge in. NES reads TrapAllowedDirs-1,X for
 * any monster slot 1..$B; slots 7..$B read past the six entries into
 * the first opcode bytes of UpdateTrap_Full (bank 1 $A34A..$A34E,
 * B5 AC D0 4B A5, read from the PRG0 ROM). */
static const unsigned char k_trap_allowed_dirs[11] = {
    0x05u, 0x09u, 0x06u, 0x0Au, 0x01u, 0x02u,
    0xB5u, 0xACu, 0xD0u, 0x4Bu, 0xA5u
};

/* LinkToSquareOffsetsX — Z_01.asm:1264 (mirrored in
 * src/data/player_constants.inc). */
static const unsigned char k_link_to_square_offsets_x[4] = {
    0x00u, 0x00u, 0xF0u, 0x10u
};

/* LinkToSquareOffsetsY — Z_01.asm:1268. */
static const unsigned char k_link_to_square_offsets_y[4] = {
    0xFBu, 0x13u, 0x03u, 0x03u
};

/* LevelMasks — Z_01.asm. Used by SummonWhirlwind to gate teleport
 * cycling on completed dungeons. Same shape as the table baked
 * inside progress_dispatch.c (k_level_masks). */
static const unsigned char k_trap_level_masks[8] = {
    0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u
};

void trap_advance_teleporting_level_index(void)
{
    /* drain at trap_runtime.c:81-86. NES AdvanceTeleportingLevelIndex.
     *
     *   INC TELEPORT_LEVEL_INDEX
     *   LDA LINK_DIR
     *   AND #$09        ; up ($08) | right ($01)
     *   BNE @Exit
     *   DEC TELEPORT_LEVEL_INDEX
     *   DEC TELEPORT_LEVEL_INDEX
     *
     * If LINK_DIR's up-or-right bits are clear, undo the increment
     * AND decrement once more (net -1 from original). */
    TELEPORT_LEVEL_INDEX = (uint8_t)(TELEPORT_LEVEL_INDEX + 1u);
    if ((LINK_DIR & 0x09u) == 0u) {
        TELEPORT_LEVEL_INDEX = (uint8_t)(TELEPORT_LEVEL_INDEX - 2u);
    }
}

static void whirlwind_sync_link(void);

/* CheckInitWhirlwindAndBeginUpdate (Z_01.asm), the whirlwind half: in the
 * destination room teleporting state 2 drops Link off. Link halts ($40),
 * InitWhirlwind puts TeleportYs[TeleportingLevelIndex & 7] in Link's ObjY
 * and SetUpWhirlwind the whirlwind in slot 9 at his Y, X 0. T-171: the
 * drain also ran AdvanceTeleportingLevelIndex, which the NES does not.
 * Returns 1 when a teleport is in progress. */
unsigned char trap_init_whirlwind_at_destination(void)
{
    if ((unsigned char)TELEPORT_ACTIVE_FLAG == 0u) return 0u;
    TELEPORT_ACTIVE_FLAG = (uint8_t)((unsigned char)TELEPORT_ACTIVE_FLAG + 1u);
    LINK_ACTION_TIMER = 0x40u;
    LINK_Y = k_teleport_ys[(unsigned char)TELEPORT_LEVEL_INDEX & 7u];
    core_set_up_whirlwind(9u);
    whirlwind_sync_link();
    return 1u;
}

void trap_check_init_whirlwind_and_begin_update(void)
{
    (void)trap_init_whirlwind_at_destination();
    SUBMODE_VALUE = 0u;
    MODE_TIMER = (uint8_t)((unsigned char)MODE_TIMER + 1u);
}

void trap_summon_whirlwind(void)
{
    /* drain at trap_runtime.c:88-110. */
    if ((unsigned char)MODE_VALUE != 5u) {
        return;
    }
    trap_advance_teleporting_level_index();
    unsigned char mask =
        k_trap_level_masks[(unsigned char)TELEPORT_LEVEL_INDEX & 7u];
    for (;;) {
        const unsigned char triforce_pieces = (unsigned char)RAM(0x0671);
        if (triforce_pieces == 0u) {
            return;
        }
        if (triforce_pieces & mask) {
            break;
        }
        trap_advance_teleporting_level_index();
        if (LINK_DIR & 0x09u) {
            mask = (unsigned char)((mask << 1) | (mask >> 7));
        } else {
            mask = (unsigned char)((mask >> 1) | (mask << 7));
        }
    }
    if ((unsigned char)WHIRLWIND_ACTIVE_FLAG ||
        (unsigned char)TELEPORT_ACTIVE_FLAG) {
        return;
    }
    const unsigned int empty = enemy_find_empty_monster_slot();
    if (empty == 0u) {
        return;
    }
    WHIRLWIND_ACTIVE_FLAG =
        (uint8_t)((unsigned char)WHIRLWIND_ACTIVE_FLAG + 1u);
    core_set_up_whirlwind(empty);
}

void trap_init_trap_full(unsigned int slot)
{
    /* drain at trap_runtime.c:4-17. */
    WORLD_TMP1 = (uint8_t)MON_STATUS_FLAGS(slot);
    WORLD_TMP0 = TRAP_OBJ_TYPE;
    signed char count = ((unsigned char)MON_TYPE(slot) == TRAP_OBJ_TYPE) ? 5 : 3;
    do {
        const unsigned char ns =
            (unsigned char)((unsigned char)count +
                            (unsigned char)TRAP_BASE_SLOT);
        OBJ_X(ns) = k_trap_xs[(unsigned char)count];
        OBJ_Y(ns) = k_trap_ys[(unsigned char)count];
        core_init_one_simple_object(ns);
        --count;
    } while (count >= 0);
}

void trap_draw_whirlwind(unsigned int slot)
{
    /* drain at trap_runtime.c:19-24. */
    sprite_anim_advance_and_fetch(1u, slot);
    (void)core_anim_set_sprite_desc_attrs(
        (unsigned int)((unsigned char)FRAME_COUNTER & 3u));
    sprite_anim_set_obj_hflip(slot);
    draw_object_not_mirrored_with_frame(0u, slot);
}

/* The Genesis owns Link's position/facing in engine/src/main.c; the
 * whirlwind writes the NES cells (ObjX, ObjDir), so hand them over
 * (T-171 t171_flute_whirlwind t282: Link kept facing up). */
extern void roomrom_main_set_link_story_pose(unsigned char x, unsigned char y,
                                             unsigned char face);
static void whirlwind_sync_link(void)
{
    const unsigned char d = (unsigned char)LINK_DIR;
    /* ROOMROM_MAIN_LINK_FACE_*: 0 down, 1 up, 2 left, 3 right. */
    const unsigned char face = (d & 0x01u) ? 3u : (d & 0x02u) ? 2u :
                               (d & 0x08u) ? 1u : 0u;
    roomrom_main_set_link_story_pose((unsigned char)LINK_X,
                                     (unsigned char)RAM(0x0084u), face);
}

void trap_update_whirlwind_full(unsigned int slot)
{
    /* drain at trap_runtime.c:26-67. */
    const unsigned char halted =
        (unsigned char)((unsigned char)LINK_ACTION_TIMER & 0x40u);
    const unsigned char new_x =
        (unsigned char)(2u + (unsigned char)OBJ_X(slot));
    OBJ_X(slot) = new_x;
    int skip_collision = 0;
    if (halted == 0x40u) {
        const unsigned char tele = (unsigned char)TELEPORT_ACTIVE_FLAG;
        if (tele != 0u) {
            LINK_X = new_x;
            whirlwind_sync_link();
            if (tele != 1u && new_x == 0x80u) {
                LINK_ACTION_TIMER = 0u;
                TELEPORT_ACTIVE_FLAG = 0u;
                MON_TYPE(slot) = 0u;
                progress_update_player_position_marker();
                trap_draw_whirlwind(slot);
                return;
            }
            skip_collision = 1;
        }
    }
    if (!skip_collision) {
        link_collision_check_link_collision(slot);
        if ((unsigned char)COMBAT_COLLIDED) {
            LINK_DIR = 1u;
            MON_SHOVE_DIR(0) = 0u;
            MON_SHOVE_TIMER(0) = 0u;
            LINK_CELLAR_FLAG = 0u;
            LINK_ACTION_TIMER = 0x40u;
            OAM_HIDE_2 = 0xF8u;
            OAM_HIDE_3 = 0xF8u;
            WHIRLWIND_PREV_ROOM_ID =
                k_whirlwind_prev_room_id_list[
                    (unsigned char)TELEPORT_LEVEL_INDEX & 7u];
            TELEPORT_ACTIVE_FLAG =
                (uint8_t)((unsigned char)TELEPORT_ACTIVE_FLAG + 1u);
            whirlwind_sync_link();
        }
    }
    if ((unsigned char)OBJ_X(slot) < 0xF0u) {
        trap_draw_whirlwind(slot);
        return;
    }
    core_destroy_whirlwind(slot);
    if ((unsigned char)TELEPORT_ACTIVE_FLAG) {
        extern void roomrom_main_begin_whirlwind_scroll(void);
        room_go_to_next_mode_from_play();
        roomrom_main_begin_whirlwind_scroll();
    }
    trap_draw_whirlwind(slot);
}

void trap_update_rupee_stash_full(unsigned int slot)
{
    /* drain at trap_runtime.c:112-122. */
    const unsigned char dy =
        (unsigned char)((unsigned char)LINK_Y - (unsigned char)OBJ_Y(slot));
    const unsigned char dx =
        (unsigned char)((unsigned char)LINK_X - (unsigned char)OBJ_X(slot));
    if (core_abs((unsigned int)dy) < 9u && core_abs((unsigned int)dx) < 9u) {
        core_take_one_rupee();
        core_destroy_monster(slot);
        RUPEE_STASH_FLAG = 0u;
        return;
    }
    sprite_anim_fetch_obj_pos(slot);
    draw_item_in_inventory(22u, 22u);
}

void trap_update_trap_full(unsigned int slot)
{
    /* NES source: Z_01.asm:UpdateTrap_Full, State 0.
     * Drained C: src/oracle/world/trap_runtime.c:trprt_update_trap_full.
     * Coverage: STALE (sensing branches were inverted in the drain).
     * Stance: REPLACE sensing with the source order; keep movement/draw.
     * State 0 requires a lane within 14 pixels before choosing a charge. */
    const unsigned char state = (unsigned char)OBJ_STATE(slot);
    if (state == 0u) {
        const unsigned char dy_init =
            (unsigned char)((unsigned char)LINK_Y - (unsigned char)OBJ_Y(slot));
        const unsigned char lnkx = (unsigned char)LINK_X;
        const unsigned char lnky = (unsigned char)LINK_Y;
        const unsigned char trapx = (unsigned char)OBJ_X(slot);
        const unsigned char trapy = (unsigned char)OBJ_Y(slot);
        unsigned char dir, origin;
        if (core_abs((unsigned int)dy_init) < 0x0Eu && lnkx != trapx) {
            /* Same row: charge horizontally toward Link. */
            dir = lnkx < trapx ? 2u : 1u;
            origin = trapx;
        } else {
            /* NES @CheckHorizontal tests X proximity before choosing
             * the vertical direction. Equal Y cannot move on this axis. */
            const unsigned char dx_init = (unsigned char)(lnkx - trapx);
            if (core_abs((unsigned int)dx_init) >= 0x0Eu ||
                lnky == trapy) goto draw_and_check;
            /* NES `LDA ObjY,X / BNE @GoToState1`: a trap at Y 0 falls
             * through into @UpdateMovingStates still in state 0. */
            if (trapy == 0u) goto moving_states;
            dir = lnky < trapy ? 8u : 4u;
            origin = trapy;
        }
        TRAP_RETURN_COORD(slot) = origin;
        OBJ_DIR(slot) = dir;
        if ((dir & k_trap_allowed_dirs[(slot - 1u) % 11u]) != 0u) {
            OBJ_STATE(slot) = (uint8_t)((unsigned char)OBJ_STATE(slot) + 1u);
            OBJ_QSPD_FRAC(slot) = 0x70u;
        }
    } else {
moving_states:
        COMBAT_PART_INDEX = (uint8_t)OBJ_DIR(slot);
        object_move_object((unsigned short)slot);
        if (((unsigned char)OBJ_GRID_OFFSET(slot) & 0x0Fu) == 0u) {
            OBJ_GRID_OFFSET(slot) = 0u;
        }
        link_collision_check_link_collision(slot);
        unsigned char coord;
        unsigned char target;
        if ((unsigned char)OBJ_DIR(slot) & 0x0Cu) {
            coord = (unsigned char)OBJ_Y(slot);
            target = 0x90u;
        } else {
            coord = (unsigned char)OBJ_X(slot);
            target = 0x78u;
        }
        if ((unsigned char)OBJ_STATE(slot) & 1u) {
            const unsigned char dist =
                core_abs((unsigned int)(unsigned char)(coord - target));
            if (dist < 5u) {
                OBJ_DIR(slot) = (uint8_t)core_get_opposite_dir(
                    (unsigned int)(unsigned char)OBJ_DIR(slot));
                OBJ_QSPD_FRAC(slot) = 0x20u;
                OBJ_STATE(slot) =
                    (uint8_t)((unsigned char)OBJ_STATE(slot) + 1u);
            }
        } else {
            if (coord == (unsigned char)TRAP_RETURN_COORD(slot)) {
                OBJ_STATE(slot) = 0u;
            }
        }
    }
draw_and_check:
    uw_person_person_draw_and_check_collisions(slot);
}

void trap_init_mode_b_enter_cave_bank5(void)
{
    /* drain at trap_runtime.c:124-138. NES InitMode_B_EnterCave_Bank5.
     *
     * STAGE-2 partial port. NES InitMode_EnterRoom (z_05.asm:1564)
     * decomposes to several native helpers we already have, plus
     * heavy DrawSpritesBetweenRooms + level-block-attr-F caching
     * which defer. Same for RunCrossRoomTasks + Link_EndMoveAndAnimate.
     * Mode-B cellar entry under NATIVE_TRAP gets:
     *   - native room_reset_player_state (LINK_ACTION_TIMER + halt clear)
     *   - native core_clear_ram0300_up_to(5, 31)
     *   - native room_reset_inv_obj_state
     *   - Link teleport coords + cellar flag
     * Skipped: DrawSpritesBetweenRooms, level-attr-F cache,
     * Link_EndMoveAndAnimate, RunCrossRoomTasks. Zelda.md
     * NATIVE_TRAP=off keeps full asm path intact. */
    const unsigned char submode = (unsigned char)SUBMODE_VALUE;

    /* InitMode_EnterRoom partial: ResetPlayerState + ClearRam0300UpTo
     * + ResetInvObjState. Skip DrawSpritesBetweenRooms + level-attr-F. */
    room_reset_player_state();
    core_clear_ram0300_up_to(5u, 31u);
    RAM(0x0054u) = 0u;     /* door trigger info */
    RAM(0x0055u) = 0u;
    room_reset_inv_obj_state();

    LINK_X = 112u;
    LINK_Y = 0xDDu;
    LINK_DIR = 8u;

    /* TODO Phase 5: native Link_EndMoveAndAnimate port (huge ladder
     * /water/warp/animation chain in z_07.asm). */
    /* TODO Phase 5: native RunCrossRoomTasksAndBeginUpdateMode_PlayModesNoCellar
     * port. */

    SUBMODE_VALUE = submode;
    MODE_TIMER = 0u;
    SUBMODE_VALUE = (uint8_t)((unsigned char)SUBMODE_VALUE + 1u);
    OBJ_GRID_OFFSET(0) = 48u;
    LINK_CELLAR_FLAG = 1u;
}

void trap_check_passive_tile_objects(void)
{
    /* drain at trap_runtime.c:140-188. */
    if ((unsigned char)OBJ_GRID_OFFSET(0) != 0u) {
        return;
    }
    if ((unsigned char)PASSIVE_OBJ_FLAG == 0u) {
        return;
    }
    {
        const unsigned char collided_tile =
            (unsigned char)ENEMY_COLLIDED_TILE(0);
        unsigned char tile = 0xBBu;
        int found = 0;
        for (int count = 8; count > 0; --count) {
            ++tile;
            WORLD_TMP2 = tile;
            if (collided_tile == tile) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return;
        }
    }
    WORLD_TMP0 = (uint8_t)LINK_X;
    WORLD_TMP1 = (uint8_t)LINK_Y;
    if ((unsigned char)LINK_DIR & 0x0Cu) {
        const unsigned char col_type =
            (unsigned char)((unsigned char)WORLD_TMP2 & 3u);
        unsigned char lx = (unsigned char)WORLD_TMP0;
        if (col_type < 2u) {
            lx = (unsigned char)(lx + 8u);
        }
        WORLD_TMP0 = (uint8_t)(lx & 0xF0u);
    } else {
        if (!((unsigned char)WORLD_TMP2 & 1u)) {
            WORLD_TMP1 = (uint8_t)((unsigned char)WORLD_TMP1 + 8u);
        }
    }
    {
        const unsigned int empty = enemy_find_empty_monster_slot();
        if (empty == 0u) {
            return;
        }
        const unsigned int opp =
            core_get_opposite_dir((unsigned int)(unsigned char)LINK_DIR);
        const unsigned char opp_idx = (unsigned char)(opp >> 8);
        OBJ_X(empty) =
            (uint8_t)((unsigned char)WORLD_TMP0 +
                      k_link_to_square_offsets_x[opp_idx & 3u]);
        OBJ_Y(empty) =
            (uint8_t)((unsigned char)WORLD_TMP1 +
                      k_link_to_square_offsets_y[opp_idx & 3u]);
        if (!(unsigned char)ENEMY_ALIVE_FLAG(empty)) {
            return;
        }
        WORLD_TMP3 = (uint8_t)empty;
        for (signed char i = 11; i >= 1; --i) {
            const unsigned char si = (unsigned char)i;
            if (si == (unsigned char)empty) {
                continue;
            }
            if ((unsigned char)OBJ_X(si) != (unsigned char)OBJ_X(empty)) {
                continue;
            }
            if ((unsigned char)OBJ_Y(si) != (unsigned char)OBJ_Y(empty)) {
                continue;
            }
            if ((unsigned char)MON_TYPE(si) != 0u) {
                return;
            }
            if (!(unsigned char)ENEMY_ALIVE_FLAG(si)) {
                return;
            }
            break;
        }
        MON_TYPE(empty) =
            (uint8_t)(((unsigned char)WORLD_TMP2 >= 0xC0u)
                          ? TRAP_ALT_OBJ_TYPE
                          : TRAP_PUSHED_OBJ_TYPE);
        core_reset_obj_metastate(empty);
        OBJ_MOVE_TIMER(empty) = 63u;
    }
}
