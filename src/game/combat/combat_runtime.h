#ifndef ROOMROM_COMBAT_H
#define ROOMROM_COMBAT_H

/* RoomRom S7 v4: sword swing.
 *
 * NES Z1 reference (reference/aldonunez/Z_05.asm WieldSword + Z_07.asm
 * UpdateSwordOrRod + PlayerToWeaponOffsetsX/Y at Z_07:4337):
 *
 *   - Player ObjState set to $10 (wielding) on swing start.
 *   - Sword has 5 visible-window states:
 *       state 1 (5 frames): windup, sword raised UP regardless of
 *                            facing.
 *       state 2 (8 frames): full extend in facing direction (the
 *                            "slash").
 *       state 3 (1 frame):  mid-retract.
 *       state 4 (1 frame):  almost retracted.
 *       state 5 (1 frame):  invisible — sword sprite hidden, Link's
 *                            body returns to walk pose.
 *   - Total swing window: 16 frames. Re-swing locked the entire window.
 *
 * Body sprite changes during the swing (states 1-4): Link draws as
 * attack pose tiles ($14/$16 down, $18/$1A up, $10/$12 left/right).
 * See roomrom_sprites_set_link_attack_pose.
 *
 * Sword tiles (NES Anim_ItemFrameTiles, Z_01.asm:5202):
 *   vertical (UP/DOWN, state 1, states 2-4 vertical facings) = $20
 *   horizontal (LEFT/RIGHT in states 2-4) = $82
 */

#include "../../src/game/world/render/sprite_render.h"

void roomrom_combat_init(void);
void roomrom_combat_try_swing(link_face_t face, short link_x, short link_y);
void roomrom_combat_update(short link_x, short link_y, link_face_t face);
unsigned char roomrom_combat_link_locked(void);
/* T-116: NES Link item-use state (see combat_runtime.c). */
void link_place_weapon_for_player_state(unsigned char and_anim);
void link_anim_state_step(void);
void link_step_after_wield(void);
unsigned char link_item_use_blocks_move(void);

/* Recompute sword visual Y bias for overworld vs. dungeon sprite baselines.
 * The beam keeps NES object coordinates; only renderer-local draw offsets
 * are applied for it. 0 = OW, 1 = UW. */
void roomrom_combat_set_uw(unsigned char in_uw);

/* Redux mode toggle (per docs/audit/redux_touchpoints.md, diagonal
 * sword + ALttP-style sword arc are out-of-scope IPS extensions of
 * NES Z1). v11 hooks the flag so future patches can vary timing or
 * dispatch alternate animation paths without touching the cardinal
 * combat state machine. Currently a no-op flag — reserved for v11+. */
void roomrom_combat_set_redux(unsigned char redux);

/* Plan v5 sword/enemy collision sync. NES weapon slot 13 holds the
 * sword; collision_check_monster_sword_collision tests OBJ_STATE(13)
 * == 2 (= state 2 = full extend). Expose the current swing pose so
 * nes_ram_sync_sword can publish the NES-side weapon slot each tick.
 *
 * roomrom_combat_get_swing_state: 0 = idle, 2 = full extend (NES
 * state 2; the only state collision engine considers "active"). */
unsigned char roomrom_combat_get_swing_state(void);
short         roomrom_combat_get_swing_x(void);
short         roomrom_combat_get_swing_y(void);
link_face_t   roomrom_combat_get_swing_face(void);

/* T-116: WieldRod (rod slot $12) and its UpdateRodOrArrow states $3x. */
void roomrom_combat_wield_rod(void);
void roomrom_combat_end_move_and_animate(void);
/* AnimateLinkBase alone (Link_EndMoveAndDraw callers: halted Link). */
void roomrom_combat_animate_link_base(void);
void roomrom_combat_update_rod(void);

#endif
