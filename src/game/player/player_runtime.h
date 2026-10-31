#ifndef PLAYER_RUNTIME_H
#define PLAYER_RUNTIME_H

#include "../../state/player_state.h"

enum PlayerRuntimeDirection {
    PLAYER_RUNTIME_NONE = 0,
    PLAYER_RUNTIME_DOWN = 1,
    PLAYER_RUNTIME_UP = 2,
    PLAYER_RUNTIME_LEFT = 3,
    PLAYER_RUNTIME_RIGHT = 4
};

/* T-367: native movement integration.
 *
 * Direction values match engine's link_dir_t: 1=down, 2=up, 3=left,
 * 4=right. The functions own PlayerState position/q-speed/grid math; legacy
 * collision callers publish only the cells they require.
 */
void player_runtime_move_object(PlayerState *p, unsigned char dir);
void player_runtime_init_speed(PlayerState *p, unsigned char level,
                               unsigned char collided_tile);
void player_runtime_modify_dir_on_grid_line(PlayerState *p,
                                            unsigned char input_dir,
                                            unsigned char level,
                                            unsigned char collided_tile);
unsigned char player_runtime_modify_dir_at_grid_point(PlayerState *p,
                                                       unsigned char input_bits,
                                                       unsigned char walkable_bits,
                                                       unsigned char level,
                                                       unsigned char collided_tile);
void player_runtime_finish_grid_cell(PlayerState *p, unsigned char game_mode,
                                     unsigned char *underground_exit_type);
unsigned char player_runtime_filter_doorway_input(unsigned char in_doorway,
                                                  unsigned char input_dir,
                                                  unsigned char link_dir);

/* Explicit inputs for Link's four-pixel shove. Room/terrain ownership remains
 * a declared world view; the kernel never borrows another player or NES RAM. */
#include "../combat/terrain_query.h"
typedef struct {
    TerrainQueryView terrain;
    unsigned char left, right, top, bottom;
    unsigned char first_obstacle, remix, person_blocking;
} PlayerShoveWorld;
/* Call-local outputs for temporary ABI consumers; never persistent state. */
typedef struct {
    TerrainTileResult tile_query;
    unsigned char tile_queried, bounds_sampled, started, stepped;
    unsigned char moving_bits, boundary_y, delta, pixel_budget, hotspot;
} PlayerShoveTrace;
void player_runtime_shove(PlayerState *player, const PlayerShoveWorld *world,
                           PlayerShoveTrace *trace);
/* Transitional producer/observer adapter; damage still owns legacy shove cells. */
void player_runtime_shove_legacy_bridge(PlayerState *player);

#endif /* PLAYER_RUNTIME_H */
