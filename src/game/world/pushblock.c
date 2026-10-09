/* UW push block: object type $68 in slot 11.
 *
 * NES source: reference/aldonunez/Z_04.asm UpdateBlock, UpdateBlock0Idle,
 *             UpdateBlock1Moving, DrawBlock, ResetPushTimer,
 *             BlockPushDirections; Z_05.asm FindAndCreatePushBlockObject
 *             (boss_framework.c find_and_create_push_block_object seeds
 *             slot 11 at room init).
 * Drained C: dyn_tile_dispatch.c dyn_tile_change_tile_obj_tiles
 *            (ChangeTileObjTiles), object_dispatch.c object_move_object.
 * Coverage: FULL (tools/audit/asm_equiv UpdateBlock).
 * Stance:    REPLACE. The previous native state machine read the block
 *            square from a generated per-room table, accepted any input
 *            containing the push bit (diagonals pushed), never posted the
 *            "secret" tune and kept its state outside NES RAM.
 *
 * State lives in NES RAM like the NES: ObjState+11 (0 idle, 1 moving,
 * 2 done), ObjPushTimer+11, ObjDir+11, ObjX/Y+11, ObjGridOffset+11.
 * ChangeTileObjTiles queues the 2x2 tile records in DynTileBuf; the
 * transfer drain publishes them at the next NMI boundary (T-186). The
 * Genesis UW collision tables (uw_render) are refreshed from the same
 * squares here.
 */

#include "pushblock.h"
#include <stdint.h>
#include "platform_abi.h"              /* RAM */
#include "draw_dispatch.h"             /* draw_object_not_mirrored */
#include "dyn_tile_dispatch.h"         /* dyn_tile_change_tile_obj_tiles */
#include "object_dispatch.h"           /* object_move_object */
#include "../dungeon/uw_render.h"      /* UW collision tables */

#define PB_SLOT 11u                                    /* where the game creates it */
#define PB_OBJ_X(s)           RAM(0x0070u + (s))
#define PB_OBJ_Y(s)           RAM(0x0084u + (s))
#define PB_OBJ_DIR(s)         RAM(0x0098u + (s))
#define PB_OBJ_STATE(s)       RAM(0x00ACu + (s))
#define PB_OBJ_TYPE(s)        RAM(0x034Fu + (s))
#define PB_OBJ_GRID(s)        RAM(0x0394u + (s))
#define PB_PUSH_TIMER(s)      RAM(0x0412u + (s))     /* ObjPushTimer */
#define LINK_X                RAM(0x0070u)
#define LINK_Y                RAM(0x0084u)
#define LINK_INPUT_DIR        RAM(0x03F8u)             /* ObjInputDir */
#define ROOM_ALL_DEAD         RAM(0x034Du)
#define RETURN_TO_BANK4       RAM(0x00F7u)
#define TUNE1_REQUEST         RAM(0x0602u)
#define BLOCK_PUSH_COMPLETE   RAM(0x04CFu)
#define PB_SCRATCH_DIR        RAM(0x000Fu)             /* [0F] MoveObject dir */

#define PB_TILE_FLOOR  0x74u   /* UpdateBlock0Idle: floor at the source */
#define PB_TILE_BLOCK  0xB0u   /* UpdateBlock1Moving: block at the target */

/* BlockPushDirections (Z_04.asm:615): Link below, above, right, left. */
static const unsigned char k_block_push_dirs[4] = { 0x08u, 0x04u, 0x02u, 0x01u };

/* Debug mirror (main.c probe block): rooms whose block finished during
 * the current entry. Not read by gameplay. */
static unsigned char s_pb_done_room[256];
static unsigned char s_pb_room = 0xFFu;
static unsigned char s_pb_complete_count;

/* Link's collision on the Genesis follows the square ChangeTileObjTiles
 * just wrote into PlayAreaTiles. */
static void pb_refresh_uw_collision(unsigned int slot)
{
    roomrom_uw_room_render_refresh_square(
        (unsigned char)(((unsigned char)PB_OBJ_X(slot) & 0xF0u) >> 3),
        (unsigned char)(((unsigned char)((unsigned char)PB_OBJ_Y(slot) & 0xF0u) - 0x40u) >> 3));
}

/* DrawBlock: sprite one pixel above the object's Y, frame 0. */
static void pb_draw_block(unsigned int slot)
{
    RAM(0x0000u) = (unsigned char)PB_OBJ_X(slot);                  /* Anim_FetchObjPos... */
    RAM(0x0001u) = (unsigned char)((unsigned char)PB_OBJ_Y(slot) - 1u);   /* DEC $01 */
    RAM(0x000Fu) = 0u;
    draw_object_not_mirrored(0u, slot);
}

/* UpdateBlock0Idle. */
static void pb_idle(unsigned int slot)
{
    unsigned char y;
    unsigned char d;
    if ((unsigned char)ROOM_ALL_DEAD == 0u) {
        PB_PUSH_TIMER(slot) = 0u;                                  /* ResetPushTimer */
        return;
    }
    if ((unsigned char)LINK_X == (unsigned char)PB_OBJ_X(slot)) {
        y = 0u;
        d = (unsigned char)((unsigned char)LINK_Y + 3u - (unsigned char)PB_OBJ_Y(slot));
    } else {
        if ((unsigned char)((unsigned char)LINK_Y + 3u) != (unsigned char)PB_OBJ_Y(slot)) {
            PB_PUSH_TIMER(slot) = 0u;
            return;
        }
        y = 2u;
        d = (unsigned char)((unsigned char)LINK_X - (unsigned char)PB_OBJ_X(slot));
    }
    if (d & 0x80u) {                                         /* BPL / INY / Negate */
        y++;
        d = (unsigned char)(0u - d);
    }
    if (d >= 0x11u || (unsigned char)LINK_INPUT_DIR != k_block_push_dirs[y]) {
        PB_PUSH_TIMER(slot) = 0u;
        return;
    }
    PB_PUSH_TIMER(slot) = (uint8_t)((unsigned char)PB_PUSH_TIMER(slot) + 1u);
    if ((unsigned char)PB_PUSH_TIMER(slot) < 0x10u) return;
    PB_OBJ_DIR(slot) = k_block_push_dirs[y];
    PB_OBJ_STATE(slot) = (uint8_t)((unsigned char)PB_OBJ_STATE(slot) + 1u);
    RETURN_TO_BANK4 = (uint8_t)((unsigned char)RETURN_TO_BANK4 + 1u);
    dyn_tile_change_tile_obj_tiles(PB_TILE_FLOOR, slot);
    pb_refresh_uw_collision(slot);
    pb_draw_block(slot);
}

/* UpdateBlock1Moving. */
static void pb_moving(unsigned int slot)
{
    unsigned char g;
    PB_SCRATCH_DIR = (unsigned char)PB_OBJ_DIR(slot);
    object_move_object((unsigned short)slot);
    pb_draw_block(slot);
    g = (unsigned char)PB_OBJ_GRID(slot);
    if (g != 0x10u && g != 0xF0u) return;
    TUNE1_REQUEST = 0x04u;                                   /* "secret revealed" */
    RETURN_TO_BANK4 = (uint8_t)((unsigned char)RETURN_TO_BANK4 + 1u);
    dyn_tile_change_tile_obj_tiles(PB_TILE_BLOCK, slot);
    pb_refresh_uw_collision(slot);
    PB_OBJ_STATE(slot) = (uint8_t)((unsigned char)PB_OBJ_STATE(slot) + 1u);
    BLOCK_PUSH_COMPLETE = (uint8_t)((unsigned char)BLOCK_PUSH_COMPLETE + 1u);
    if (s_pb_room != 0xFFu) s_pb_done_room[s_pb_room] = 1u;
    if (s_pb_complete_count < 0xFFu) s_pb_complete_count++;
}

/* UpdateBlock: object loop handler for type $68 (enemy_loop.c). */
void roomrom_pushblock_update(unsigned int slot)
{
    switch ((unsigned char)PB_OBJ_STATE(slot) & 0x03u) {
    case 0u: pb_idle(slot); break;
    case 1u: pb_moving(slot); break;
    default: break;                                          /* UpdateBlock2Done */
    }
}

void roomrom_pushblock_init(void)
{
    unsigned short i;
    for (i = 0u; i < 256u; i++) s_pb_done_room[i] = 0u;
    s_pb_room = 0xFFu;
    s_pb_complete_count = 0u;
}

/* Room entry (main.c): FindAndCreatePushBlockObject already reset slot
 * 11; only the debug mirror follows the room. */
void roomrom_pushblock_room_load(unsigned char level,
                                 unsigned char quest,
                                 unsigned char room_id)
{
    (void)level;
    (void)quest;
    s_pb_room = room_id;
    s_pb_done_room[room_id] = 0u;
}

/* The NES has no per-frame block work outside the object loop. */
void roomrom_pushblock_tick(void)
{
}

/* Debug accessors (main.c probe block, offsets 80..88). */
static unsigned char pb_is_block(void)
{
    return (unsigned char)((unsigned char)PB_OBJ_TYPE(PB_SLOT) == 0x68u);
}

unsigned char roomrom_pushblock_state_for_room(unsigned char room_id)
{
    return s_pb_done_room[room_id];
}

unsigned char roomrom_pushblock_active_state(void)
{
    return pb_is_block() ? (unsigned char)((unsigned char)PB_OBJ_STATE(PB_SLOT) & 0x03u) : 0u;
}

unsigned char roomrom_pushblock_active_dir(void)
{
    return pb_is_block() ? (unsigned char)PB_OBJ_DIR(PB_SLOT) : 0u;
}

unsigned char roomrom_pushblock_active_timer(void)
{
    return pb_is_block() ? (unsigned char)PB_PUSH_TIMER(PB_SLOT) : 0u;
}

unsigned char roomrom_pushblock_active_offset(void)
{
    const unsigned char g = (unsigned char)PB_OBJ_GRID(PB_SLOT);
    if (!pb_is_block()) return 0u;
    return (g & 0x80u) ? (unsigned char)(0u - g) : g;
}

unsigned char roomrom_pushblock_active_block_col(void)
{
    return pb_is_block() ? (unsigned char)((unsigned char)PB_OBJ_X(PB_SLOT) >> 4) : 0u;
}

unsigned char roomrom_pushblock_active_block_row(void)
{
    return pb_is_block()
        ? (unsigned char)(((unsigned char)PB_OBJ_Y(PB_SLOT) - 0x40u) >> 4) : 0u;
}

unsigned char roomrom_pushblock_complete_count(void)
{
    return s_pb_complete_count;
}

unsigned char roomrom_pushblock_room_all_dead(void)
{
    return (unsigned char)ROOM_ALL_DEAD;
}

void roomrom_pushblock_publish_persist(void)
{
    volatile unsigned char *dst =
        (volatile unsigned char *)ROOMROM_DEBUG_PUSHBLOCK_PERSIST_BASE;
    unsigned short i;
    for (i = 0u; i < 256u; i++) dst[i] = s_pb_done_room[i];
}
