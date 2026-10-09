#ifndef ROOMROM_UW_WALK_MODEL_H
#define ROOMROM_UW_WALK_MODEL_H

/* NES source: reference/aldonunez/Z_07.asm:GetCollidingTileMoving,
 *             GetCollidableTile; reference/aldonunez/Z_05.asm:
 *             Link_ModifyDirInDoorway, CheckDoorway
 * Drained C:  NONE in engine active scope
 * Coverage:   PARTIAL (Link walking, wall samples, doorway geometry)
 * Stance:     REPLACE
 */

#define UW_WALK_DIR_NONE  0u
#define UW_WALK_DIR_DOWN  1u
#define UW_WALK_DIR_UP    2u
#define UW_WALK_DIR_LEFT  3u
#define UW_WALK_DIR_RIGHT 4u

#define UW_WALK_DOOR_NONE 0xFFu
#define UW_WALK_DOOR_E 0u
#define UW_WALK_DOOR_W 1u
#define UW_WALK_DOOR_S 2u
#define UW_WALK_DOOR_N 3u

#define UW_WALK_VISUAL_X_BIAS_PX 0
/* GetCollidableTile subtracts NES HUD height $40. The Genesis
 * display crop is not part of the gameplay/tile-cache coordinate space. */
#define UW_WALK_NES_PLAYFIELD_TOP_PX 0x40
#define UW_WALK_DOORWAY_H_Y 0x85
#define UW_WALK_DOORWAY_V_X (0x78 + UW_WALK_VISUAL_X_BIAS_PX)
#define UW_WALK_DOORWAY_W_MIN (0x00 + UW_WALK_VISUAL_X_BIAS_PX)
#define UW_WALK_DOORWAY_W_MAX (0x21 + UW_WALK_VISUAL_X_BIAS_PX)
#define UW_WALK_DOORWAY_E_MIN (0xCF + UW_WALK_VISUAL_X_BIAS_PX)
#define UW_WALK_DOORWAY_E_MAX (0xF1 + UW_WALK_VISUAL_X_BIAS_PX)
#define UW_WALK_DOORWAY_N_MIN 0x35
#define UW_WALK_DOORWAY_N_MAX 0x56
#define UW_WALK_DOORWAY_S_MIN 0xB5
#define UW_WALK_DOORWAY_S_MAX 0xD6
#define UW_WALK_DOWN_ASIS_Y 0xDD

#define UW_WALK_EDGE_WEST_X 0
#define UW_WALK_EDGE_EAST_X 240
#define UW_WALK_EDGE_NORTH_Y 56
#define UW_WALK_EDGE_SOUTH_Y 208

typedef struct {
    short hot_x;
    short hot_y;
    signed char tile_col;
    signed char tile_row;
    unsigned char has_second_col;
} uw_walk_probe_t;

void uw_walk_collidable_probe(unsigned char dir, short x, short y,
                              uw_walk_probe_t *probe);

unsigned char uw_walk_tile_passable(
    const uw_walk_probe_t *probe,
    unsigned char (*tile_at)(unsigned char col, unsigned char row));

unsigned char uw_walk_find_doorway(unsigned char active_doorway_dir,
                                   unsigned char obj_dir,
                                   short x,
                                   short y,
                                   unsigned char *door_dir);

unsigned char uw_walk_modify_dir_in_doorway(unsigned char doorway_dir,
                                            unsigned char obj_dir,
                                            unsigned char input_mask);

void uw_walk_snap_to_doorway_axis(unsigned char door_dir, short *x, short *y);

unsigned char uw_walk_edge_crossing(short x, short y, unsigned char *door_dir);

void uw_walk_arrival_position(unsigned char door_dir, short *x, short *y);

unsigned char uw_walk_door_axis_matches(unsigned char door_dir,
                                        unsigned char dir);
unsigned char uw_walk_dir_for_door(unsigned char door_dir);
unsigned char uw_walk_opposite_dir(unsigned char dir);
unsigned char uw_walk_input_mask_from_dir(unsigned char dir);

#endif /* ROOMROM_UW_WALK_MODEL_H */
