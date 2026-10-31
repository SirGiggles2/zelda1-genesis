#include "link_doorway.h"
#include "door_state.h"                        /* uw_door_state_get_type */
#include "platform_abi.h"
#include "../combat/collision_dispatch.h"      /* collision_get_colliding_tile_moving */
#include "../room/room_dispatch.h"             /* room_player_link_modify_dir_in_doorway */
#include "../world/object_dispatch.h"          /* object_bound_by_room */

/* See link_doorway.h for the NES source map (T-131). */
#define TMP_00              0x0000u
#define TMP_01              0x0001u
#define TMP_02              0x0002u
#define TMP_0C              0x000Cu
#define TMP_0D              0x000Du
#define TMP_0E              0x000Eu
#define TMP_0F              0x000Fu
#define CUR_LEVEL           0x0010u
#define GAME_MODE           0x0012u
#define GAME_SUBMODE        0x0013u
#define IS_UPDATING_MODE    0x0011u
#define LINK_TIMER          0x0028u   /* ObjTimer */
#define DOORWAY_DIR         0x0053u
#define TRIGGERED_DOOR_CMD  0x0054u
#define TRIGGERED_DOOR_DIR  0x0055u
#define LADDER_SLOT         0x0064u
#define LINK_X              0x0070u
#define LINK_Y              0x0084u
#define LINK_DIR            0x0098u
#define LINK_STATE          0x00ACu
#define LINK_SHOVE_DIR      0x00C0u
#define LINK_SHOVE_DIST     0x00D3u
#define CUR_OPENED_DOORS    0x00EEu
#define BUTTONS_PRESSED     0x00F8u
#define FIRST_UNWALKABLE    0x034Au   /* ObjectFirstUnwalkableTile */
#define LINK_INPUT_DIR      0x03F8u
#define LINK_INVINCIBILITY  0x04F0u
#define SHUTTER_PASSED      0x0519u
#define INV_MAGIC_KEY       0x0664u
#define INV_KEYS            0x066Eu

/* Z_01.asm ReverseDirections: reverse index -> direction (up, down, left,
 * right). */
static const unsigned char k_rev_dirs[4] = { 0x08u, 0x04u, 0x02u, 0x01u };
/* Z_05.asm BorderBounds (OW outer, UW outer, A-button inner). */
static const unsigned char k_border_bounds[12] = {
    0xD6u, 0x45u, 0xE9u, 0x07u, 0xC6u, 0x55u, 0xD9u, 0x17u,
    0xBEu, 0x54u, 0xD1u, 0x1Fu
};
/* Z_05.asm DoorwayRequiredCoord / DoorwayBounds{Min,Max}{Over,Under}. */
static const unsigned char k_doorway_coord[4]     = { 0x78u, 0x78u, 0x8Du, 0x8Du };
static const unsigned char k_doorway_min_over[4]  = { 0x3Du, 0xBDu, 0x00u, 0xCFu };
static const unsigned char k_doorway_max_over[4]  = { 0x5Eu, 0xDEu, 0x21u, 0xF1u };
static const unsigned char k_doorway_min_under[4] = { 0x3Du, 0xBFu, 0x00u, 0xD2u };
static const unsigned char k_doorway_max_under[4] = { 0x5Cu, 0xDEu, 0x1Fu, 0xF1u };
/* Z_07.asm PlayerScreenEdgeBounds (up, down, left, right). */
static const unsigned char k_screen_edge[4] = { 0x3Du, 0xDDu, 0x00u, 0xF0u };

/* Z_01.asm GetOppositeDir's Y: reverse index of the lowest set direction
 * bit (right 3, left 2, down 1, up 0), $FF for none. */
static unsigned char rev_index(unsigned char dir)
{
    signed char y = 3;
    while (y >= 0) {
        unsigned char c = (unsigned char)(dir & 1u);
        dir >>= 1;
        if (c) break;
        --y;
    }
    return (unsigned char)y;
}

/* Z_01.asm GetOppositeDir's A. */
static unsigned char opposite_dir(unsigned char dir)
{
    static const unsigned char k_opposite[4] = { 0x04u, 0x08u, 0x01u, 0x02u };
    unsigned char y = rev_index(dir);
    return (y < 4u) ? k_opposite[y] : 0u;
}

/* Z_05.asm MaskInputInBorder. [00] direction, [02] coordinate. */
/* T-172 option A: inlined into its callers (call overhead). */
__attribute__((always_inline)) static void mask_input_in_border(unsigned char mask, unsigned char y)
{
    unsigned char coord = F_ram_0002_N;
    unsigned char crossed;
    F_ram_0001_N = mask;
    if (F_ram_0000_N & 0x0Au) crossed = (coord >= k_border_bounds[y]) ? 1u : 0u;
    else                         crossed = (coord <  k_border_bounds[y]) ? 1u : 0u;
    if (!crossed) {
        F_ram_0001_N = 0xFFu;
        return;
    }
    F_ram_00F8_N2 = (unsigned char)(F_ram_00F8_N2 & mask);
    if (F_ram_0010_N3 == 0u || mask != 0u) return;
    F_ram_03F8_N2 = (unsigned char)(F_ram_03F8_N2 &
        ((F_ram_0098_N4 & 0x0Cu) ? 0x0Cu : 0x03u));
}

void link_filter_input(void)
{
    unsigned char opp = opposite_dir(F_ram_0098_N4);
    unsigned char y = rev_index(opp);
    if (F_ram_00AC_N3 != 0u) return;
    F_ram_0000_N = opp;
    F_ram_0002_N = (y >= 2u && y < 4u) ? F_ram_0070_N4 : F_ram_0084_N4;
    mask_input_in_border(0x80u, (unsigned char)(y + 8u));
    if (F_ram_0001_N == 0xFFu) {
        F_ram_0000_N = F_ram_0098_N4;
        y = rev_index(F_ram_0098_N4);
    }
    if (F_ram_0010_N3 != 0u) y = (unsigned char)(y + 4u);
    mask_input_in_border(0x00u, y);
}

void link_modify_dir_in_doorway(void)
{
    if (F_ram_0010_N3 != 0u) room_player_link_modify_dir_in_doorway();
}

/* Z_05.asm GetPlayerCoordsForDirection: [00] perpendicular, [01] axis. */
static void player_coords_for_dir(unsigned char dir)
{
    if (dir & 0x03u) {
        F_ram_0000_N = F_ram_0084_N4;
        F_ram_0001_N = F_ram_0070_N4;
    } else {
        F_ram_0000_N = F_ram_0070_N4;
        F_ram_0001_N = F_ram_0084_N4;
    }
}

static signed char search_doorways(const unsigned char *mins, const unsigned char *maxs)
{
    signed char y;
    for (y = 3; y >= 0; --y) {
        if (F_ram_0000_N == k_doorway_coord[y] &&
            F_ram_0001_N >= mins[y] && F_ram_0001_N < maxs[y])
            return y;
    }
    return -1;
}

/* Z_05.asm TouchDoor (TouchDoorWall/Open/False/Bombable/Key/Shutter,
 * BlockUntilTime). [0C] door bit; blocks by [0E] = $FF. */
static void touch_door(unsigned char attr)
{
    unsigned char bit = F_ram_000C_N;
    switch (attr & 7u) {
    case DOOR_TYPE_OPEN:
        return;
    case DOOR_TYPE_FALSE:
    case DOOR_TYPE_FALSE2:
        if (F_ram_0028_N2 == 0u) F_ram_0028_N2 = 0x18u;
        else if (F_ram_0028_N2 == 1u) return;
        break;
    case DOOR_TYPE_BOMBABLE:
        if (bit & F_ram_00EE_N4) return;
        break;
    case DOOR_TYPE_SHUTTER:
        if (F_ram_0054_N3 != 0u) break;
        if ((bit & F_ram_00EE_N4) == 0u) break;
        if (bit & F_ram_0519_N2) { /* native_ram bulk:dyn_addr */
            if (F_ram_0028_N2 == 0u) return;         /* BlockUntilTime */
            break;
        }
        F_ram_0519_N2 = (unsigned char)(F_ram_0519_N2 | bit);
        return;
    case DOOR_TYPE_KEY:
    case DOOR_TYPE_KEY2:
        if (bit & F_ram_00EE_N4) return;
        if (F_ram_0054_N3 != 0u) {
            if (F_ram_0028_N2 == 0u) return;         /* BlockUntilTime */
            break;
        }
        if (F_ram_0664_N2 == 0u) {
            if (F_ram_066E_N2 == 0u) break;             /* BlockAtWall */
            F_ram_066E_N2 = (unsigned char)(F_ram_066E_N2 - 1u);
        }
        F_ram_0055_N3 = bit;                  /* TriggerOpenDoor */
        F_ram_0054_N3 = 0x06u;
        F_ram_0028_N2 = 0x20u;
        break;
    default:                                                /* TouchDoorWall */
        break;
    }
    F_ram_000E_N = 0xFFu;
}

/* Z_05.asm FindDoorAttrByDoorBit for a single door bit: the door type,
 * 8 when no bit matches. */
static unsigned char find_door_attr(unsigned char bit)
{
    if (bit & DOOR_BIT_E) return uw_door_state_get_type(DOOR_DIR_E);
    if (bit & DOOR_BIT_W) return uw_door_state_get_type(DOOR_DIR_W);
    if (bit & DOOR_BIT_S) return uw_door_state_get_type(DOOR_DIR_S);
    if (bit & DOOR_BIT_N) return uw_door_state_get_type(DOOR_DIR_N);
    return 8u;
}

/* Z_05.asm CheckDoorway. Returns 1 for GoToNextModeFromPlay. */
static unsigned char check_doorway(void)
{
    unsigned char dd = F_ram_0053_N;
    signed char y;
    unsigned char in, attr, t;

    if (dd != 0u) {
        unsigned char yi;
        player_coords_for_dir(dd);
        yi = rev_index(dd);
        if (yi < 4u &&
            F_ram_0001_N >= k_doorway_min_over[yi] &&
            F_ram_0001_N <  k_doorway_max_over[yi] &&
            dd == F_ram_0098_N4) {
            player_coords_for_dir(F_ram_0098_N4);
            y = search_doorways(k_doorway_min_over, k_doorway_max_over);
        } else {
            y = search_doorways(k_doorway_min_under, k_doorway_max_under);
        }
    } else {
        player_coords_for_dir(F_ram_0098_N4);
        y = search_doorways(k_doorway_min_over, k_doorway_max_over);
    }
    if (y < 0) {
        F_ram_0053_N = 0u;
        return 0u;
    }
    /* @TestDoorwayDoor */
    F_ram_000E_N = (unsigned char)y;
    in = (unsigned char)(F_ram_03F8_N2 & 0x0Fu);
    F_ram_0002_N = in;
    F_ram_000C_N = in;
    if (in != k_rev_dirs[y]) return 0u;
    attr = find_door_attr(in);
    F_ram_000D_N = attr;
    touch_door(attr);
    if (F_ram_000E_N & 0x80u) return 0u;
    F_ram_0098_N4    = k_rev_dirs[y];
    F_ram_000F_N      = k_rev_dirs[y];
    F_ram_0053_N = k_rev_dirs[y];
    t = (unsigned char)(attr & 7u);
    return (t == 2u || t == 3u || t == 4u) ? 1u : 0u;
}

/* Z_07.asm GoToNextModeFromPlay. */
static void go_to_next_mode_from_play(void)
{
    F_ram_0012_N3 = (unsigned char)(F_ram_0012_N3 + 1u);
    F_ram_0013_N2 = 0u;
    F_ram_0011_N2 = 0u;
    F_ram_000F_N = 0u;
    F_ram_00AC_N3 = 0u;
    F_ram_00C0_N2 = 0u;
    F_ram_00D3_N2 = 0u;
    F_ram_04F0_N3 = 0u;
}

unsigned char link_uw_walker_checks(void)
{
    if (F_ram_0010_N3 == 0u || F_ram_0012_N3 == 9u) return 0u;
    if (F_ram_0053_N == 0u) (void)object_bound_by_room(0u);
    if (check_doorway()) {
        go_to_next_mode_from_play();
        return 1u;
    }
    return 0u;
}

/* Z_07.asm GoWalkableDir / CheckScreenEdge for Link. */
static unsigned char go_walkable_dir(signed char grid_offset)
{
    unsigned char in, y, d, coord;
    if (F_ram_0012_N3 != 5u) return 0u;
    if (F_ram_0064_N != 0u) return 0u;
    if (grid_offset != 0) return 0u;
    in = F_ram_03F8_N2;
    if (in == 0u) return 0u;
    y = rev_index(in);
    if (y >= 4u) return 0u;
    d = k_rev_dirs[y];
    coord = (d & 0x0Cu) ? F_ram_0084_N4 : F_ram_0070_N4;
    if (coord != k_screen_edge[y]) return 0u;
    F_ram_0098_N4 = d;
    go_to_next_mode_from_play();
    return d;
}

unsigned char link_walker_check_tile_collision(signed char grid_offset)
{
    if (F_ram_0053_N != 0u) return go_walkable_dir(grid_offset);
    if (F_ram_000E_N & 0x80u) return 0u;
    if (grid_offset != 0) return 0u;
    F_ram_000E_N = 0u;
    if (F_ram_000F_N == 0u) return 0u;
    if (collision_get_colliding_tile_moving(0u) < F_ram_034A_N)
        return go_walkable_dir(grid_offset);
    /* PlayerUnwalkable. The OW CheckPassiveTileObjects step belongs to
     * the OW caller; this entry is used in the UW. */
    F_ram_000F_N = 0u;
    F_ram_00F8_N2 = 0u;
    if (F_ram_0010_N3 == 0u) return go_walkable_dir(grid_offset);
    return 0u;
}
