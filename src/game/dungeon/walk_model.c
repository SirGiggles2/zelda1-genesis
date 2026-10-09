#include "walk_model.h"

#define UW_INPUT_RIGHT 0x01u
#define UW_INPUT_LEFT  0x02u
#define UW_INPUT_DOWN  0x04u
#define UW_INPUT_UP    0x08u

static const unsigned char s_door_required_coord[4] = {
    UW_WALK_DOORWAY_H_Y, UW_WALK_DOORWAY_H_Y,
    UW_WALK_DOORWAY_V_X, UW_WALK_DOORWAY_V_X
};

static const unsigned char s_door_min_over[4] = {
    UW_WALK_DOORWAY_E_MIN, UW_WALK_DOORWAY_W_MIN,
    UW_WALK_DOORWAY_S_MIN, UW_WALK_DOORWAY_N_MIN
};

static const unsigned char s_door_max_over[4] = {
    UW_WALK_DOORWAY_E_MAX, UW_WALK_DOORWAY_W_MAX,
    UW_WALK_DOORWAY_S_MAX, UW_WALK_DOORWAY_N_MAX
};

static const unsigned char s_door_min_under[4] = {
    (0xD2u + UW_WALK_VISUAL_X_BIAS_PX),
    UW_WALK_DOORWAY_W_MIN,
    0xB7u,
    UW_WALK_DOORWAY_N_MIN
};

static const unsigned char s_door_max_under[4] = {
    UW_WALK_DOORWAY_E_MAX,
    (0x1Fu + UW_WALK_VISUAL_X_BIAS_PX),
    UW_WALK_DOORWAY_S_MAX,
    0x54u
};

static unsigned char is_horizontal(unsigned char dir)
{
    return (dir == UW_WALK_DIR_LEFT || dir == UW_WALK_DIR_RIGHT) ? 1u : 0u;
}

static unsigned char is_vertical(unsigned char dir)
{
    return (dir == UW_WALK_DIR_UP || dir == UW_WALK_DIR_DOWN) ? 1u : 0u;
}

static void player_coords_for_dir(unsigned char obj_dir, short x, short y,
                                  short *perp, short *axis)
{
    if (is_horizontal(obj_dir)) {
        *perp = y;
        *axis = x;
    } else {
        *perp = x;
        *axis = y;
    }
}

static unsigned char coord_in_bounds(short coord,
                                     const unsigned char *mins,
                                     const unsigned char *maxs,
                                     unsigned char door_dir)
{
    return (coord >= (short)mins[door_dir] &&
            coord < (short)maxs[door_dir]) ? 1u : 0u;
}

static unsigned char search_doorways(short perp, short axis,
                                     const unsigned char *mins,
                                     const unsigned char *maxs,
                                     unsigned char *door_dir)
{
    static const unsigned char order[4] = {
        UW_WALK_DOOR_E, UW_WALK_DOOR_W, UW_WALK_DOOR_S, UW_WALK_DOOR_N
    };
    unsigned char i;

    for (i = 0u; i < 4u; i++) {
        unsigned char d = order[i];
        if (perp == (short)s_door_required_coord[d] &&
            coord_in_bounds(axis, mins, maxs, d)) {
            *door_dir = d;
            return 1u;
        }
    }
    return 0u;
}

void uw_walk_collidable_probe(unsigned char dir, short x, short y,
                              uw_walk_probe_t *probe)
{
    short base_x = x;
    short base_y = (short)(y + 0x0B);
    short offset;
    short tile_x;

    switch (dir) {
        case UW_WALK_DIR_RIGHT: offset = 0x10; break;
        case UW_WALK_DIR_DOWN:  offset = 0x08; break;
        default:                offset = -8;   break;
    }

    probe->has_second_col = is_vertical(dir);
    if (is_vertical(dir)) {
        probe->hot_x = base_x;
        if (dir == UW_WALK_DIR_DOWN && base_y >= UW_WALK_DOWN_ASIS_Y) {
            probe->hot_y = base_y;
        } else {
            probe->hot_y = (short)(base_y + offset);
        }
    } else {
        probe->hot_y = base_y;
        if (dir == UW_WALK_DIR_LEFT && base_x < 0x10) {
            probe->hot_x = base_x;
        } else if (dir == UW_WALK_DIR_RIGHT && base_x >= 0xF0) {
            probe->hot_x = base_x;
        } else {
            probe->hot_x = (short)(base_x + offset);
        }
    }

    tile_x = (short)(probe->hot_x - UW_WALK_VISUAL_X_BIAS_PX);
    probe->tile_col = (tile_x < 0) ? (signed char)-1
                                   : (signed char)(tile_x / 8);
    probe->tile_row = (probe->hot_y < UW_WALK_NES_PLAYFIELD_TOP_PX)
        ? (signed char)-1
        : (signed char)((probe->hot_y - UW_WALK_NES_PLAYFIELD_TOP_PX) / 8);
}

unsigned char uw_walk_tile_passable(
    const uw_walk_probe_t *probe,
    unsigned char (*tile_at)(unsigned char col, unsigned char row))
{
    if (probe->hot_y < UW_WALK_NES_PLAYFIELD_TOP_PX) return 0u;
    if (probe->tile_col < 0 || probe->tile_col > 31 ||
        probe->tile_row < 0 || probe->tile_row > 21) {
        return 1u;
    }

    if (!tile_at((unsigned char)probe->tile_col,
                 (unsigned char)probe->tile_row)) {
        return 0u;
    }

    if (probe->has_second_col && probe->tile_col < 31) {
        return tile_at((unsigned char)(probe->tile_col + 1),
                       (unsigned char)probe->tile_row);
    }
    return 1u;
}

unsigned char uw_walk_find_doorway(unsigned char active_doorway_dir,
                                   unsigned char obj_dir,
                                   short x,
                                   short y,
                                   unsigned char *door_dir)
{
    short perp, axis;

    player_coords_for_dir(obj_dir, x, y, &perp, &axis);
    if (active_doorway_dir >= 4u) {
        return search_doorways(perp, axis, s_door_min_over,
                               s_door_max_over, door_dir);
    }

    if (perp == (short)s_door_required_coord[active_doorway_dir] &&
        coord_in_bounds(axis, s_door_min_over, s_door_max_over,
                        active_doorway_dir) &&
        obj_dir == uw_walk_dir_for_door(active_doorway_dir)) {
        return search_doorways(perp, axis, s_door_min_over,
                               s_door_max_over, door_dir);
    }

    return search_doorways(perp, axis, s_door_min_under,
                           s_door_max_under, door_dir);
}

unsigned char uw_walk_modify_dir_in_doorway(unsigned char doorway_dir,
                                            unsigned char obj_dir,
                                            unsigned char input_mask)
{
    unsigned char opposite;

    if (doorway_dir >= 4u || input_mask == 0u) return obj_dir;
    if (input_mask & uw_walk_input_mask_from_dir(obj_dir)) return obj_dir;

    opposite = uw_walk_opposite_dir(obj_dir);
    if (input_mask & uw_walk_input_mask_from_dir(opposite)) return opposite;
    return obj_dir;
}

void uw_walk_snap_to_doorway_axis(unsigned char door_dir, short *x, short *y)
{
    if (door_dir == UW_WALK_DOOR_E || door_dir == UW_WALK_DOOR_W) {
        *y = UW_WALK_DOORWAY_H_Y;
    } else if (door_dir == UW_WALK_DOOR_S || door_dir == UW_WALK_DOOR_N) {
        *x = UW_WALK_DOORWAY_V_X;
    }
}

unsigned char uw_walk_edge_crossing(short x, short y, unsigned char *door_dir)
{
    if (x < UW_WALK_EDGE_WEST_X) {
        *door_dir = UW_WALK_DOOR_W;
        return 1u;
    }
    if (x > UW_WALK_EDGE_EAST_X) {
        *door_dir = UW_WALK_DOOR_E;
        return 1u;
    }
    if (y < UW_WALK_EDGE_NORTH_Y) {
        *door_dir = UW_WALK_DOOR_N;
        return 1u;
    }
    if (y > UW_WALK_EDGE_SOUTH_Y) {
        *door_dir = UW_WALK_DOOR_S;
        return 1u;
    }
    return 0u;
}

void uw_walk_arrival_position(unsigned char door_dir, short *x, short *y)
{
    switch (door_dir) {
        case UW_WALK_DOOR_W:
            *x = UW_WALK_EDGE_EAST_X;
            *y = UW_WALK_DOORWAY_H_Y;
            break;
        case UW_WALK_DOOR_E:
            *x = UW_WALK_EDGE_WEST_X;
            *y = UW_WALK_DOORWAY_H_Y;
            break;
        case UW_WALK_DOOR_N:
            *x = UW_WALK_DOORWAY_V_X;
            *y = UW_WALK_EDGE_SOUTH_Y;
            break;
        case UW_WALK_DOOR_S:
            *x = UW_WALK_DOORWAY_V_X;
            *y = UW_WALK_EDGE_NORTH_Y;
            break;
        default:
            break;
    }
}

unsigned char uw_walk_door_axis_matches(unsigned char door_dir,
                                        unsigned char dir)
{
    if (door_dir == UW_WALK_DOOR_E || door_dir == UW_WALK_DOOR_W)
        return is_horizontal(dir);
    if (door_dir == UW_WALK_DOOR_S || door_dir == UW_WALK_DOOR_N)
        return is_vertical(dir);
    return 0u;
}

unsigned char uw_walk_dir_for_door(unsigned char door_dir)
{
    switch (door_dir) {
        case UW_WALK_DOOR_E: return UW_WALK_DIR_RIGHT;
        case UW_WALK_DOOR_W: return UW_WALK_DIR_LEFT;
        case UW_WALK_DOOR_S: return UW_WALK_DIR_DOWN;
        case UW_WALK_DOOR_N: return UW_WALK_DIR_UP;
        default:             return UW_WALK_DIR_NONE;
    }
}

unsigned char uw_walk_opposite_dir(unsigned char dir)
{
    switch (dir) {
        case UW_WALK_DIR_RIGHT: return UW_WALK_DIR_LEFT;
        case UW_WALK_DIR_LEFT:  return UW_WALK_DIR_RIGHT;
        case UW_WALK_DIR_DOWN:  return UW_WALK_DIR_UP;
        case UW_WALK_DIR_UP:    return UW_WALK_DIR_DOWN;
        default:                return UW_WALK_DIR_NONE;
    }
}

unsigned char uw_walk_input_mask_from_dir(unsigned char dir)
{
    switch (dir) {
        case UW_WALK_DIR_RIGHT: return UW_INPUT_RIGHT;
        case UW_WALK_DIR_LEFT:  return UW_INPUT_LEFT;
        case UW_WALK_DIR_DOWN:  return UW_INPUT_DOWN;
        case UW_WALK_DIR_UP:    return UW_INPUT_UP;
        default:                return 0u;
    }
}
