#ifndef LINK_LADDER_H
#define LINK_LADDER_H

/* Link's ladder, on NES RAM (T-056).
 *
 * NES source: Z_07.asm Link_EndMoveAndAnimate (@CheckLadderRoom ..
 *             @CheckWarps: LadderRoomsOW, LinkToLadderOffsetsX/Y,
 *             FindEmptyMonsterSlot, ResetShoveInfo), Z_05.asm CheckLadder
 *             (@StashLadder, @HandleInput, @DrawLadder,
 *             SetMovingDirAndSwitchToPlayerSlot), Z_01.asm GetOppositeDir
 *             / Abs.
 * Drained C:  none (no _runtime.c candidate for these routines).
 *             Callees: collision_get_colliding_tile_moving
 *             (collision_dispatch.c), core_destroy_monster
 *             (core_dispatch.c), draw_static_item_sprites
 *             (draw_dispatch.c).
 * Coverage:   FULL for the routines above, routine level: tools/audit/
 *             asm_equiv/ runs the original 6502 code against this C (same
 *             callee stubs, random RAM). Integration (main.c hooks, deferred
 *             draw, rendering) needs the Windows lockstep presets t056_*.
 * Stance:     GREENFIELD port of unported NES code (drain_coverage: no
 *             candidate). The ladder object is type $5F; its
 *             UpdateObject entry is DoNothing (InitTileObjOrItem on the
 *             first update), already handled by enemy_loop. */

/* Link_EndMoveAndAnimate's ladder half: GameMode 5, ObjGridOffset 0,
 * ladder room (any UW room, OW rooms $17 $18 $19 $27 $4F $5F), no
 * doorway, InvLadder, Link not halted, no ladder out, water ahead in
 * ObjDir (UW $F4, OW $8D..$98), a free monster slot and ObjInputDir ==
 * ObjDir: the ladder object goes into that slot (LadderSlot $64).
 * Call after Link moved (ObjGridOffset truncated), before AnimateLinkBase. */
void link_ladder_end_move(void);

/* CheckLadder: Walker_Move for Link after Walker_CheckTileCollision,
 * before MoveObject. [0F] is the moving direction in and out. Puts the
 * ladder away when Link left it; on the ladder it overrides [0F] and
 * queues the ladder draw (link_ladder_draw). No-op without a ladder. */
/* Returns the NES MoveObject slot: 0 for Link, ladder slot when stashed. */
unsigned char link_ladder_check(void);

/* @DrawLadder's Anim_FetchObjPosForSpriteDescriptor +
 * Anim_WriteStaticItemSpritesWithAttributes (item slot $0C, attr 0),
 * deferred: the NES draws during UpdatePlayer; the Genesis object sprite
 * cache is cleared after UpdatePlayer, so the play tick calls this right
 * after that clear. The draw belongs to the ladder slot. */
void link_ladder_draw(void);

#endif
