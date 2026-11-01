#include "link_ladder.h"
#include "platform_abi.h"
#include "draw_dispatch.h"                  /* draw_static_item_sprites */
#include "../combat/collision_dispatch.h"   /* collision_get_colliding_tile_moving */
#include "../core/core_dispatch.h"          /* core_destroy_monster, core_abs,
                                             * core_get_opposite_dir */
#include "../../state/nes_ram_sync.h"        /* nes_ram_sync_link_face */
#include "../../state/player_state.h"

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
static unsigned char s_ladder_player;
#define FRAME_COUNTER       0x0015u

/* NES source: CheckLadder -> LadderSlot and deferred DrawLadder.
 * Drained C: existing end-move/check/draw implementation below.
 * Coverage: PARTIAL independent party ladders; source rules unchanged.
 * Stance: EXTEND caller/owner selection and derived draw requests. */
void link_ladder_set_player(unsigned char player)
{
    s_ladder_player = player < PLAYER_MAX_COUNT ? player : 0u;
}

void link_ladder_end_move(void)
{
    unsigned char gm, tile, x, in, y;

    if (F_ram_0522_N != 0u) return;
    /* Modes 4 and 6 and below 5 go to @CheckWarps; the ladder only
     * applies at @CheckLadderRoom in mode 5. Grid truncation
     * (@TruncGridOffset) is done by the Genesis mover before this. */
    gm = F_ram_0012_N3;
    if (gm != 0x05u) return;
    if (F_ram_0394_N != 0u) return;
    if (F_ram_0010_N3 == 0u) {
        signed char i;
        for (i = 5; i >= 0; --i)
            if (F_ram_00EB_N == k_ladder_rooms_ow[i]) break;
        if (i < 0) return;
    }
    if (F_ram_0053_N != 0u) return;
    if (F_ram_0663_N == 0u) return;
    if ((F_ram_00AC_N & 0xC0u) == 0x40u) return;
    if (F_ram_0064_N != 0u) return;

    /* ObjDir is Link's facing at this point of the NES frame; the Genesis
     * mirrors it into $98 in the object phase, so take it now. */
    nes_ram_sync_link_face();
    F_ram_000F_N = F_ram_0098_N5;
    tile = collision_get_colliding_tile_moving(0u);
    if (F_ram_0010_N3 != 0u) {
        if (tile != 0xF4u) return;
    } else {
        if (tile < 0x8Du || tile >= 0x99u) return;
    }

    /* @SetUpLadder: FindEmptyMonsterSlot ($B..1, ObjType 0). */
    for (x = 0x0Bu; x != 0u; --x)
        if (F_ram_034F_X(x) == 0u) break;
    if (x == 0u) return;
    F_ram_0059_N = x;
    in = F_ram_03F8_N;
    if (in == 0u) return;
    if (in != F_ram_0098_N5) return;

    F_ram_0064_N = x;
    F_ram_0098_X2(x) = in;
    y = (unsigned char)(core_get_opposite_dir(in) >> 8);   /* in != 0: 0..3 */
    F_ram_0070_X(x) = (unsigned char)(F_ram_0070_N + k_ladder_dx[y & 3u]);
    F_ram_0084_X(x) = (unsigned char)(F_ram_0084_N + k_ladder_dy[y & 3u]);
    F_ram_034F_X(x) = LADDER_OBJ_TYPE;
    F_ram_00C0_X(x) = 0u;           /* ResetShoveInfo */
    F_ram_00D3_X(x) = 0u;
    F_ram_04F0_X(x) = 0u;
    F_ram_00AC_X(x) = 1u;
}

unsigned char link_ladder_check(void)
{
    unsigned char x = F_ram_0064_N;
    unsigned char a, d;

    s_draw_pending &= (unsigned char)~(1u << s_ladder_player);
    if (x == 0u) return 0u;
    if (F_ram_00AC_X(x) == 0u) goto stash;

    if (F_ram_0098_X2(x) & 0x0Cu) {
        if (F_ram_0070_N != F_ram_0070_X(x)) goto stash;
        a = (unsigned char)((unsigned char)(F_ram_0084_N + 3u) - F_ram_0084_X(x));
    } else {
        if ((unsigned char)(F_ram_0084_N + 3u) != F_ram_0084_X(x)) goto stash;
        a = (unsigned char)(F_ram_0070_N - F_ram_0070_X(x));
    }
    /* @CheckDistanceToLadder: [00] = |distance|. */
    d = core_abs(a);
    F_ram_0000_N = d;
    if (d < 0x10u) {
        F_ram_00AC_X(x) = 2u;            /* @SetState2 */
        goto handle_input;
    }
    if (d != 0x10u) goto stash;
    if (F_ram_0098_N5 != F_ram_0098_X2(x)) goto stash;
    if (F_ram_00AC_X(x) == 1u) goto handle_input;

stash:
    F_ram_0064_N = 0u;
    core_destroy_monster(x);
    /* CheckLadder returns with NES X still selecting the destroyed ladder.
     * Walker_Move falls through to MoveObject with that slot, not Link. */
    return x;

handle_input:
    /* X is the ladder slot, or 0 after case E (the NES switches X to
     * Link there and loops: ObjDir,X then reads Link's direction). */
    for (;;) {
        unsigned char opp;
        a = F_ram_03F8_N;
        if (a == 0u) break;                     /* draw, [0F] = 0 */
        a = F_ram_0098_N5;
        /* A. Off the ladder centre, facing its way: keep Link's dir. */
        if (F_ram_0000_N != 0u && a == F_ram_0098_X2(x)) break;
        /* B. Moving the ladder's way. */
        a = F_ram_0098_X2(x);
        if (a == F_ram_000F_N) break;
        /* C. Facing back where the ladder came from. */
        opp = (unsigned char)core_get_opposite_dir(a);
        a = opp;
        if (a == F_ram_0098_N5) break;
        /* D/E. Ladder up and input up: test the tile 8 px higher. */
        if (opp != 0x04u || F_ram_03F8_N != 0x08u) {
            a = 0u;
            break;
        }
        F_ram_000F_N = 0x08u;                /* SetMovingDirAndSwitchToPlayerSlot */
        x = 0u;
        {
            const unsigned char saved_y = F_ram_0084_N;
            F_ram_0084_N = (unsigned char)(saved_y - 8u);
            (void)collision_get_colliding_tile_moving(0u);
            F_ram_0084_N = saved_y;
        }
        a = F_ram_000F_N;
        if (F_ram_049E_N >= F_ram_034A_N) {
            a = 0u;
            break;
        }
        /* Walkable: BCC @HandleInput with X = 0. */
    }

    /* @DrawLadder (deferred, link_ladder_draw), then
     * SetMovingDirAndSwitchToPlayerSlot. */
    s_draw_pending |= (unsigned char)(1u << s_ladder_player);
    s_draw_frame = F_ram_0015_N4;
    F_ram_000F_N = a;
    return 0u;
}

static void draw_ladder_slot(unsigned char x)
{
    unsigned char saved_cur;
    if (x == 0u) return;
    saved_cur = F_ram_0340_N;
    F_ram_0340_N = x;                 /* Genesis cache owner */
    /* Anim_FetchObjPosForSpriteDescriptor. */
    F_ram_0000_N = F_ram_0070_X(x);
    F_ram_0001_N = F_ram_0084_X(x);
    F_ram_000F_N = 0u;
    draw_static_item_sprites(0u, x, LADDER_ITEM_SLOT);
    F_ram_0340_N = saved_cur;
}

void link_ladder_draw(void)
{
    const unsigned char pending = s_draw_pending;
    unsigned char player, i, saved_scratch[16];
    if (!pending) return;
    s_draw_pending = 0u;
    if (s_draw_frame != F_ram_0015_N4) return;
    if (pending & 1u) draw_ladder_slot(F_ram_0064_N);
    if (g_player_count <= 1u || (pending & 0x0Eu) == 0u) return;
    /* Additional presentation cannot change the source P1 scratch result. */
    for (i = 0u; i < 16u; ++i) saved_scratch[i] = nes_ram[i];
    for (player = 1u; player < g_player_count; ++player) {
        const unsigned char slot = G_PLAYERS[player].ladder_slot;
        if ((pending & (1u << player)) && slot != 0u &&
            F_ram_034F_X(slot) == LADDER_OBJ_TYPE) draw_ladder_slot(slot);
    }
    for (i = 0u; i < 16u; ++i) nes_ram[i] = saved_scratch[i];
}
