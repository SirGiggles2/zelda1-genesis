#include "link_ladder.h"
#include "platform_abi.h"
#include "draw_dispatch.h"                  /* draw_static_item_sprites */
#include "../combat/collision_dispatch.h"   /* collision_get_colliding_tile_moving */
#include "../core/core_dispatch.h"          /* core_destroy_monster, core_abs,
                                             * core_get_opposite_dir */
#include "../../state/nes_ram_sync.h"        /* nes_ram_sync_link_face */

/* See link_ladder.h for the NES source map (T-056). */
#define TMP_00              0x0000u
#define TMP_01              0x0001u
#define TMP_0F              0x000Fu
#define CUR_LEVEL           0x0010u
#define GAME_MODE           0x0012u
#define DOORWAY_DIR         0x0053u
#define EMPTY_MONSTER_SLOT  0x0059u
#define LADDER_SLOT         0x0064u
#define OBJ_X               0x0070u
#define OBJ_Y               0x0084u
#define OBJ_DIR             0x0098u
#define OBJ_STATE           0x00ACu
#define OBJ_SHOVE_DIR       0x00C0u
#define OBJ_SHOVE_DIST      0x00D3u
#define ROOM_ID             0x00EBu
#define CUR_OBJ_INDEX       0x0340u
#define FIRST_UNWALKABLE    0x034Au
#define OBJ_TYPE            0x034Fu
#define OBJ_GRID_OFFSET     0x0394u
#define OBJ_INPUT_DIR       0x03F8u
#define OBJ_COLLIDED_TILE   0x049Eu
#define OBJ_INVINCIBILITY   0x04F0u
#define WHIRLWIND_TELEPORT  0x0522u
#define INV_LADDER          0x0663u

#define LADDER_OBJ_TYPE     0x5Fu
#define LADDER_ITEM_SLOT    0x0Cu

/* Z_07.asm LinkToLadderOffsetsX/Y, LadderRoomsOW. */
static const unsigned char k_ladder_dx[4] = { 0x00u, 0x00u, 0xF0u, 0x10u };
static const unsigned char k_ladder_dy[4] = { 0xFBu, 0x13u, 0x03u, 0x03u };
static const unsigned char k_ladder_rooms_ow[6] = {
    0x17u, 0x18u, 0x19u, 0x27u, 0x4Fu, 0x5Fu
};
/* Set by CheckLadder's @DrawLadder; FrameCounter of that tick, so a
 * tick that skips the object phase cannot leak the draw into the next. */
static unsigned char s_draw_pending;
static unsigned char s_draw_frame;
#define FRAME_COUNTER       0x0015u

void link_ladder_end_move(void)
{
    unsigned char gm, tile, x, in, y;

    if (nes_ram[WHIRLWIND_TELEPORT] != 0u) return;
    /* Modes 4 and 6 and below 5 go to @CheckWarps; the ladder only
     * applies at @CheckLadderRoom in mode 5. Grid truncation
     * (@TruncGridOffset) is done by the Genesis mover before this. */
    gm = nes_ram[GAME_MODE];
    if (gm != 0x05u) return;
    if (nes_ram[OBJ_GRID_OFFSET] != 0u) return;
    if (nes_ram[CUR_LEVEL] == 0u) {
        signed char i;
        for (i = 5; i >= 0; --i)
            if (nes_ram[ROOM_ID] == k_ladder_rooms_ow[i]) break;
        if (i < 0) return;
    }
    if (nes_ram[DOORWAY_DIR] != 0u) return;
    if (nes_ram[INV_LADDER] == 0u) return;
    if ((nes_ram[OBJ_STATE] & 0xC0u) == 0x40u) return;
    if (nes_ram[LADDER_SLOT] != 0u) return;

    /* ObjDir is Link's facing at this point of the NES frame; the Genesis
     * mirrors it into $98 in the object phase, so take it now. */
    nes_ram_sync_link_face();
    nes_ram[TMP_0F] = nes_ram[OBJ_DIR];
    tile = collision_get_colliding_tile_moving(0u);
    if (nes_ram[CUR_LEVEL] != 0u) {
        if (tile != 0xF4u) return;
    } else {
        if (tile < 0x8Du || tile >= 0x99u) return;
    }

    /* @SetUpLadder: FindEmptyMonsterSlot ($B..1, ObjType 0). */
    for (x = 0x0Bu; x != 0u; --x)
        if (nes_ram[OBJ_TYPE + x] == 0u) break;
    if (x == 0u) return;
    nes_ram[EMPTY_MONSTER_SLOT] = x;
    in = nes_ram[OBJ_INPUT_DIR];
    if (in == 0u) return;
    if (in != nes_ram[OBJ_DIR]) return;

    nes_ram[LADDER_SLOT] = x;
    nes_ram[OBJ_DIR + x] = in;
    y = (unsigned char)(core_get_opposite_dir(in) >> 8);   /* in != 0: 0..3 */
    nes_ram[OBJ_X + x] = (unsigned char)(nes_ram[OBJ_X] + k_ladder_dx[y & 3u]);
    nes_ram[OBJ_Y + x] = (unsigned char)(nes_ram[OBJ_Y] + k_ladder_dy[y & 3u]);
    nes_ram[OBJ_TYPE + x] = LADDER_OBJ_TYPE;
    nes_ram[OBJ_SHOVE_DIR + x] = 0u;           /* ResetShoveInfo */
    nes_ram[OBJ_SHOVE_DIST + x] = 0u;
    nes_ram[OBJ_INVINCIBILITY + x] = 0u;
    nes_ram[OBJ_STATE + x] = 1u;
}

unsigned char link_ladder_check(void)
{
    unsigned char x = nes_ram[LADDER_SLOT];
    unsigned char a, d;

    s_draw_pending = 0u;
    if (x == 0u) return 0u;
    if (nes_ram[OBJ_STATE + x] == 0u) goto stash;

    if (nes_ram[OBJ_DIR + x] & 0x0Cu) {
        if (nes_ram[OBJ_X] != nes_ram[OBJ_X + x]) goto stash;
        a = (unsigned char)((unsigned char)(nes_ram[OBJ_Y] + 3u) - nes_ram[OBJ_Y + x]);
    } else {
        if ((unsigned char)(nes_ram[OBJ_Y] + 3u) != nes_ram[OBJ_Y + x]) goto stash;
        a = (unsigned char)(nes_ram[OBJ_X] - nes_ram[OBJ_X + x]);
    }
    /* @CheckDistanceToLadder: [00] = |distance|. */
    d = core_abs(a);
    nes_ram[TMP_00] = d;
    if (d < 0x10u) {
        nes_ram[OBJ_STATE + x] = 2u;            /* @SetState2 */
        goto handle_input;
    }
    if (d != 0x10u) goto stash;
    if (nes_ram[OBJ_DIR] != nes_ram[OBJ_DIR + x]) goto stash;
    if (nes_ram[OBJ_STATE + x] == 1u) goto handle_input;

stash:
    nes_ram[LADDER_SLOT] = 0u;
    core_destroy_monster(x);
    /* CheckLadder returns with NES X still selecting the destroyed ladder.
     * Walker_Move falls through to MoveObject with that slot, not Link. */
    return x;

handle_input:
    /* X is the ladder slot, or 0 after case E (the NES switches X to
     * Link there and loops: ObjDir,X then reads Link's direction). */
    for (;;) {
        unsigned char opp;
        a = nes_ram[OBJ_INPUT_DIR];
        if (a == 0u) break;                     /* draw, [0F] = 0 */
        a = nes_ram[OBJ_DIR];
        /* A. Off the ladder centre, facing its way: keep Link's dir. */
        if (nes_ram[TMP_00] != 0u && a == nes_ram[OBJ_DIR + x]) break;
        /* B. Moving the ladder's way. */
        a = nes_ram[OBJ_DIR + x];
        if (a == nes_ram[TMP_0F]) break;
        /* C. Facing back where the ladder came from. */
        opp = (unsigned char)core_get_opposite_dir(a);
        a = opp;
        if (a == nes_ram[OBJ_DIR]) break;
        /* D/E. Ladder up and input up: test the tile 8 px higher. */
        if (opp != 0x04u || nes_ram[OBJ_INPUT_DIR] != 0x08u) {
            a = 0u;
            break;
        }
        nes_ram[TMP_0F] = 0x08u;                /* SetMovingDirAndSwitchToPlayerSlot */
        x = 0u;
        {
            const unsigned char saved_y = nes_ram[OBJ_Y];
            nes_ram[OBJ_Y] = (unsigned char)(saved_y - 8u);
            (void)collision_get_colliding_tile_moving(0u);
            nes_ram[OBJ_Y] = saved_y;
        }
        a = nes_ram[TMP_0F];
        if (nes_ram[OBJ_COLLIDED_TILE] >= nes_ram[FIRST_UNWALKABLE]) {
            a = 0u;
            break;
        }
        /* Walkable: BCC @HandleInput with X = 0. */
    }

    /* @DrawLadder (deferred, link_ladder_draw), then
     * SetMovingDirAndSwitchToPlayerSlot. */
    s_draw_pending = 1u;
    s_draw_frame = nes_ram[FRAME_COUNTER];
    nes_ram[TMP_0F] = a;
    return 0u;
}

void link_ladder_draw(void)
{
    unsigned char x, saved_cur;
    if (!s_draw_pending) return;
    s_draw_pending = 0u;
    if (s_draw_frame != nes_ram[FRAME_COUNTER]) return;
    x = nes_ram[LADDER_SLOT];
    if (x == 0u) return;
    saved_cur = nes_ram[CUR_OBJ_INDEX];
    nes_ram[CUR_OBJ_INDEX] = x;                 /* Genesis cache owner */
    /* Anim_FetchObjPosForSpriteDescriptor. */
    nes_ram[TMP_00] = nes_ram[OBJ_X + x];
    nes_ram[TMP_01] = nes_ram[OBJ_Y + x];
    nes_ram[TMP_0F] = 0u;
    draw_static_item_sprites(0u, x, LADDER_ITEM_SLOT);
    nes_ram[CUR_OBJ_INDEX] = saved_cur;
}
