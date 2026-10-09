/* cave_dispatch.h — native cave gamemode entry (debate 006 D2).
 *
 * Both ROMs link this. Zelda.md (post-cutover) replaces
 * src/gen/z_01.c cavert_init_cave / cavert_update_cave_person /
 * cavert_draw_cave_person callsites with these. engine calls
 * directly from main.c SCENE_CAVE dispatch.
 *
 * Native impl mirrors src/oracle/cave/cave_runtime.c reference
 * (verified MATCH per Gate 1 findings 3_2, 3_2b, 3_4) and
 * reference/aldonunez/Z_01.asm spec. NO transpile-bridge shims
 * (z01_/z07_/c_/progrt_) — pure C + src/state/cave_state.h typed
 * struct + src/sgdk_adapter/ render API.
 *
 * Scope (first iteration):
 *   cave_init   — populate CaveState + upload cave palette
 *   (cave person: cave_update_cave_person from the enemy object loop)
 *   cave_exit   — restore overworld state
 *
 * Phase 12 promotion target: this file becomes Zelda.md's primary
 * cave gameplay path; src/oracle/cave/ retires.
 */

#ifndef CAVE_DISPATCH_H
#define CAVE_DISPATCH_H

/* Avoid stdint.h here: under SGDK_GCC the toolchain's types.h defines
 * uint8_t / uint32_t as macros aliasing SGDK's u8 / u32, which collides
 * with the project's src/stdint.h shim (typedef-based). cave_dispatch.h
 * is a public header included by both Zelda.md (no SGDK_GCC) and engine
 * (with SGDK_GCC), so use plain C types in the API. cave_state.h needs
 * stdint.h types internally — gate by SGDK_GCC there too if/when needed. */

#ifdef __cplusplus
extern "C" {
#endif

/* Cave identifier per NES Z_01.asm CaveSpec table.
 * 0x6A..0x7C maps to cave room types (sword/heart/shop/old man/etc.). */
typedef unsigned char cave_id_t;

/* Initialize cave subsystem for the given cave_id. Populates
 * CaveState typed struct + uploads cave BG CHR + loads palette via
 * src/sgdk_adapter/ render API. Idempotent — caller may re-init if
 * cave_id changes. Returns 0 on success, -1 on invalid cave_id. */
int cave_init(cave_id_t cave_id);

/* T-143: NES InitCave (Z_01.asm:69) — InitObject of the cave person in
 * `slot` (types >= $6A), run by the enemy object loop. */
void cave_init_person(unsigned int slot);

/* Per-frame cave tick. Called once per VBlank from gameplay loop.
 * Currently stub (returns immediately). NPC/shop/text logic ports
 * per-function from oracle reference in subsequent commits. */

/* Cave exit handler. Saves any persistent state to the typed CaveState
 * + signals scene transition back to overworld. Caller (engine main
 * or Zelda.md gamemode dispatch) handles the actual scene swap. */
void cave_exit(void);

/* Read accessor: which cave is currently active. Returns 0 if no
 * cave entered since last cave_exit. */
cave_id_t cave_current_id(void);

/* Draw a cave NPC sprite for the given object slot.
 *
 * Native rewrite of NES Z_01.asm DrawCavePerson (lines 370-383),
 * verified MATCH per Gate 1 finding 3_4. NES dispatch logic:
 *
 *   JSR Anim_FetchObjPosForSpriteDescriptor
 *   LDY ObjType+1
 *   CPY #$7B
 *   BCS NotMirrored          ; ObjType+1 >= 0x7B
 *   JMP DrawObjectMirrored   ; otherwise
 *   NotMirrored: JMP DrawObjectNotMirrored
 *
 * Stage 1 (this commit): port branch shape only. The actual sprite
 * descriptor fetch + SAT writes are stubs — Phase 4 cross-subsystem
 * native object_draw port replaces them with render_sat_write-based
 * impl. Zelda.md gates this via NATIVE_CAVE_DRAW (default OFF), so
 * stage-1 stub never runs in shipping path. engine links it but
 * doesn't call it yet (cave_tick is also stub). */
void cave_draw_person(unsigned int slot);

/* Draw the cave's wares row + price rupee.
 *
 * Native rewrite of NES Z_01.asm DrawCaveItems (lines 388-440), MATCH
 * verdict per Phase 3 summary. Two cave-flag conditions:
 *
 *   CAVE_FLAGS & 0x04 (show items)  -> loop wares 2..0, position via
 *                                      ware_xs[i] / Y=$98, draw item if
 *                                      not $3F (sentinel).
 *   CAVE_FLAGS & 0x08 (show prices) -> draw rupee sprite at ($30, $AB).
 *
 * Stage-1: positioning + flag dispatch native; underlying item_object
 * draw is Phase 4 deferred (c_animate_item_object stub). NES table
 * CaveWareXs = $58/$78/$98 baked in as ware_xs[]. */
void cave_draw_items(void);

/* One state of the cave-person update state machine: format ware prices
 * into the textbox transfer buffer, OR if prices aren't being shown,
 * simply advance person_state by one (state++). Mirrors NES
 * UpdateCavePersonState_TransferPrices (Z_01.asm:442) — drain MATCH
 * (verdict) per Phase 3 summary.
 *
 * Stage-1: state-advance branch native; price formatter (BCD digit
 * write into transfer buf) Phase 4 deferred — that helper chains into
 * cavert_write_prices_to_dynamic_transfer_buf + cavert_format_decimal_byte
 * which need their own ports first. */
void cave_update_transfer_prices(void);

/* State arm: handle Link talking to a shop person, paying door charges,
 * and ware-purchase loop. Mirrors NES UpdateCavePersonState_TalkOrShopOrDoorCharge
 * (Z_01.asm:666). Drain at src/oracle/cave/cave_runtime.c:228-283.
 *
 * Three sub-branches:
 *   1. CAVE_FLAGS & 0x01 not set  -> set state=8, door-repair $20-bump
 *                                    if ROOM_TYPE=0x71.
 *   2. door_repair_rupee_delta!=0 -> wait (return).
 *   3. ware purchase loop (slots 2..0): collision check vs Link pos,
 *                                    rupee/heart gates, take item.
 *
 * Stage-1: full branch shape native, RAM writes inline. Cross-subsystem
 * shims (post_debit, take_item, cue_transfer_buf_and_advance_state,
 * progrt_set_room_flag_uw_item_state) Phase 4 deferred. */
void cave_update_talk_shop_or_door_charge(void);

/* State arm: hint cave / door-charge variant / money game.
 * Mirrors NES UpdateCavePersonState_HintOrMoneyGame (Z_01.asm:851).
 * Drain at src/oracle/cave/cave_runtime.c:285-324.
 *
 * Three sub-branches:
 *   1. CAVE_FLAGS & 0x10 (hint cave) -> select hint text via room/sel
 *      offset, point at line 2, clear chars, cue text transfer.
 *   2. CAVE_ROOM_TYPE >= 0x7B (door-charge variant) -> copy price list
 *      template, write prices, set sfx, post credit middle slot.
 *   3. else money game -> rupee gate ($0A), copy prizes to prices, write
 *      prices, prepend signs (gain $14/$32 = '+' = 100, else '-' = 98),
 *      post credit/debit on chosen amount.
 *
 * Stage-1: branch shape, RAM writes, sign-prepend native; cross-
 * subsystem shims (post_credit/_debit, copy_price_list_template,
 * write_prices_*, cue_transfer_buf, progrt_set_room_flag_uw_item_state)
 * Phase 4 deferred. */
void cave_update_hint_or_money_game(void);

/* Trivial state arm: hide person object after delay timer expires.
 * Mirrors NES UpdatePersonState_DelayThenHide (Z_01.asm:838).
 *
 *   if (CAVE_DELAY_TIMER == 0) CAVE_ROOM_TYPE = 0;
 *
 * No deferred shims, no cross-subsystem deps. Drain MATCH (trivial)
 * per Phase 3 summary. */
void cave_update_person_state_delay_then_hide(void);

/* Trivial cave-flag-bit-clear helper. Mirrors cavert_clear_prices_cave_flag
 * (drain trivial). Public version of the file-static cave_clear_prices_flag_inline
 * helper used by other cave functions. */
void cave_clear_prices_flag(void);

/* Top-level cave-person update orchestrator. Mirrors NES Z_01.asm
 * UpdateCavePerson (line 300). Drain MATCH per finding 3_4n_h.
 *
 * Frame skip: in state 4, skip draws + go directly to dispatch on
 * every other frame (FrameCounter bit 0).
 *
 * Medicine-shop ($74) letter handling: when InvLetter != 2 and the
 * letter is selected with B pressed, run the use-letter actions
 * (sfx + letter state++ + select potion); otherwise unhalt Link if
 * halted and return early without item draw or dispatch.
 *
 * Otherwise: draw cave items, then 9-way state dispatch via switch
 * (states 0..8, NES UpdateCavePerson_JumpTable).
 *
 * State arms 1, 3, 6, 7 are TODO Phase 4 (textbox + cue_transfer_*
 * shims). Other 5 arms are native. */
void cave_update_cave_person(unsigned int slot);

/* Format an unsigned 0..255 byte into BCD digits with leading-zero
 * suppression. Mirrors NES FormatDecimalByte (Z_01.asm:3129). Drain
 * MATCH per finding 3_4n_i.
 *
 * Outputs:
 *   CAVE_TMP1 = hundreds digit (0..9, or $24 = space if leading zero)
 *   CAVE_TMP2 = tens digit (0..9, or $24 if hundreds==0 AND tens==0)
 *   CAVE_TMP3 = units digit (0..9)
 *
 * Pure C, no shims. */
void cave_format_decimal_byte(unsigned char val);

/* Write all 3 ware prices into the dynamic transfer buffer using the
 * given price prefix character (e.g. $21 = "X" for shop, $24 = space
 * for price-list display). Mirrors NES WritePricesToDynamicTransferBuf
 * (Z_01.asm:455). Drain MATCH per finding 3_4n_i.
 *
 * Loops 3 wares: format price digits, swap space/sign for alignment,
 * write to DynTileBuf+5/6/7 at per-ware offset. Bumps cave delay timer
 * by 10 frames. cue_transfer_buf_and_advance_state is STAGE-1 STUB
 * pending Phase 4 native equivalent. */
void cave_write_prices_to_dynamic_transfer_buf(unsigned char price_char);

/* Copy 17 bytes of PriceListTemplateTransferBuf (Z_01.asm:295) into
 * the dynamic transfer buf (DynTileBuf @ $0302). Native port of NES
 * CopyPriceListTemplate (Z_01.asm:548). Used by cave_write_prices_*
 * and the door-charge branch of cave_update_hint_or_money_game. */
void cave_copy_price_list_template(void);

/* Write all 3 ware prices into the static price-list transfer buf.
 * Mirrors NES WritePricesTransferBuf (Z_01.asm:449). Two-step:
 * copy_price_list_template + write_prices_to_dynamic_transfer_buf(33).
 * Drain at src/oracle/cave/cave_runtime.c:126. */
void cave_write_prices_transfer_buf(void);

/* If item slot has lifetime < $F0 and Link is within 9px (Y axis
 * with +3 offset / X axis), mark slot collected and dispatch
 * item_take_item. NES TryTakeItem.
 * drain at cave_runtime.c:356-378. */
void cave_try_take_item(unsigned int slot);

/* Gate cave-room item pickup by action timer + room flag, then
 * stash CAVE_TMP4 with the dir + dispatch try_take_item. NES
 * TryTakeRoomItem. drain at cave_runtime.c:380-393. */
void cave_try_take_room_item(void);

/* Tick textbox char-streamer: advance one char into transfer buf,
 * dispatch line-flag handlers ($00 = mid-line, $40 = next, $80 =
 * blank, $C0 = end-of-text). NES UpdatePersonState_Textbox.
 * drain at cave_runtime.c:131-174.
 *
 * STAGE-1: Link draw side-render (c_link_end_move_and_draw_bank1)
 * stubbed pending native NES Link rendering pipeline. Text-render
 * core fully native. */
void cave_update_person_state_textbox(void);

#ifdef __cplusplus
}
#endif

#endif /* CAVE_DISPATCH_H */
