/* room_dispatch.h — native room subsystem dispatch (Phase 4).
 *
 * Native rewrite of select leaves from src/oracle/room/room_runtime.c.
 * Both ROMs link.
 */

#ifndef ROOM_DISPATCH_H
#define ROOM_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read SRAM room-flags pointer + return current room's flag byte.
 * Stashes pointer to SAVEFILE_PTR_LO/HI as side effect. NES GetRoomFlags. */
unsigned char room_get_room_flags(void);

/* Decode CUR_ROOM_ID into (row << 8) | col. NES SplitRoomId. */
unsigned int room_split_room_id(void);

/* if CUR_LEVEL == 0 return 0; else return (SRAM[$6A7E + col] & $80).
 * NES IsDarkRoom. */
unsigned char room_is_dark_room(unsigned int col);

/* ROOM_SFX_MAIN = $80; ROOM_SFX_AUX = $80. NES SilenceSound. */
void room_silence_sound(void);

/* Scan slots ROOM_MAX_MONSTER_SLOT..0 for living monsters. If none,
 * clear LINK_DAMAGE_DISABLE_FLAG and bump ROOM_MONSTER_ALL_DEAD.
 * NES CheckHasLivingMonsters. */
void room_check_has_living_monsters(void);

/* Reset ROOM_MODE_TIMER + SUBMODE_VALUE; return 0. NES EndGameMode
 * (the inner Plan-C drain in src/oracle/room/room_mode_runtime.c:249). */
unsigned char room_end_game_mode(void);

/* Walk OAM 64 sprites; set Y=$F8 to hide all. NES HideAllSprites.
 * drain at room_runtime.c:273-276. */
void room_hide_all_sprites(void);

/* CUR_ROOM_ID indexes SRAM unique-id table; mask bottom 6 bits.
 * NES GetUniqueRoomId. drain at room_runtime.c:278-281. */
unsigned char room_get_unique_room_id(void);

/* Clear ROOM_HISTORY[0..5] + ROOM_HISTORY_IDX. NES ClearRoomHistory.
 * drain at room_runtime.c:283-287. */
void room_clear_room_history(void);

/* Record room_id in the NES six-room cycling history if absent.
 * NES RunCrossRoomTasksAndBeginUpdateMode. */
void room_record_history(unsigned char room_id);

/* Reset LINK_ACTION_TIMER + LINK_HALT_FLAG. NES ResetPlayerState.
 * drain at room_runtime.c:289-292. */
void room_reset_player_state(void);

/* Set room-flag bit 5 (visited) at SRAM[ptr + CUR_ROOM_ID]. NES
 * MarkRoomVisited. drain at room_runtime.c:294-299. */
void room_mark_room_visited(void);

/* MODE_VALUE++; room_end_game_mode(). NES GoToNextMode.
 * drain at room_mode_runtime.c:255-258. */
void room_go_to_next_mode(void);

/* Copy a 22-byte room column from PlayArea ($6530-base) into the
 * transfer buffer at TRANSFER_BUF_POS, incrementing the buffer and
 * stashing PPU dst + src ptrs. NES CopyColumnToTilebuf.
 * drain at room_transfer_runtime.c:6-32. */
void room_copy_column_to_tilebuf(void);

/* go_to_next_mode + RAM($0394)=0. NES GoToNextMode_ResetGridOffset.
 * drain at room_mode_runtime.c:267-270. */
void room_go_to_next_mode_reset_grid_offset(void);

/* ITEM_SFX_SECONDARY=LevelSongIds[CUR_LEVEL]; go_to_next_mode;
 * RAM($0394)=0. NES GoToNextMode_PlayLevelSong.
 * drain at room_mode_runtime.c:260-265. */
void room_go_to_next_mode_play_level_song(void);

/* MODE_VALUE++; SUBMODE_VALUE=0; ROOM_MODE_TIMER=0; clear RAM($000F),
 * LINK_ACTION_TIMER, RAM($00C0), RAM($00D3), LINK_STUN_TIMER. NES
 * GoToNextMode_FromPlay. drain at room_mode_runtime.c:306-315. */
void room_go_to_next_mode_from_play(void);

/* If ROOM_INPUT_DIR is non-zero and Link is at the per-direction
 * screen-edge bound, go to next mode. NES CheckScreenEdge.
 * drain at room_runtime.c:301-322. */
void room_check_screen_edge(void);

/* PPU_MASK shadow = 0; clear NES_PPU_MASK_SHADOW. NES TurnOffAllVideo
 * (Z_07.asm:1739). Genesis-native: maintain shadow for re-upload via
 * existing pipeline. drain semantics: write 0 to RAM($00FE). */
void room_turn_off_all_video(void);

/* Heart-fill animation tick: if state set, advance partial heart by
 * +6 each tick; rollover to next heart when full; stop at hearts ==
 * containers. NES WorldFillHearts.
 * drain at room_object_runtime.c:31-48. */
void room_world_fill_hearts(void);

/* SwitchBank(5) [no-op on Genesis — MMC1 was NES] + WorldFillHearts +
 * WorldChangeRupees. NES UpdateHeartsAndRupees.
 * drain at room_mode_runtime.c:323-327. */
void room_update_hearts_and_rupees(void);

/* update_world_curtain_effect; if curtain done, [skip MMC1 ctrl —
 * Genesis no-op] + dispatch by cellar flag. NES UpdateMode3Unfurl.
 * drain at room_mode_runtime.c:295-304. */
void room_update_mode3_unfurl(void);

/* turn_off_all_video; UpdateMode2Load_Full (bank-window load + Q2
 * patch dispatch); go_to_next_mode. NES UpdateMode2_Load.
 * drain at room_mode_runtime.c:317-321 + room_load_runtime.c:299-325.
 *
 * Q1 (default quest) path fully native. Q2 (second quest) level>0
 * Q2-patch loop is STAGE-1 STUB pending bake of 9 LevelInfoUWQ2Replacements
 * blobs (~600 bytes). Q2 level=0 patch_q2_rooms native. */
void room_update_mode2_load(void);

/* Read MenuPalettesTransferBuf[20 + SaveSlotToPaletteRowOffset[slot]],
 * write to SRAM($0B92), set ROOM_TRANSFER_BUF_SELECT=24, advance
 * submode. Patches the level palette row 7 to match the player's
 * personal color (selected at file-select). NES
 * PatchAndCueLevelPalettesTransfer.
 * drain at room_mode_runtime.c:272-279. */
void room_patch_and_cue_level_palettes_transfer(void);
/* Patch half only (no transfer cue, no submode advance). */
void room_patch_level_palette_link_color(void);
/* InitMode5Play's sprite palette row 7 transfer cue. */
void room_init_mode5_play_palette_row7(void);

/* If CUR_LEVEL!=0 or ROOM_ID_ALT==$FF, read room_id from SRAM($0BAD);
 * else use ROOM_ID_ALT (and clear it). Then patch_and_cue. NES
 * InitMode3_Sub1. drain at room_mode_runtime.c:281-293. */
void room_init_mode3_sub1(void);

/* ROOM_INV_OBJ_ACTIVE = 0; ROOM_INV_OBJ_STATE[0..5] = 0.
 * NES ResetInvObjState. drain at room_load_runtime.c:25-30. */
void room_reset_inv_obj_state(void);

/* if !INVENTORY_VALUE(offset+OFF[level]) return 0 else return 1.
 * Helpers test compass-by-level (offset 16) / map-by-level
 * (offset 17). NES HasCompass / HasMap.
 * drain at room_runtime.c:24-45. */
unsigned char room_has_compass(void);
unsigned char room_has_map(void);

/* If OW (CUR_LEVEL=0) or no compass, no-op. Else
 * progress_update_position_marker(SRAM($0BAE), 4). NES
 * UpdateTriforcePositionMarker. drain at Z_07.asm:1821-1834. */
void room_update_triforce_position_marker(void);

/* Copy 5-byte ObjRoomBounds bank (OW base 0 / UW base 5) into
 * ROOM_BOUNDS[0..4]. Resets ROOM_IN_DOORWAY_FLAG when in OW.
 * NES SetupObjRoomBounds. drain at room_load_runtime.c:58-67. */
void room_setup_obj_room_bounds(void);

/* ROOM_SPRITE0_ENABLED = 1; copy 4-byte sprite0 descriptor into
 * OAM bytes 0..3. NES WriteAndEnableSprite0.
 * drain at room_load_runtime.c:13-18. */
void room_write_and_enable_sprite0(void);

/* ROOM_LINK_BG_ATTR_A/B |= 0x20. NES PutLinkBehindBackground.
 * drain at room_load_runtime.c:20-23. */
void room_put_link_behind_background(void);

/* ROOM_LINK_SPEED = 96 default; UW always 96; OW slows to 48 on
 * stair tiles ($74/$75) and resets fraction. NES InitLinkSpeed.
 * drain at room_load_runtime.c:69-81. */
void room_init_link_speed(void);

/* Fill 48-byte ROOM_PALETTE_ATTR with outer-attr from SRAM($687E+room_id),
 * then patch inner 5x5 region with inner-attr from SRAM($68FE+room_id),
 * with low-nibble blend on row 4 (d3 >= 0x21). NES FillPlayAreaAttrs.
 * drain at room_load_runtime.c:32-56. */
void room_fill_play_area_attrs(unsigned int room_id);

/* Get tile-still under slot 0; if push-block tile ($24), clear
 * triforce-hold + sfx-aux=8 + push-timer=LINK_Y+$10. Always
 * advance ROOM_MODE_TIMER. NES InitMode10.
 * drain at room_object_runtime.c:7-15. */
void room_init_mode10(void);

/* Reset all action-mode state for end-of-room: SUBMODE/timer/COMBAT
 * /LINK_ACTION/MON_SHOVE/STUN. NES EndPrepareMode.
 * drain at room_object_runtime.c:50-58. */
void room_end_prepare_mode(void);

/* Setup tile-object spawn type for OW. Special-case rooms $3F/$55
 * use type 97; else copy from ROOM_TILE_OBJ_0/1/2.
 * drain at room_object_runtime.c:17-29. */
void room_setup_tile_object_ow(void);

/* Mode-helper leaves — small RAM-state ops used by NES dispatch
 * tables. drain at room_mode_runtime.c. */
void room_inc_submode(void);                        /* 8-10 */
void room_inc_2_submodes(void);                     /* 12-15 */
void room_init_mode4_go_to_sub0(void);              /* 23-26 */
void room_reset_vscroll_lo(void);                   /* 33-36 */
void room_select_transfer_buf(unsigned int val);    /* 38-41 */
void room_select_transfer_buf_and_inc_state(unsigned int val); /* 43-46 */
void room_set_fade_cycle_and_advance_submode(unsigned int val); /* 65-68 */
void room_switch_to_nt1(void);                      /* 129-131 */
void room_update_menu_common2(void);                /* 107-109 */
void room_update_menu_common3(void);                /* 111-113 */
void room_update_menu_common4(void);                /* 115-117 */
void room_update_menu5_ow(void);                    /* 119-121 */
void room_init_mode9_transfer_attrs(void);          /* 153-155 */
void room_init_mode_a_sub_a_go_to_mode4(void);      /* 17-21 */
void room_update_mode11_death_set_timer_inc_submode(unsigned int val);
void room_update_mode11_death_sub4(void);           /* 138-140 */
void room_update_mode11_death_sub5(void);           /* 142-145 */
void room_update_mode11_death_sub6(void);           /* 28-31 */
void room_update_mode11_death_sub9(void);           /* 147-151 */
void room_start_filling_hearts(void);               /* 157-160 */
void room_init_mode7_finish(void);                  /* 123-127 */

/* ROOM_MENU_SCROLL_POS--. NES DecSubmenuScroll.
 * drain at room_object_runtime.c:60-62. */
void room_dec_submenu_scroll(void);

/* Copy 32-byte row at ROOM_ROW_INDEX into transfer buffer.
 * NES CopyRowToTilebuf. drain at room_transfer_runtime.c:34-60. */
void room_copy_row_to_tilebuf(void);

/* Cycle d3 in [0..8] up/down by ROOM_CYCLE_DIR.
 * NES Cycle9InDirection. drain at room_transfer_runtime.c:62-76. */
unsigned int room_cycle9_in_direction(unsigned int d3_in);

/* Dispatch row vs column copy by ROOM_ROW_INDEX.
 * NES CopyColumnOrRowToTilebuf. drain at room_transfer_runtime.c:78-90. */
void room_copy_column_or_row_to_tilebuf(void);

/* SAVEFILE_PTR := $6530. NES FetchTileMapAddr.
 * drain at room_transfer_runtime.c:92-95. */
void room_fetch_tile_map_addr(void);

/* Reverse-copy 24 ROOM_PALETTE_ATTR bytes into transfer buf, head with
 * PPU dst hi/lo. NES CopyPlayAreaAttrsHalf.
 * drain at room_transfer_runtime.c:97-108. */
void room_copy_play_area_attrs_half(unsigned int ppu_hi,
                                    unsigned int ppu_lo,
                                    unsigned int end_off);

/* Player-coord leaves — drains at room_player_runtime.c. */
void room_player_get_coords_for_direction(unsigned int dir);
unsigned int room_player_is_distance_safe_to_spawn(unsigned int slot);
void room_player_set_moving_dir_and_switch_to_player_slot(unsigned int dir);
void room_player_link_modify_dir_in_doorway(void);

/* Door / secret-trigger / touch leaves — drains at room_runtime.c. */
void room_calc_open_doorway_mask(unsigned int attr, unsigned int dir_idx);
void room_add_door_flags(void);
void room_set_door_flag(unsigned int dir_idx);
void room_reset_door_flag(unsigned int dir_idx);
void room_set_entering_doorway(void);
void room_save_kill_count_ow(unsigned int slot);
void room_save_kill_count_uw(void);
void room_trigger_open_door(unsigned int val);
void room_touch_door_wall(void);
void room_touch_door_open(void);
void room_wield_nothing(void);
void room_mask_cur_ppu_mask_grayscale(void);
void room_block_at_wall(void);
unsigned int room_check_secret_trigger_none(void);
unsigned int room_trigger_shutters(void);
unsigned int room_return_false(void);
unsigned int room_check_secret_trigger_all_dead(void);
unsigned int room_check_secret_trigger_last_boss(void);
unsigned int room_check_secret_trigger_money_or_life(void);
unsigned int room_check_secret_trigger_block_door(void);
unsigned int room_check_secret_trigger_block_stairs(void);
unsigned int room_check_secret_trigger_ringleader(void);
unsigned int room_check_secret_trigger_block_stairs(void);
void room_check_underworld_secrets(void);   /* Z_05 CheckUnderworldSecrets */
void room_touch_door_bombable(void);
void room_block_until_time(void);
unsigned int room_touch_door_false(void);
void room_touch_door_shutter(void);

/* Mode-7 scroll + mode-3 init + mode-11 death + mode-12 end-level
 * leaves — drains at room_mode_runtime.c. */
unsigned int room_copy_next_row_to_transfer_buf(void);
unsigned int room_copy_next_row_advance_submode(void);
void room_update_mode7_scroll_sub2(void);
void room_update_mode7_scroll_sub6(void);
void room_update_mode7_scroll_sub7(void);
void room_cue_transfer_play_area_attrs_half_and_advance_submode(
    unsigned int ppu_hi, unsigned int ppu_lo, unsigned int end_off);
void room_init_mode_b_sub1(void);
void room_update_mode12_end_level_sub1(void);
void room_init_mode3_sub2(void);
void room_init_mode3_sub3(void);
void room_init_mode3_sub4(void);
void room_init_mode3_sub5(void);
void room_init_mode3_sub6(void);
void room_init_mode3_sub7(void);
void room_init_mode_a_sub1(void);
void room_update_mode11_death_sub_c(void);
void room_update_mode11_death_sub2(void);
void room_end_game_mode12(void);

/* T-050 OW tile objects (slot $B): InitTileObjOrItem and the NES
 * UpdateObject rows $62-$67. */
void room_init_tile_obj_or_item(unsigned int slot);
void room_update_rock_or_gravestone(unsigned int slot);
void room_update_rock_wall(unsigned int slot);
void room_update_tree(unsigned int slot);

#ifdef __cplusplus
}
#endif

/* T-171 flute pond secret (Z_07.asm UpdateFluteSecret / AnimatePond). */
void room_update_flute_secret(unsigned int slot);
void room_animate_pond(void);
void room_cue_pond_palette_row(unsigned char y);

#endif /* ROOM_DISPATCH_H */
