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
static void mask_input_in_border(unsigned char mask, unsigned char y)
{
    unsigned char coord = nes_ram[TMP_02];
    unsigned char crossed;
    nes_ram[TMP_01] = mask;
    if (nes_ram[TMP_00] & 0x0Au) crossed = (coord >= k_border_bounds[y]) ? 1u : 0u;
    else                         crossed = (coord <  k_border_bounds[y]) ? 1u : 0u;
    if (!crossed) {
        nes_ram[TMP_01] = 0xFFu;
        return;
    }
    nes_ram[BUTTONS_PRESSED] = (unsigned char)(nes_ram[BUTTONS_PRESSED] & mask);
    if (nes_ram[CUR_LEVEL] == 0u || mask != 0u) return;
    nes_ram[LINK_INPUT_DIR] = (unsigned char)(nes_ram[LINK_INPUT_DIR] &
        ((nes_ram[LINK_DIR] & 0x0Cu) ? 0x0Cu : 0x03u));
}

void link_filter_input(void)
{
    unsigned char opp = opposite_dir(nes_ram[LINK_DIR]);
    unsigned char y = rev_index(opp);
    if (nes_ram[LINK_STATE] != 0u) return;
    nes_ram[TMP_00] = opp;
    nes_ram[TMP_02] = (y >= 2u && y < 4u) ? nes_ram[LINK_X] : nes_ram[LINK_Y];
    mask_input_in_border(0x80u, (unsigned char)(y + 8u));
    if (nes_ram[TMP_01] == 0xFFu) {
        nes_ram[TMP_00] = nes_ram[LINK_DIR];
        y = rev_index(nes_ram[LINK_DIR]);
    }
    if (nes_ram[CUR_LEVEL] != 0u) y = (unsigned char)(y + 4u);
    mask_input_in_border(0x00u, y);
}

void link_modify_dir_in_doorway(void)
{
    if (nes_ram[CUR_LEVEL] != 0u) room_player_link_modify_dir_in_doorway();
}

/* Z_05.asm GetPlayerCoordsForDirection: [00] perpendicular, [01] axis. */
static void player_coords_for_dir(unsigned char dir)
{
    if (dir & 0x03u) {
        nes_ram[TMP_00] = nes_ram[LINK_Y];
        nes_ram[TMP_01] = nes_ram[LINK_X];
    } else {
        nes_ram[TMP_00] = nes_ram[LINK_X];
        nes_ram[TMP_01] = nes_ram[LINK_Y];
    }
}

static signed char search_doorways(const unsigned char *mins, const unsigned char *maxs)
{
    signed char y;
    for (y = 3; y >= 0; --y) {
        if (nes_ram[TMP_00] == k_doorway_coord[y] &&
            nes_ram[TMP_01] >= mins[y] && nes_ram[TMP_01] < maxs[y])
            return y;
    }
    return -1;
}

/* Z_05.asm TouchDoor (TouchDoorWall/Open/False/Bombable/Key/Shutter,
 * BlockUntilTime). [0C] door bit; blocks by [0E] = $FF. */
static void touch_door(unsigned char attr)
{
    unsigned char bit = nes_ram[TMP_0C];
    switch (attr & 7u) {
    case DOOR_TYPE_OPEN:
        return;
    case DOOR_TYPE_FALSE:
    case DOOR_TYPE_FALSE2:
        if (nes_ram[LINK_TIMER] == 0u) nes_ram[LINK_TIMER] = 0x18u;
        else if (nes_ram[LINK_TIMER] == 1u) return;
        break;
    case DOOR_TYPE_BOMBABLE:
        if (bit & nes_ram[CUR_OPENED_DOORS]) return;
        break;
    case DOOR_TYPE_SHUTTER:
        if (nes_ram[TRIGGERED_DOOR_CMD] != 0u) break;
        if ((bit & nes_ram[CUR_OPENED_DOORS]) == 0u) break;
        if (bit & nes_ram[SHUTTER_PASSED]) {
            if (nes_ram[LINK_TIMER] == 0u) return;         /* BlockUntilTime */
            break;
        }
        nes_ram[SHUTTER_PASSED] = (unsigned char)(nes_ram[SHUTTER_PASSED] | bit);
        return;
    case DOOR_TYPE_KEY:
    case DOOR_TYPE_KEY2:
        if (bit & nes_ram[CUR_OPENED_DOORS]) return;
        if (nes_ram[TRIGGERED_DOOR_CMD] != 0u) {
            if (nes_ram[LINK_TIMER] == 0u) return;         /* BlockUntilTime */
            break;
        }
        if (nes_ram[INV_MAGIC_KEY] == 0u) {
            if (nes_ram[INV_KEYS] == 0u) break;             /* BlockAtWall */
            nes_ram[INV_KEYS] = (unsigned char)(nes_ram[INV_KEYS] - 1u);
        }
        nes_ram[TRIGGERED_DOOR_DIR] = bit;                  /* TriggerOpenDoor */
        nes_ram[TRIGGERED_DOOR_CMD] = 0x06u;
        nes_ram[LINK_TIMER] = 0x20u;
        break;
    default:                                                /* TouchDoorWall */
        break;
    }
    nes_ram[TMP_0E] = 0xFFu;
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
    unsigned char dd = nes_ram[DOORWAY_DIR];
    signed char y;
    unsigned char in, attr, t;

    if (dd != 0u) {
        unsigned char yi;
        player_coords_for_dir(dd);
        yi = rev_index(dd);
        if (yi < 4u &&
            nes_ram[TMP_01] >= k_doorway_min_over[yi] &&
            nes_ram[TMP_01] <  k_doorway_max_over[yi] &&
            dd == nes_ram[LINK_DIR]) {
            player_coords_for_dir(nes_ram[LINK_DIR]);
            y = search_doorways(k_doorway_min_over, k_doorway_max_over);
        } else {
            y = search_doorways(k_doorway_min_under, k_doorway_max_under);
        }
    } else {
        player_coords_for_dir(nes_ram[LINK_DIR]);
        y = search_doorways(k_doorway_min_over, k_doorway_max_over);
    }
    if (y < 0) {
        nes_ram[DOORWAY_DIR] = 0u;
        return 0u;
    }
    /* @TestDoorwayDoor */
    nes_ram[TMP_0E] = (unsigned char)y;
    in = (unsigned char)(nes_ram[LINK_INPUT_DIR] & 0x0Fu);
    nes_ram[TMP_02] = in;
    nes_ram[TMP_0C] = in;
    if (in != k_rev_dirs[y]) return 0u;
    attr = find_door_attr(in);
    nes_ram[TMP_0D] = attr;
    touch_door(attr);
    if (nes_ram[TMP_0E] & 0x80u) return 0u;
    nes_ram[LINK_DIR]    = k_rev_dirs[y];
    nes_ram[TMP_0F]      = k_rev_dirs[y];
    nes_ram[DOORWAY_DIR] = k_rev_dirs[y];
    t = (unsigned char)(attr & 7u);
    return (t == 2u || t == 3u || t == 4u) ? 1u : 0u;
}

/* Z_07.asm GoToNextModeFromPlay. */
static void go_to_next_mode_from_play(void)
{
    nes_ram[GAME_MODE] = (unsigned char)(nes_ram[GAME_MODE] + 1u);
    nes_ram[GAME_SUBMODE] = 0u;
    nes_ram[IS_UPDATING_MODE] = 0u;
    nes_ram[TMP_0F] = 0u;
    nes_ram[LINK_STATE] = 0u;
    nes_ram[LINK_SHOVE_DIR] = 0u;
    nes_ram[LINK_SHOVE_DIST] = 0u;
    nes_ram[LINK_INVINCIBILITY] = 0u;
}

unsigned char link_uw_walker_checks(void)
{
    if (nes_ram[CUR_LEVEL] == 0u || nes_ram[GAME_MODE] == 9u) return 0u;
    if (nes_ram[DOORWAY_DIR] == 0u) (void)object_bound_by_room(0u);
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
    if (nes_ram[GAME_MODE] != 5u) return 0u;
    if (nes_ram[LADDER_SLOT] != 0u) return 0u;
    if (grid_offset != 0) return 0u;
    in = nes_ram[LINK_INPUT_DIR];
    if (in == 0u) return 0u;
    y = rev_index(in);
    if (y >= 4u) return 0u;
    d = k_rev_dirs[y];
    coord = (d & 0x0Cu) ? nes_ram[LINK_Y] : nes_ram[LINK_X];
    if (coord != k_screen_edge[y]) return 0u;
    nes_ram[LINK_DIR] = d;
    go_to_next_mode_from_play();
    return d;
}

unsigned char link_walker_check_tile_collision(signed char grid_offset)
{
    if (nes_ram[DOORWAY_DIR] != 0u) return go_walkable_dir(grid_offset);
    if (nes_ram[TMP_0E] & 0x80u) return 0u;
    if (grid_offset != 0) return 0u;
    nes_ram[TMP_0E] = 0u;
    if (nes_ram[TMP_0F] == 0u) return 0u;
    if (collision_get_colliding_tile_moving(0u) < nes_ram[FIRST_UNWALKABLE])
        return go_walkable_dir(grid_offset);
    /* PlayerUnwalkable. The OW CheckPassiveTileObjects step belongs to
     * the OW caller; this entry is used in the UW. */
    nes_ram[TMP_0F] = 0u;
    nes_ram[BUTTONS_PRESSED] = 0u;
    if (nes_ram[CUR_LEVEL] == 0u) return go_walkable_dir(grid_offset);
    return 0u;
}
