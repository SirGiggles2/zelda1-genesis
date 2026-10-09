#include "dock.h"
#include "platform_abi.h"
#include "draw_dispatch.h"                  /* draw_static_item_sprites */
#include "sprite_dispatch.h"                /* sprite_animate_object_walking */
#include "../room/room_dispatch.h"          /* room_go_to_next_mode_from_play */
#include "../../../RoomRom/src/roomrom_main_state.h"  /* roomrom_main_link_* */

/* See dock.h for the NES source map (T-056). */
#define OBJ_X               0x0070u
#define OBJ_Y               0x0084u
#define OBJ_DIR             0x0098u
#define OBJ_STATE           0x00ACu
#define ROOM_ID             0x00EBu
#define OBJ_GRID_OFFSET     0x0394u
#define TUNE1_REQUEST       0x0602u
#define INV_RAFT            0x0660u

#define RAFT_ITEM_SLOT      0x09u

/* Z_04.asm RaftDirections (state 1 down, state 2 up). */
static const unsigned char k_raft_dirs[2] = { 0x04u, 0x08u };

void world_update_dock(unsigned int slot)
{
    const unsigned char x = (unsigned char)slot;
    unsigned char st, leave = 0u;

    if (nes_ram[INV_RAFT] == 0u) return;
    st = nes_ram[OBJ_STATE + x];
    if (st == 0u) {
        const unsigned char dock_x = (nes_ram[ROOM_ID] == 0x55u) ? 0x80u : 0x60u;
        unsigned char y = 1u;
        const unsigned char ly = nes_ram[OBJ_Y];
        if (dock_x != nes_ram[OBJ_X]) return;
        nes_ram[OBJ_X + x] = dock_x;
        if (ly != 0x3Du) {
            y = 2u;
            if (ly != 0x7Du) return;
        }
        nes_ram[OBJ_STATE + x] = y;
        nes_ram[OBJ_Y + x] = (unsigned char)(ly + 6u);
        nes_ram[TUNE1_REQUEST] = 0x04u;          /* PlaySecretFoundTune */
        nes_ram[OBJ_STATE] = 0x40u;              /* halt Link */
        nes_ram[OBJ_DIR] = k_raft_dirs[y - 1u];
        roomrom_main_link_sync_from_nes();
        return;
    }

    if (st != 1u) {
        /* State 2: up. */
        nes_ram[OBJ_Y + x] = (unsigned char)(nes_ram[OBJ_Y + x] - 1u);
        nes_ram[OBJ_Y] = (unsigned char)(nes_ram[OBJ_Y] - 1u);
        if (nes_ram[OBJ_Y] != 0x3Du) goto draw;
        room_go_to_next_mode_from_play();
        leave = 1u;
    } else {
        /* State 1: down. */
        nes_ram[OBJ_Y + x] = (unsigned char)(nes_ram[OBJ_Y + x] + 1u);
        nes_ram[OBJ_Y] = (unsigned char)(nes_ram[OBJ_Y] + 1u);
        if (nes_ram[OBJ_Y] != 0x7Fu) goto draw;
        nes_ram[OBJ_GRID_OFFSET] = 0x02u;        /* $7F - $7D */
    }
    /* @ResetLinkAndRaftStateAndDraw */
    nes_ram[OBJ_STATE] = 0u;
    nes_ram[OBJ_STATE + x] = 0u;

draw:
    roomrom_main_link_end_move_from_object();   /* Link_EndMoveAndAnimate_Bank4 */
    sprite_animate_object_walking(x);
    draw_static_item_sprites(0u, x, RAFT_ITEM_SLOT);
    /* Genesis: the mode-6 scroll starts here, after Link and the raft
     * were drawn (it moves the typed Link to the arrival edge). */
    if (leave) roomrom_main_ow_scroll_from_object();
}
