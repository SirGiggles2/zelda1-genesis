/* Phase 8 Task 8.1 step 2 — Boss Framework body.
 *
 * NES source: Z_05.asm:8154-8250 (CreateRoomObjects).
 * Drained C:  NONE for CreateRoomObjects — transpiled body lives at
 *             src/zelda_translated/z_05.asm:9033 (REPLACE target).
 * Coverage:   PARTIAL — push-block branch (LBA_D & 0x40) defers to
 *             FindAndCreatePushBlockObject @ Z_05.asm:5461 which is
 *             not yet drained or shimmed; the branch is logged + no-op
 *             pending Phase 8 follow-up step (most boss rooms do not
 *             trigger this branch — boss tests still exercise the
 *             reward path).
 * Stance:     REPLACE (CreateRoomObjects) — verbatim transcribe of NES
 *             asm into a native body that touches the same RAM cells
 *             documented in src/state/boss_state.h.
 */

#include "boss_framework.h"
#include "boss_state.h"
#include "dungeon_state.h"
#include "world_dispatch.h"
#include "world/progress_dispatch.h"      /* progress_get_room_flag_uw_item_state */
#include "platform_abi.h"

/* NES FindAndCreatePushBlockObject (Z_05.asm:5461): state and direction
 * of slot 11 cleared; unique room $21 puts the block at ($40, $80), else
 * the first $B0 tile in play-area row $A, columns 4, 6, ... $18 (none
 * found: column $1A), at Y $90; type $68. */
static void find_and_create_push_block_object(unsigned char room_id)
{
    unsigned char col;
    OBJ(0x00ACu, 11u) = 0u;      /* ObjState+11 */
    OBJ(0x0098u, 11u) = 0u;      /* ObjDir+11 */
    if (((unsigned char)DUNGEON_LBA_D(room_id) & 0x3Fu) == 0x21u) {
        OBJ(0x0070u, 11u) = 0x40u;
        OBJ(0x0084u, 11u) = 0x80u;
    } else {
        for (col = 4u; col < 0x1Au; col = (unsigned char)(col + 2u)) {
            if ((unsigned char)RAM(NES_PLAY_AREA_BASE + (unsigned short)col * NES_TILE_COL_STRIDE + 10u) == 0xB0u)
                break;
        }
        OBJ(0x0070u, 11u) = (unsigned char)(col * 8u);
        OBJ(0x0084u, 11u) = 0x90u;
    }
    OBJ(0x034Fu, 11u) = 0x68u;   /* Block object type */
}

/* NES source: Z_05.asm:CreateRoomObjects @Deactivate through @StoreLocation.
 * Drained C: existing boss_framework_room_init (native replacement).
 * Coverage: FULL collected-item branch, block initialization and item position.
 * Stance: EXTEND; collected items suppress only the item, not room mechanics. */
void boss_framework_room_init(unsigned char room_id)
{
    unsigned char attrs_e;
    unsigned char attrs_f;
    unsigned char item_id;
    unsigned char secret_trigger;
    unsigned int  xy;
    unsigned char x;
    unsigned char y;
    unsigned char level;
    unsigned char deactivate;

    /* Z_05.asm:8157 — set ObjState[$13] = 0 (active by default). The
     * body below toggles to $FF (deactivated) on any of:
     *   - already-taken (UW only)
     *   - L-block item-id == 3 (master-sword stand-in for "no item")
     *   - secret trigger == 3 (last_boss) or 7 (foes_for_item)
     *   - OW path when not in mode 5 / room $5F. */
    BOSS_ROOM_ITEM_STATE = 0u;
    deactivate = 0u;

    level = (unsigned char)BOSS_CUR_LEVEL;
    if (level == 0u) {
        /* OW: only room $5F in gameplay mode 5 spawns the heart. */
        BOSS_ROOM_ITEM_ID = BOSS_ITEM_ID_HEART_CONTAINER;
        if ((unsigned char)BOSS_GAMEMODE != BOSS_GAMEMODE_PLAY ||
            room_id != BOSS_OW_HEART_CONTAINER_ROOM) {
            BOSS_ROOM_ITEM_STATE = 0xFFu;
            return;
        }
        BOSS_ROOM_ITEM_X = BOSS_OW_HEART_CONTAINER_X;
        BOSS_ROOM_ITEM_Y = BOSS_OW_HEART_CONTAINER_Y;
        return;
    }

    /* UW path. Z_05.asm:8166 jumps to @Deactivate, then still creates
     * the block and installs item coordinates. RoomItemId is retained. */
    if (progress_get_room_flag_uw_item_state() != 0u) {
        BOSS_ROOM_ITEM_STATE = 0xFFu;
        goto create_block;
    }

    /* Z_05.asm:8176-8183 — pull room item from LBA_E low 5 bits. */
    attrs_e = (unsigned char)DUNGEON_LBA_E(room_id);
    item_id = (unsigned char)(attrs_e & BOSS_LBA_E_ITEM_MASK);
    if (item_id == BOSS_ITEM_ID_MASTER_SWORD_PLACEHOLDER) {
        /* "No item" stand-in. Deactivate object but still write the id. */
        deactivate = 1u;
    }
    BOSS_ROOM_ITEM_ID = item_id;

    /* Z_05.asm:8188-8195 — secret trigger 3 (last_boss) or 7 (foes_for_item)
     * defers activation until the secret fires; deactivate slot now. */
    attrs_f = (unsigned char)DUNGEON_LBA_F(room_id);
    secret_trigger = (unsigned char)(attrs_f & BOSS_LBA_F_SECRET_MASK);
    if (secret_trigger == BOSS_SECRET_TRIGGER_LAST_BOSS ||
        secret_trigger == BOSS_SECRET_TRIGGER_FOES_ITEM) {
        deactivate = 1u;
    }

    if (deactivate != 0u) {
        BOSS_ROOM_ITEM_STATE = 0xFFu;
    }

create_block:
    /* Z_05.asm:8200-8203 — a room with a push block (LBA_D bit 6)
     * creates the block object in slot 11: FindAndCreatePushBlockObject
     * (Z_05.asm:5461). T-013: room $42 of L1 has one. */
    if (((unsigned char)DUNGEON_LBA_D(room_id) & 0x40u) != 0u)
        find_and_create_push_block_object(room_id);

    /* Z_05.asm:8207-8210 — set X/Y for the room item from
     * GetShortcutOrItemXY (drained as
     * worldrt_get_shortcut_or_item_xy_for_room). */
    xy = world_get_shortcut_or_item_xy_for_room((unsigned int)room_id);
    x  = (unsigned char)((xy >> 8) & 0xFFu);
    y  = (unsigned char)(xy & 0xFFu);

    /* Z_05.asm:8211-8222 — triforce piece shifts left 8 px. */
    if ((unsigned char)(DUNGEON_LBA_E(room_id) & BOSS_LBA_E_ITEM_MASK) ==
        BOSS_ITEM_ID_TRIFORCE_PIECE) {
        x = (unsigned char)(x - BOSS_LBA_E_TRIFORCE_X_OFFSET);
    }

    BOSS_ROOM_ITEM_X = x;
    BOSS_ROOM_ITEM_Y = y;
}
