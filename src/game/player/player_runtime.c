/* NES source: Z_05.asm:InitLinkSpeed/Link_ModifyDirOnGridLine; Z_07:MoveObject.
 * Drained C: existing host speed/direction helpers and player_runtime below.
 * Coverage: PARTIAL (speed, direction decisions and quarter-step integration;
 *           collision, edge and shove callers still use bridges).
 * Stance: EXTEND the existing native kernel with authenticated host bodies.
 */
#include "player_runtime.h"

#define PLAYER_GRID_SIZE 8

static unsigned char opposite_dir(unsigned char dir)
{
    switch (dir) {
    case PLAYER_RUNTIME_DOWN:  return PLAYER_RUNTIME_UP;
    case PLAYER_RUNTIME_UP:    return PLAYER_RUNTIME_DOWN;
    case PLAYER_RUNTIME_LEFT:  return PLAYER_RUNTIME_RIGHT;
    case PLAYER_RUNTIME_RIGHT: return PLAYER_RUNTIME_LEFT;
    default: return PLAYER_RUNTIME_NONE;
    }
}

static unsigned char direction_bit(unsigned char dir)
{
    switch (dir) {
    case PLAYER_RUNTIME_DOWN:  return 4u;
    case PLAYER_RUNTIME_UP:    return 8u;
    case PLAYER_RUNTIME_LEFT:  return 2u;
    case PLAYER_RUNTIME_RIGHT: return 1u;
    default: return 0u;
    }
}

void player_runtime_init_speed(PlayerState *p, unsigned char level,
                               unsigned char collided_tile)
{
    unsigned char speed = 0x60u;
    if (p == 0) return;
    if (level == 0u && (collided_tile == 0x74u || collided_tile == 0x75u)) {
        speed = 0x30u;
        if (p->qspeed_frac != speed) p->pos_frac = 0u;
    }
    p->qspeed_frac = speed;
}

static unsigned char direction_from_bit(unsigned char bit)
{
    if (bit & 8u) return PLAYER_RUNTIME_UP;
    if (bit & 4u) return PLAYER_RUNTIME_DOWN;
    if (bit & 2u) return PLAYER_RUNTIME_LEFT;
    if (bit & 1u) return PLAYER_RUNTIME_RIGHT;
    return PLAYER_RUNTIME_NONE;
}

unsigned char player_runtime_modify_dir_at_grid_point(PlayerState *p,
                                                       unsigned char input_bits,
                                                       unsigned char walkable_bits,
                                                       unsigned char level,
                                                       unsigned char collided_tile)
{
    static const unsigned char axis_masks[4] = { 12u, 12u, 3u, 3u };
    unsigned char count = 0u, walk = 0u, last_input = 0u, last_walk = 0u;
    unsigned char bit, a, latch = 0u, objdir;
    if (p == 0) return PLAYER_RUNTIME_NONE;
    objdir = direction_bit(p->dir);
    p->go_straight = 0u;
    for (bit = 1u; bit <= 8u; bit <<= 1) {
        if ((input_bits & bit) == 0u) continue;
        last_input = bit;
        ++count;
        if (walkable_bits & bit) {
            last_walk = bit;
            ++walk;
        }
    }
    if (count == 0u) return PLAYER_RUNTIME_NONE;
    if (count == 1u) {
        a = last_input;
    } else if (walk == 0u) {
        p->input_dir = last_input;
        return direction_from_bit(last_input);
    } else {
        p->go_straight = 1u;
        a = last_walk;
        if (walk >= 2u && level != 0u) {
            unsigned char x = (unsigned char)p->x, y = (unsigned char)p->y;
            if ((x == 0x20u || x == 0xD0u) && y == 0x85u && (objdir & 4u)) {
                a = objdir;
            } else {
                unsigned char perpendicular = 1u;
                a = objdir;
                latch = p->go_straight_when_diag;
                if (latch != 0u &&
                    (x != 0x78u || y != 0x5Du || (a & 3u) == 0u))
                    perpendicular = 0u;
                if (perpendicular) {
                    unsigned char reverse = (objdir & 8u) ? 0u :
                                            (objdir & 4u) ? 1u :
                                            (objdir & 2u) ? 2u : 3u;
                    latch = (unsigned char)(latch + 1u);
                    a = (unsigned char)(input_bits ^ (input_bits & axis_masks[reverse]));
                }
            }
        }
    }
    p->go_straight_when_diag = latch;
    p->input_dir = a;
    p->dir = direction_from_bit(a);
    player_runtime_init_speed(p, level, collided_tile);
    return p->dir;
}

void player_runtime_modify_dir_on_grid_line(PlayerState *p,
                                            unsigned char input_dir,
                                            unsigned char level,
                                            unsigned char collided_tile)
{
    signed char off;
    unsigned char magnitude, negative_facing;
    if (p == 0 || input_dir == PLAYER_RUNTIME_NONE) return;
    off = p->grid_offset;
    magnitude = (unsigned char)(off < 0 ? -off : off);
    if (input_dir == p->dir) {
        player_runtime_init_speed(p, level, collided_tile);
        return;
    }
    if (input_dir == opposite_dir(p->dir)) {
        p->dir = input_dir;
        p->input_dir = direction_bit(input_dir);
        return;
    }
    if (p->go_straight) {
        player_runtime_init_speed(p, level, collided_tile);
        return;
    }
    if (magnitude >= 4u) return;
    negative_facing = (p->dir == PLAYER_RUNTIME_LEFT || p->dir == PLAYER_RUNTIME_UP);
    if (negative_facing ? (off >= 0) : (off < 0)) return;
    p->dir = opposite_dir(p->dir);
    p->grid_offset = (signed char)((off < 0) ? (8 + off) : (-8 + off));
}

void player_runtime_move_object(PlayerState *p, unsigned char dir)
{
    /* NES source: Z_07.asm:MoveObject; drained objrt_move_object.
     * Coverage: FULL native Link integration. Stance: EXTEND the existing
     * object_dispatch.c folded quarter-step arithmetic. No caller can
     * observe intermediate steps; fraction advances even at a grid limit. */
    unsigned short full_speed, total;
    unsigned char positive, steps, grid, to_positive_limit, to_negative_limit;
    signed short delta;
    if (p == 0 || dir < PLAYER_RUNTIME_DOWN || dir > PLAYER_RUNTIME_RIGHT)
        return;
    positive = (unsigned char)(dir == PLAYER_RUNTIME_DOWN ||
                               dir == PLAYER_RUNTIME_RIGHT);
    full_speed = (unsigned short)((unsigned short)p->qspeed_frac << 2);
    if (positive) {
        total = (unsigned short)(p->pos_frac + full_speed);
        steps = (unsigned char)(total >> 8);
        p->pos_frac = (unsigned char)total;
    } else {
        total = (unsigned short)(full_speed + 255u - p->pos_frac);
        steps = (unsigned char)(total >> 8);
        p->pos_frac = (unsigned char)(p->pos_frac - full_speed);
    }
    if (steps == 0u) return;
    grid = (unsigned char)p->grid_offset;
    if (positive) {
        to_positive_limit = (unsigned char)(PLAYER_GRID_SIZE - grid);
        to_negative_limit = (unsigned char)(0xF8u - grid);
    } else {
        to_positive_limit = (unsigned char)(grid - PLAYER_GRID_SIZE);
        to_negative_limit = (unsigned char)(grid - 0xF8u);
    }
    if (steps > to_positive_limit) steps = to_positive_limit;
    if (steps > to_negative_limit) steps = to_negative_limit;
    delta = positive ? (signed short)steps : (signed short)(0 - (signed short)steps);
    p->grid_offset = (signed char)(grid + delta);
    if (dir == PLAYER_RUNTIME_LEFT || dir == PLAYER_RUNTIME_RIGHT)
        p->x = (signed short)(p->x + delta);
    else
        p->y = (signed short)(p->y + delta);
}

unsigned char player_runtime_filter_doorway_input(unsigned char in_doorway,
                                                  unsigned char input_dir,
                                                  unsigned char link_dir)
{
    unsigned char opposite;
    if (in_doorway == 0u || input_dir == 0u) {
        return input_dir;
    }
    if (link_dir & input_dir) {
        return link_dir;
    }
    opposite = (link_dir & 0x01u) ? 0x02u :
               (link_dir & 0x02u) ? 0x01u :
               (link_dir & 0x04u) ? 0x08u :
               (link_dir & 0x08u) ? 0x04u : 0;
    if (opposite & input_dir) {
        return opposite;
    }
    return link_dir;
}

void player_runtime_finish_grid_cell(PlayerState *p, unsigned char game_mode,
                                     unsigned char *underground_exit_type)
{
    if (p == 0) {
        return;
    }
    /* Link_EndMoveAndAnimate truncates any nonzero multiple of 8. */
    if (p->grid_offset != 0 &&
        (((unsigned char)p->grid_offset) & 7u) == 0u) {
        p->grid_offset = 0;
        if (game_mode == 5u && underground_exit_type != 0) {
            *underground_exit_type = 0u;
        }
    }
}


/* NES Obj_Shove: Link policy only. Enemy-specific 16-pixel alignment remains
 * in its existing drain. Arithmetic and four-pixel update ordering are retained. */
void player_runtime_shove(PlayerState *p, const PlayerShoveWorld *world,
                           PlayerShoveTrace *trace)
{
    unsigned char dir = p->shove_dir;
    trace->tile_queried = trace->bounds_sampled = 0u;
    trace->started = trace->stepped = 0u;
    if (dir & 0x80u) {
        unsigned char facing = direction_bit(p->dir);
        dir &= 0x7Fu;
        p->shove_dir = dir;
        if (facing < 3u ? ((dir & 12u) == 0u) : ((dir & 3u) == 0u)) return;
        if (p->grid_offset != 0) {
            /* GetOppositeDir tests the lowest set bit; zero's authenticated
             * NES code-table fallthrough is $A9 (existing core drain). */
            p->shove_dir = (facing & 1u) ? 2u : (facing & 2u) ? 1u :
                           (facing & 4u) ? 8u : (facing & 8u) ? 4u : 0xA9u;
        }
        return;
    }
    if (p->shove_distance == 0u) {
        p->shove_dir = 0u;
        return;
    }
    trace->started = 1u;
    trace->pixel_budget = 4u;
    do {
        dir = (unsigned char)(p->shove_dir & 15u);
        if (p->grid_offset == 0) {
            p->x = (unsigned char)((unsigned char)p->x & 0xF8u);
            p->y = (unsigned char)(((unsigned char)p->y & 0xF8u) | 5u);
            trace->moving_bits = dir;
            trace->hotspot = (dir & 5u) ? ((dir & 4u) ? 8u : 16u) : 0xF8u;
            trace->tile_query = terrain_query_tile(&world->terrain,
                (unsigned char)p->x, (unsigned char)p->y, dir, trace->hotspot, 1u);
            trace->tile_queried = 1u;
            trace->bounds_sampled = 0u;
            if (trace->tile_query.tile >= world->first_obstacle ||
                (world->remix && trace->tile_query.tile >= 0x54u && trace->tile_query.tile < 0x60u)) {
                p->shove_dir = p->shove_distance = 0u;
                return;
            }
        }
        trace->moving_bits = dir;
        if ((unsigned char)p->x < world->left) {
            if (dir & 2u) trace->moving_bits = 0u;
        } else if ((unsigned char)p->x >= world->right) {
            if (dir & 1u) trace->moving_bits = 0u;
        }
        trace->bounds_sampled = 1u;
        trace->boundary_y = (unsigned char)p->y;
        if (trace->boundary_y < world->top) {
            if (trace->moving_bits & 8u) trace->moving_bits = 0u;
        } else if (trace->boundary_y >= world->bottom) {
            if (trace->moving_bits & 4u) trace->moving_bits = 0u;
        }
        if (trace->moving_bits == 0u ||
            (world->person_blocking && (unsigned char)p->y < 0x8Eu &&
             (trace->moving_bits & 8u))) {
            trace->moving_bits = 0u;
            p->shove_dir = p->shove_distance = 0u;
            return;
        }
        trace->stepped = 1u;
        trace->delta = (p->shove_dir & 5u) ? 1u : 0xFFu;
        p->shove_distance = (unsigned char)(p->shove_distance - 1u);
        {
            unsigned char grid = (unsigned char)((unsigned char)p->grid_offset + trace->delta);
            p->grid_offset = (signed char)((grid & 7u) ? grid : 0u);
        }
        if (p->shove_dir & 3u)
            p->x = (unsigned char)((unsigned char)p->x + trace->delta);
        else
            p->y = (unsigned char)((unsigned char)p->y + trace->delta);
        --trace->pixel_budget;
    } while (trace->pixel_budget != 0u);
}
