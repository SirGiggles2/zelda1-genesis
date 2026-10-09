/* uw_person_dispatch.h — native underworld person subsystem dispatch
 * (Phase 4).
 *
 * Native rewrite of select leaves from
 * src/oracle/cave/uw_person_runtime.c. Both ROMs link.
 *
 * Phase 4 first batch: trivial leaves that don't call c_draw_object_*
 * / c_animate_item_object / c_update_person_state_textbox shims —
 * those defer until a native draw + textbox port lands.
 */

#ifndef UW_PERSON_DISPATCH_H
#define UW_PERSON_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* CAVE_DELAY_TIMER = 10; cue transfer buf 118; advance state.
 * NES UpdateLifeOrMoneyState_0. drain at uw_person_runtime.c:88-91. */
void uw_person_update_life_or_money_state_0(void);

/* If LINK_MOVING_DIR has the up bit ($08) set and Link's tile-Y is
 * above $8E, clear LINK_MOVING_DIR (block walk-through). NES
 * CheckPersonBlocking. drain at uw_person_runtime.c:116-122. */
void uw_person_check_person_blocking(void);

/* set_room_flag_uw_item_state; CAVE_DELAY_TIMER = 64;
 * cue_transfer_buf_and_advance_state(30). NES
 * PersonFlagItemTakenAndAdvanceState.
 * drain at uw_person_runtime.c:110-114. */
void uw_person_flag_item_taken_and_advance_state(void);

/* If OBJ_STATE($0F) has bit 7 set (grumble Link bumped), set Link
 * action timer + sfx + advance state. NES UpdateGrumble1.
 * drain at uw_person_runtime.c:124-131. */
void uw_person_update_grumble1(void);

/* Init underworld-person variant B (older man). Sets up cave objs,
 * picks text selector by obj type, plays char sfx. NES
 * InitUnderworldPersonB. drain at uw_person_runtime.c:34-43. */
void uw_person_init_underworld_person_b(unsigned int slot);

/* If room item already taken, destroy person + clear obj-state;
 * otherwise play char sfx. NES UnderworldPerson_DestroyIfTaken.
 * drain at uw_person_runtime.c:93-101. */
void uw_person_destroy_if_taken(unsigned int slot);

/* Init underworld-person variant A (woman/store). Sets up cave objs,
 * picks text selector A by obj type. If room is $4F (life-or-money),
 * dispatch to destroy_if_taken; otherwise play char sfx. NES
 * InitUnderworldPersonA. drain at uw_person_runtime.c:133-143. */
void uw_person_init_underworld_person_a(unsigned int slot);

/* Init life-or-money person: same as variant A's tail but always
 * dispatches to destroy_if_taken. NES
 * InitUnderworldPersonLifeOrMoney_Full. drain at uw_person_runtime.c:103-108. */
void uw_person_init_life_or_money_full(unsigned int slot);

/* Init grumble (full): set up cave objs, set CAVE_TEXT_SELECTOR=36,
 * CAVE_TEXT_LINE_ADDR_LO=$A4. If room flag clear, play char sfx;
 * else clear OBJ_STATE(0)+CAVE_ROOM_TYPE. NES InitGrumble_Full.
 * drain at uw_person_runtime.c:62-74. */
void uw_person_init_grumble_full(unsigned int slot);

/* Init underworld-person variant C (boss room old man / triforce
 * room). Sets up cave objs, plays char sfx, picks text selector C
 * by obj type. If obj_type == 0x4B and triforce-pieces == $FF,
 * trigger shutter + clear OBJ_STATE(0) + destroy monster. NES
 * InitUnderworldPersonC. drain at uw_person_runtime.c:45-60. */
void uw_person_init_underworld_person_c(unsigned int slot);

/* Init rupee stash spawn (10 rupee monsters in fixed pattern).
 * NES InitRupeeStash_Full. drain at uw_person_runtime.c:76-86. */
void uw_person_init_rupee_stash_full(unsigned int slot);

/* Underworld old-man bomb upgrade if Link aligned to (X=$78,Y=~$98)
 * with >=100 rupees. Spends 100 rupees, gives +4 max bomb capacity.
 * NES UpdateComplexState_SenseLink. drain at uw_person_runtime.c:145-163. */
void uw_person_update_complex_state_sense_link(void);

/* Take heart-or-rupee item: 50 rupees buys quarter-heart loss; the
 * other slot deducts a heart container. NES
 * UpdateUnderworldPersonLifeOrMoneyState_2. drain at
 * uw_person_runtime.c:165-198. */
void uw_person_update_life_or_money_state_2(void);

/* Draw the 2 life-or-money cave items (heart container + rupee
 * stash) at fixed positions ($58, $98) and ($98, $98). NES
 * DrawLifeOrMoneyItems. drain at uw_person_runtime.c:210-217. */
void uw_person_draw_life_or_money_items(void);

/* Run check_monster_collisions (full battery), then if statue-person
 * fireball state was set on slot 0, copy it to ENEMY_STATUE_PERSON_FIREBALLS
 * and clear it. NES PersonCheckCollisions.
 * drain at uw_person_runtime.c:200-208. */
void uw_person_person_check_collisions(unsigned int slot);

/* uw_person_person_check_collisions then anim_fetch_obj_pos +
 * draw_object_mirrored. NES Person_DrawAndCheckCollisions.
 * drain at uw_person_runtime.c:28-32. */
void uw_person_person_draw_and_check_collisions(unsigned int slot);

/* Top-level UW-person update dispatchers — state-machine drivers
 * that consume CAVE_PERSON_STATE. Textbox state arms now route to
 * cave's native char-streamer via uw_person_state_textbox (post-D3
 * pointer-array PersonTextAddrs). NES UpdateUnderworldPerson_Complex
 * et al. drain at uw_person_runtime.c:232+. */
void uw_person_update_person_complex(unsigned int slot);
void uw_person_update_person_full(unsigned int slot);
void uw_person_update_grumble_full(unsigned int slot);
void uw_person_update_life_or_money_full(unsigned int slot);
void uw_person_update_grumble3(void);

#ifdef __cplusplus
}
#endif

#endif /* UW_PERSON_DISPATCH_H */
