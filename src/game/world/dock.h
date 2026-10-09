#ifndef DOCK_H
#define DOCK_H

/* OW dock and raft, on NES RAM (T-056).
 *
 * NES source: Z_04.asm UpdateDock (RaftDirections, PlaySecretFoundTune,
 *             @HandleOtherStates, @ResetLinkAndRaftStateAndDraw, @Draw),
 *             object type $61 (UpdateObject_JumpTable).
 * Drained C:  none for UpdateDock. Callees: room_go_to_next_mode_from_play
 *             (room_dispatch.c), sprite_animate_object_walking
 *             (sprite_dispatch.c), draw_static_item_sprites
 *             (draw_dispatch.c); Link_EndMoveAndAnimate_Bank4 =
 *             roomrom_main_link_end_move_from_object (Link owner, main.c).
 * Coverage:   FULL for UpdateDock, routine level (tools/audit/asm_equiv/,
 *             Link_EndMoveAndAnimate and AnimateObjectWalking stubbed on
 *             both sides). Integration (Link sync, scroll start, raft CHR)
 *             needs the Windows lockstep preset t056_raft.
 * Stance:     GREENFIELD (drain_coverage: no candidate). The dock is set
 *             up in rooms $3F / $55 by room_setup_tile_object_ow and
 *             initialised by InitTileObjOrItem (enemy_loop). */

/* UpdateDock: with the raft, Link at the dock X ($80 room $55, else $60)
 * and Y $3D (arriving from the north: raft goes down, state 1) or $7D
 * (on the dock: raft goes up, state 2) is halted on the raft; it moves
 * him one pixel a frame to Y $7F (grid offset 2) or to $3D, where
 * GoToNextModeFromPlay scrolls north. */
void world_update_dock(unsigned int slot);

#endif
