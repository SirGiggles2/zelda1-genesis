#include "dock.h"
#include "platform_abi.h"
#include "draw_dispatch.h"                  /* draw_static_item_sprites */
#include "sprite_dispatch.h"                /* sprite_animate_object_walking */
#include "../room/room_dispatch.h"          /* room_go_to_next_mode_from_play */
#include "../../../engine/src/engine_state.h"  /* roomrom_main_link_* */
#include "../../state/player_state.h"

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

/* NES source: UpdateDock's exact-coordinate boarding and single carry tick.
 * Drained C: existing world_update_dock body, retained below.
 * Coverage: PARTIAL any-player boarding/carried actor and team transition.
 * Stance: EXTEND owner adapter; no additional raft update. */
void world_update_dock(unsigned int slot)
{
    unsigned char player;
    if (g_player_count <= 1u) {
        world_update_dock_for_loaded_player(slot);
        return;
    }
    player = G_OBJECT_PLAYER_REFS[slot].status_player;
    if (F_ram_00AC_X(slot) == 0u) {
        const short x = F_ram_00EB_N == 0x55u ? 0x80 : 0x60;
        for (player = 0u; player < g_player_count; ++player)
            if (PLAYER(player).x == x &&
                (PLAYER(player).y == 0x3D || PLAYER(player).y == 0x7D)) break;
        G_OBJECT_PLAYER_REFS[slot].status_player =
            player < g_player_count ? player : PLAYER_REF_NONE;
        if (player >= g_player_count) return;
    } else if (player >= g_player_count) player = 0u;
    if (player == 0u) world_update_dock_for_loaded_player(slot);
    else roomrom_main_dock_update_player(slot, player);
    if (F_ram_00AC_X(slot) == 0u)
        G_OBJECT_PLAYER_REFS[slot].status_player = PLAYER_REF_NONE;
}

void world_update_dock_for_loaded_player(unsigned int slot)
{
    const unsigned char x = (unsigned char)slot;
    unsigned char st, leave = 0u;

    if (F_ram_0660_N == 0u) return;
    st = F_ram_00AC_X(x);
    if (st == 0u) {
        const unsigned char dock_x = (F_ram_00EB_N == 0x55u) ? 0x80u : 0x60u;
        unsigned char y = 1u;
        const unsigned char ly = F_ram_0084_N;
        if (dock_x != F_ram_0070_N) return;
        F_ram_0070_X(x) = dock_x;
        if (ly != 0x3Du) {
            y = 2u;
            if (ly != 0x7Du) return;
        }
        F_ram_00AC_X(x) = y;
        F_ram_0084_X(x) = (unsigned char)(ly + 6u);
        F_ram_0602_N3 = 0x04u;          /* PlaySecretFoundTune */
        F_ram_00AC_N = 0x40u;              /* halt Link */
        F_ram_0098_N5 = k_raft_dirs[y - 1u];
        roomrom_main_link_sync_from_nes();
        return;
    }

    if (st != 1u) {
        /* State 2: up. */
        F_ram_0084_X(x) = (unsigned char)(F_ram_0084_X(x) - 1u);
        F_ram_0084_N = (unsigned char)(F_ram_0084_N - 1u);
        if (F_ram_0084_N != 0x3Du) goto draw;
        room_go_to_next_mode_from_play();
        leave = 1u;
    } else {
        /* State 1: down. */
        F_ram_0084_X(x) = (unsigned char)(F_ram_0084_X(x) + 1u);
        F_ram_0084_N = (unsigned char)(F_ram_0084_N + 1u);
        if (F_ram_0084_N != 0x7Fu) goto draw;
        F_ram_0394_N = 0x02u;        /* $7F - $7D */
    }
    /* @ResetLinkAndRaftStateAndDraw */
    F_ram_00AC_N = 0u;
    F_ram_00AC_X(x) = 0u;

draw:
    roomrom_main_link_end_move_from_object();   /* Link_EndMoveAndAnimate_Bank4 */
    sprite_animate_object_walking(x);
    draw_static_item_sprites(0u, x, RAFT_ITEM_SLOT);
    /* Genesis: the mode-6 scroll starts here, after Link and the raft
     * were drawn (it moves the typed Link to the arrival edge). */
    if (leave) roomrom_main_ow_scroll_from_object();
}
