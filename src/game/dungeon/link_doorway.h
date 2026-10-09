#ifndef LINK_DOORWAY_H
#define LINK_DOORWAY_H

/* Link input filtering and UW doorways, on NES RAM (T-131).
 *
 * NES source: Z_05.asm Link_FilterInput / MaskInputInBorder / BorderBounds,
 *             Link_ModifyDirInDoorway, CheckDoorway (DoorwayRequiredCoord,
 *             DoorwayBounds*), GetPlayerCoordsForDirection,
 *             FindDoorAttrByDoorBit, TouchDoor*; Z_07.asm Walker_Move
 *             (Link's BoundByRoom / CheckDoorway / Walker_CheckTileCollision
 *             order), GoWalkableDir / CheckScreenEdge.
 * Drained C:  room_player_link_modify_dir_in_doorway (room_dispatch.c),
 *             object_bound_by_room (object_dispatch.c),
 *             collision_get_colliding_tile_moving (collision_dispatch.c).
 * Coverage:   FULL for the routines above.
 * Stance:     REPLACE the Genesis doorway model (walk_model.c
 *             uw_walk_find_doorway: horizontal doorway Y $85 and N/S
 *             bounds 8 px off NES, which blocked Link at the W door once
 *             door tiles collided like NES, T-128). */

/* Link_FilterInput: while Link is idle (ObjState 0), mask A near the room
 * border he faces and, in the UW, the perpendicular input directions
 * outside the inner border (ButtonsPressed $F8, ObjInputDir $3F8). */
void link_filter_input(void);

/* Link_ModifyDirInDoorway (UW): in a doorway keep ObjInputDir on the
 * doorway axis. */
void link_modify_dir_in_doorway(void);

/* Walker_Move's Link-only UW checks, after the tile-object / person
 * blocking: BoundByRoom (outside doorways) and CheckDoorway on the moving
 * direction [0F]. [0E] ends as the doorway index or $FF (door blocks).
 * Returns 1 when Link walked into an open false or bombable wall
 * (CheckDoorway -> GoToNextModeFromPlay). */
unsigned char link_uw_walker_checks(void);

/* Walker_CheckTileCollision for Link, after link_uw_walker_checks (UW) or
 * directly (OW): leaves [0F] as the moving direction, 0 when blocked.
 * Returns the edge direction (NES bit) when CheckScreenEdge fires
 * (GoToNextModeFromPlay; ObjDir is set to it), else 0. grid_offset is
 * Link's ObjGridOffset. */
unsigned char link_walker_check_tile_collision(signed char grid_offset);

#endif
